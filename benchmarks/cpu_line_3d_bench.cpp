#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
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
 * Column-major perspective matrix (OpenGL-style, -Z forward, near/far positive distances).
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
 * Simple look-at from eye looking toward -Z (column-major view * already in view_proj after mul).
 * Here we bake a fixed camera: eye at (0,0,3) looking at origin.
 */
void FillViewLookAtOrigin(float* view) {
	for (int i = 0; i < 16; ++i) {
		view[i] = 0.0f;
	}
	// Right = +X, Up = +Y, Forward = -Z; eye (0,0,3).
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
 * Fill world-space segments: short diagonals (default) or long spans (multi-tile after project).
 *
 * short: ~few tens of pixels after project (existing wireframe grid).
 * long:  ~hundreds of pixels / multiple 64×64 tiles — exercises tiled depth OpenMP.
 */
void FillWorldSegments(std::vector<float>& segments, const int frame, const bool long_segs) {
	const std::size_t line_count = segments.size() / 6U;
	for (std::size_t i = 0; i < line_count; ++i) {
		const int slot = static_cast<int>(i);
		const float phase = static_cast<float>((slot + frame) % 97) * 0.01f;
		const float x0 = -1.5f + static_cast<float>(slot % 50) * 0.06f;
		const float y0 = -1.0f + static_cast<float>((slot / 50) % 40) * 0.05f;
		const float z0 = -0.2f - phase;
		const float dx = long_segs ? 1.4f : 0.08f;
		const float dy = long_segs ? 0.9f : 0.05f;
		const float dz = long_segs ? -0.4f : -0.1f;
		const float x1 = x0 + dx;
		const float y1 = y0 + dy;
		const float z1 = z0 + dz;
		const std::size_t base = i * 6U;
		segments[base + 0U] = x0;
		segments[base + 1U] = y0;
		segments[base + 2U] = z0;
		segments[base + 3U] = x1;
		segments[base + 4U] = y1;
		segments[base + 5U] = z1;
	}
}

} // namespace

/**
 * Benchmark TickLines3d (10k world segments, perspective camera, depth on, 1280x720).
 *
 * Pass "long" as argv[1] for multi-tile segments; default is the short-diagonal workload.
 */
int main(int argc, char** argv) {
	const bool long_segs = argc > 1 && std::strcmp(argv[1], "long") == 0;

	hyperlite::Engine engine(1280, 720, hyperlite::BackendKind::kCpu, "Hyperlite 3D Line Bench", hyperlite::PresentMode::kHeadless);
	engine.SetVsync(false);
	engine.EnableDepth(true);

	float view[16];
	float proj[16];
	float view_proj[16];
	FillViewLookAtOrigin(view);
	FillPerspective(proj, 1.04719755f /* 60 deg */, 1280.0f / 720.0f, 0.1f, 100.0f);
	MulMat4(proj, view, view_proj);
	engine.SetViewProj(view_proj);

	constexpr int frame_iterations = 120;
	constexpr std::size_t lines_per_frame = 10'000U;
	constexpr std::uint32_t clear_packed = hyperlite::PackColor({8, 12, 20, 255});
	constexpr std::uint32_t line_packed = hyperlite::PackColor({0, 255, 80, 255});

	std::vector<float> segments(lines_per_frame * 6U);
	FillWorldSegments(segments, 0, long_segs);

	const auto t0 = std::chrono::high_resolution_clock::now();
	for (int frame = 0; frame < frame_iterations; ++frame) {
		FillWorldSegments(segments, frame, long_segs);
		engine.TickLines3d(
			clear_packed,
			segments.data(),
			lines_per_frame,
			line_packed,
			1);
	}
	const auto t1 = std::chrono::high_resolution_clock::now();

	const double ms = ElapsedMs(t0, t1);
	const double lines = static_cast<double>(frame_iterations) * static_cast<double>(lines_per_frame);
	const double lines_per_second = lines / (ms / 1000.0);

	std::cout << "frames=" << frame_iterations << '\n';
	std::cout << "lines_per_frame=" << lines_per_frame << '\n';
	std::cout << "resolution=1280x720\n";
	std::cout << "depth=on\n";
	std::cout << "workload=" << (long_segs ? "long" : "short") << '\n';
	std::cout << "total_ms=" << ms << '\n';
	std::cout << "lines_per_second=" << lines_per_second << '\n';
	return 0;
}
