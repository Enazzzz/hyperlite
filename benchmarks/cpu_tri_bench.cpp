#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "engine/command_buffer.hpp"
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
 * Fill world-space triangles (small quads as two tris) in front of the camera.
 */
void FillWorldTris(float* dst, const std::size_t tri_count, const int frame) {
	for (std::size_t i = 0; i < tri_count; ++i) {
		const int slot = static_cast<int>(i);
		const float phase = static_cast<float>((slot + frame) % 97) * 0.01f;
		const float x = -1.5f + static_cast<float>(slot % 50) * 0.06f;
		const float y = -1.0f + static_cast<float>((slot / 50) % 40) * 0.05f;
		const float z = -0.2f - phase;
		const float s = 0.05f;
		const std::size_t base = i * 9U;
		// CCW in world/NDC (OpenGL front).
		dst[base + 0U] = x - s;
		dst[base + 1U] = y - s;
		dst[base + 2U] = z;
		dst[base + 3U] = x + s;
		dst[base + 4U] = y - s;
		dst[base + 5U] = z;
		dst[base + 6U] = x;
		dst[base + 7U] = y + s;
		dst[base + 8U] = z;
	}
}

/**
 * Two tris covering the view at a nearer z (opaque occluder for Hi-Z benches).
 */
void FillScreenOccluderTris(float* dst) {
	const float z = -0.05f;
	// Tri 0
	dst[0U] = -3.0f;
	dst[1U] = -3.0f;
	dst[2U] = z;
	dst[3U] = 3.0f;
	dst[4U] = -3.0f;
	dst[5U] = z;
	dst[6U] = 3.0f;
	dst[7U] = 3.0f;
	dst[8U] = z;
	// Tri 1
	dst[9U] = -3.0f;
	dst[10U] = -3.0f;
	dst[11U] = z;
	dst[12U] = 3.0f;
	dst[13U] = 3.0f;
	dst[14U] = z;
	dst[15U] = -3.0f;
	dst[16U] = 3.0f;
	dst[17U] = z;
}

} // namespace

/**
 * Benchmark TickTris3d (10k world tris, perspective camera, depth on, 1280x720).
 *
 * Pass argv[1]=="occluded" to prepend a fullscreen occluder (2 tris) before the 10k back-field tris.
 * Pass argv[1]=="occluded-2draw" for two Raster* calls per frame (clear + occluder draw, then back field).
 */
int main(int argc, char** argv) {
	const bool occluded_2draw = (argc > 1 && std::string(argv[1]) == "occluded-2draw");
	const bool occluded = occluded_2draw || (argc > 1 && std::string(argv[1]) == "occluded");

	hyperlite::Engine engine(1280, 720, hyperlite::BackendKind::kCpu, "Hyperlite 3D Tri Bench", hyperlite::PresentMode::kHeadless);
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

	constexpr int frame_iterations = 120;
	constexpr std::size_t back_tris = 10'000U;
	constexpr std::size_t occluder_tris = 2U;
	const std::size_t tris_per_frame = occluded ? (occluder_tris + back_tris) : back_tris;
	constexpr std::uint32_t clear_packed = hyperlite::PackColor({8, 12, 20, 255});
	constexpr std::uint32_t tri_packed = hyperlite::PackColor({0, 200, 255, 255});

	std::vector<float> verts(tris_per_frame * 9U);
	std::vector<float> occ_verts(occluder_tris * 9U);
	std::vector<float> back_verts(back_tris * 9U);

	const auto t0 = std::chrono::high_resolution_clock::now();
	for (int frame = 0; frame < frame_iterations; ++frame) {
		if (occluded_2draw) {
			FillScreenOccluderTris(occ_verts.data());
			FillWorldTris(back_verts.data(), back_tris, frame);
			engine.BeginFrame();
			engine.PushCommand(hyperlite::MakeDrawCommand(
				hyperlite::CommandType::kClear, 0, 0, 0, 0, clear_packed));
			engine.EndFrame();
			engine.Tris3d(occ_verts.data(), occluder_tris, tri_packed);
			engine.Tris3d(back_verts.data(), back_tris, tri_packed);
			engine.PollEvents();
			engine.Present();
		} else if (occluded) {
			FillScreenOccluderTris(verts.data());
			FillWorldTris(verts.data() + occluder_tris * 9U, back_tris, frame);
			engine.TickTris3d(
				clear_packed,
				verts.data(),
				tris_per_frame,
				tri_packed);
		} else {
			FillWorldTris(verts.data(), back_tris, frame);
			engine.TickTris3d(
				clear_packed,
				verts.data(),
				tris_per_frame,
				tri_packed);
		}
	}
	const auto t1 = std::chrono::high_resolution_clock::now();

	const double ms = ElapsedMs(t0, t1);
	const double tris = static_cast<double>(frame_iterations) * static_cast<double>(tris_per_frame);
	const double tris_per_second = tris / (ms / 1000.0);

	std::cout << "frames=" << frame_iterations << '\n';
	std::cout << "tris_per_frame=" << tris_per_frame << '\n';
	std::cout << "resolution=1280x720\n";
	std::cout << "depth=on\n";
	if (occluded) {
		std::cout << "workload=occluded\n";
	}
	if (occluded_2draw) {
		std::cout << "workload=occluded-2draw\n";
		std::cout << "draws_per_frame=2\n";
	}
	std::cout << "total_ms=" << ms << '\n';
	std::cout << "tris_per_second=" << tris_per_second << '\n';
	return 0;
}
