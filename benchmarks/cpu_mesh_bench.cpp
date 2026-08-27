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

} // namespace

/**
 * Benchmark retained mesh draw (load once, draw N times) — flat and textured.
 *
 * Mesh: 70×70 quads × 2 tris. Drawn once per frame via TickMesh / TickMeshTextured
 * @ 1280×720 depth on. Pass argv[1]=="textured" to run the textured path only;
 * otherwise run flat then textured and print both.
 */
int main(int argc, char** argv) {
	const bool textured_only = (argc > 1 && std::string(argv[1]) == "textured");
	const bool flat_only = (argc > 1 && std::string(argv[1]) == "flat");

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
	const std::size_t tris_per_draw = indices.size() / 3U;
	const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());
	if (mesh < 0) {
		std::cerr << "load_mesh failed\n";
		return 1;
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
			engine.TickMesh(clear_packed, mesh, model, tri_packed);
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
		std::cout << "path=" << path << '\n';
		std::cout << "total_ms=" << ms << '\n';
		std::cout << "tris_per_second=" << tris_per_second << '\n';
	};

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
