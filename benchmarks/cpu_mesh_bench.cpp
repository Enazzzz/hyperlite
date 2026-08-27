#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "engine/engine.hpp"

namespace {

/**
 * Elapsed milliseconds between two steady-clock samples.
 */
double ElapsedMs(
	const std::chrono::high_resolution_clock::time_point start,
	const std::chrono::high_resolution_clock::time_point stop) {
	return std::chrono::duration<double, std::milli>(stop - start).count();
}

/**
 * Column-major perspective matrix (OpenGL-style, -Z forward).
 */
void FillPerspective(float* m, const float fovy_rad, const float aspect, const float znear, const float zfar) {
	for (int i = 0; i < 16; ++i) {
		m[i] = 0.0f;
	}
	const float f = 1.0f / std::tan(fovy_rad * 0.5f);
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (zfar + znear) / (znear - zfar);
	m[11] = -1.0f;
	m[14] = (2.0f * zfar * znear) / (znear - zfar);
}

/**
 * View: eye at (0,0,3) looking toward origin (-Z).
 */
void FillViewLookAtOrigin(float* view) {
	for (int i = 0; i < 16; ++i) {
		view[i] = 0.0f;
	}
	view[0] = 1.0f;
	view[5] = 1.0f;
	view[10] = 1.0f;
	view[14] = -3.0f;
	view[15] = 1.0f;
}

/**
 * Multiply column-major 4x4 matrices: out = a * b.
 */
void MulMat4(const float* a, const float* b, float* out) {
	float tmp[16];
	for (int col = 0; col < 4; ++col) {
		for (int row = 0; row < 4; ++row) {
			tmp[col * 4 + row] =
				a[0 * 4 + row] * b[col * 4 + 0] +
				a[1 * 4 + row] * b[col * 4 + 1] +
				a[2 * 4 + row] * b[col * 4 + 2] +
				a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
	for (int i = 0; i < 16; ++i) {
		out[i] = tmp[i];
	}
}

/**
 * Identity column-major 4x4.
 */
void FillIdentity(float* m) {
	for (int i = 0; i < 16; ++i) {
		m[i] = 0.0f;
	}
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/**
 * Build a grid mesh of small quads (2 tris each) in front of the camera.
 *
 * Layout: 6 floats/vert (x,y,z,u,v,_pad). Indexed so shared verts are reused.
 */
void BuildGridMesh(
	std::vector<float>& verts,
	std::vector<std::uint32_t>& indices,
	const int quads_x,
	const int quads_y) {
	const int verts_x = quads_x + 1;
	const int verts_y = quads_y + 1;
	verts.clear();
	verts.reserve(static_cast<std::size_t>(verts_x * verts_y) * 6U);
	for (int iy = 0; iy < verts_y; ++iy) {
		for (int ix = 0; ix < verts_x; ++ix) {
			const float x = -1.5f + static_cast<float>(ix) * (3.0f / static_cast<float>(quads_x));
			const float y = -1.0f + static_cast<float>(iy) * (2.0f / static_cast<float>(quads_y));
			const float z = -0.5f;
			const float u = static_cast<float>(ix) / static_cast<float>(quads_x);
			const float v = static_cast<float>(iy) / static_cast<float>(quads_y);
			verts.push_back(x);
			verts.push_back(y);
			verts.push_back(z);
			verts.push_back(u);
			verts.push_back(v);
			verts.push_back(0.0f);
		}
	}
	indices.clear();
	indices.reserve(static_cast<std::size_t>(quads_x * quads_y) * 6U);
	for (int iy = 0; iy < quads_y; ++iy) {
		for (int ix = 0; ix < quads_x; ++ix) {
			const std::uint32_t i00 = static_cast<std::uint32_t>(iy * verts_x + ix);
			const std::uint32_t i10 = i00 + 1U;
			const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(verts_x);
			const std::uint32_t i11 = i01 + 1U;
			// CCW in world/NDC (OpenGL front).
			indices.push_back(i00);
			indices.push_back(i10);
			indices.push_back(i11);
			indices.push_back(i00);
			indices.push_back(i11);
			indices.push_back(i01);
		}
	}
}

/**
 * Fullscreen quad mesh (2 tris) nearer than the grid — occluder for Hi-Z benches.
 */
void BuildOccluderMesh(
	std::vector<float>& verts,
	std::vector<std::uint32_t>& indices) {
	verts = {
		-3.0f, -3.0f, -0.05f, 0.0f, 0.0f, 0.0f,
		3.0f, -3.0f, -0.05f, 1.0f, 0.0f, 0.0f,
		3.0f, 3.0f, -0.05f, 1.0f, 1.0f, 0.0f,
		-3.0f, 3.0f, -0.05f, 0.0f, 1.0f, 0.0f,
	};
	indices = {0U, 1U, 2U, 0U, 2U, 3U};
}

} // namespace

/**
 * Benchmark retained mesh draw (load once, draw N times) — flat and textured.
 *
 * Mesh: 70×70 quads × 2 tris. Drawn once per frame via TickMesh / TickMeshTextured
 * @ 1280×720 depth on. Pass argv[1]=="textured" to run the textured path only;
 * "flat" for flat only; "occluded" for flat with a fullscreen occluder draw first;
 * "instanced" for flat DrawMeshMany stress (128 instances/frame, single bin/fill).
 * Otherwise run flat then textured and print both.
 */
int main(int argc, char** argv) {
	const bool textured_only = (argc > 1 && std::string(argv[1]) == "textured");
	const bool flat_only = (argc > 1 && std::string(argv[1]) == "flat");
	const bool occluded = (argc > 1 && std::string(argv[1]) == "occluded");
	const bool instanced = (argc > 1 && std::string(argv[1]) == "instanced");

	hyperlite::Engine engine(1280, 720, hyperlite::BackendKind::kCpu, "Hyperlite 3D Mesh Bench", hyperlite::PresentMode::kHeadless);
	engine.SetVsync(false);
	engine.EnableDepth(true);
	engine.SetCullBackfaces(true);

	float view[16];
	float proj[16];
	float view_proj[16];
	FillViewLookAtOrigin(view);
	FillPerspective(proj, 1.04719755f /* 60 deg */, 1280.0f / 720.0f, 0.1f, 100.0f);
	MulMat4(proj, view, view_proj);
	engine.SetViewProj(view_proj);

	constexpr int quads_x = 70;
	constexpr int quads_y = 70;
	std::vector<float> verts;
	std::vector<std::uint32_t> indices;
	BuildGridMesh(verts, indices, quads_x, quads_y);
	const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());
	if (mesh < 0) {
		std::cerr << "load_mesh failed\n";
		return 1;
	}

	int draw_mesh = mesh;
	std::size_t tris_per_draw = indices.size() / 3U;
	if (occluded) {
		std::vector<float> combined_verts;
		std::vector<std::uint32_t> combined_indices;
		std::vector<float> occ_verts;
		std::vector<std::uint32_t> occ_indices;
		BuildOccluderMesh(occ_verts, occ_indices);
		combined_verts = occ_verts;
		combined_verts.insert(combined_verts.end(), verts.begin(), verts.end());
		combined_indices = occ_indices;
		const std::uint32_t vert_offset = static_cast<std::uint32_t>(occ_verts.size() / 6U);
		for (const std::uint32_t idx : indices) {
			combined_indices.push_back(idx + vert_offset);
		}
		tris_per_draw = combined_indices.size() / 3U;
		draw_mesh = engine.LoadMesh(
			combined_verts.data(),
			combined_verts.size(),
			combined_indices.data(),
			combined_indices.size());
		if (draw_mesh < 0) {
			std::cerr << "load_combined_mesh failed\n";
			return 1;
		}
	}

	// Opaque 64×64 checker atlas so textured sampling is non-trivial.
	constexpr int atlas_w = 64;
	constexpr int atlas_h = 64;
	std::vector<std::uint8_t> atlas_px(static_cast<std::size_t>(atlas_w * atlas_h) * 4U);
	for (int y = 0; y < atlas_h; ++y) {
		for (int x = 0; x < atlas_w; ++x) {
			const bool on = ((x / 8) + (y / 8)) % 2 == 0;
			const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(atlas_w) + static_cast<std::size_t>(x)) * 4U;
			atlas_px[i + 0U] = on ? 0 : 255;
			atlas_px[i + 1U] = on ? 200 : 0;
			atlas_px[i + 2U] = on ? 255 : 80;
			atlas_px[i + 3U] = 255;
		}
	}
	const int atlas = engine.LoadAtlas(atlas_px.data(), atlas_px.size(), atlas_w, atlas_h);
	if (atlas < 0) {
		std::cerr << "load_atlas failed\n";
		return 1;
	}

	float model[16];
	FillIdentity(model);

	constexpr int frame_iterations = 120;
	constexpr std::uint32_t clear_packed = hyperlite::PackColor({8, 12, 20, 255});
	constexpr std::uint32_t tri_packed = hyperlite::PackColor({0, 200, 255, 255});

	auto run_flat = [&]() -> double {
		FillIdentity(model);
		const auto t0 = std::chrono::high_resolution_clock::now();
		for (int frame = 0; frame < frame_iterations; ++frame) {
			model[14] = 0.001f * static_cast<float>(frame % 17);
			engine.TickMesh(clear_packed, draw_mesh, model, tri_packed);
		}
		const auto t1 = std::chrono::high_resolution_clock::now();
		return ElapsedMs(t0, t1);
	};

	auto run_textured = [&]() -> double {
		FillIdentity(model);
		const auto t0 = std::chrono::high_resolution_clock::now();
		for (int frame = 0; frame < frame_iterations; ++frame) {
			model[14] = 0.001f * static_cast<float>(frame % 17);
			engine.TickMeshTextured(clear_packed, mesh, model, atlas);
		}
		const auto t1 = std::chrono::high_resolution_clock::now();
		return ElapsedMs(t0, t1);
	};

	auto print_result = [&](const char* path, const double ms) {
		const double tris = static_cast<double>(frame_iterations) * static_cast<double>(tris_per_draw);
		const double tris_per_second = tris / (ms / 1000.0);
		std::cout << "frames=" << frame_iterations << '\n';
		std::cout << "tris_per_frame=" << tris_per_draw << '\n';
		std::cout << "draws_per_frame=1\n";
		std::cout << "resolution=1280x720\n";
		std::cout << "depth=on\n";
		if (occluded) {
			std::cout << "workload=occluded\n";
		}
		std::cout << "path=" << path << '\n';
		std::cout << "total_ms=" << ms << '\n';
		std::cout << "tris_per_second=" << tris_per_second << '\n';
	};

	if (occluded) {
		print_result("TickMesh", run_flat());
		return 0;
	}

	if (instanced) {
		constexpr int kInstances = 128;
		std::vector<float> models(static_cast<std::size_t>(kInstances) * 16U);
		for (int i = 0; i < kInstances; ++i) {
			float* m = models.data() + static_cast<std::size_t>(i) * 16U;
			FillIdentity(m);
			m[12] = 0.002f * static_cast<float>(i % 32);
			m[14] = 0.002f * static_cast<float>((i / 32) % 32);
		}
		const auto t0 = std::chrono::high_resolution_clock::now();
		for (int frame = 0; frame < frame_iterations; ++frame) {
			engine.BeginFrame();
			engine.PushCommand(hyperlite::MakeDrawCommand(
				hyperlite::CommandType::kClear, 0, 0, 0, 0, clear_packed));
			engine.DrawMeshMany(draw_mesh, models.data(), static_cast<std::size_t>(kInstances), tri_packed);
			engine.EndFrame();
			engine.Present();
		}
		const auto t1 = std::chrono::high_resolution_clock::now();
		const double ms = ElapsedMs(t0, t1);
		const double tris = static_cast<double>(frame_iterations) * static_cast<double>(tris_per_draw) *
			static_cast<double>(kInstances);
		const double tris_per_second = tris / (ms / 1000.0);
		std::cout << "frames=" << frame_iterations << '\n';
		std::cout << "tris_per_frame=" << (tris_per_draw * static_cast<std::size_t>(kInstances)) << '\n';
		std::cout << "draws_per_frame=1\n";
		std::cout << "instances_per_draw=" << kInstances << '\n';
		std::cout << "resolution=1280x720\n";
		std::cout << "depth=on\n";
		std::cout << "path=DrawMeshMany\n";
		std::cout << "total_ms=" << ms << '\n';
		std::cout << "tris_per_second=" << tris_per_second << '\n';
		return 0;
	}

	if (!textured_only) {
		print_result("TickMesh", run_flat());
	}
	if (!flat_only) {
		if (!textured_only) {
			std::cout << "---\n";
		}
		print_result("TickMeshTextured", run_textured());
	}
	return 0;
}
