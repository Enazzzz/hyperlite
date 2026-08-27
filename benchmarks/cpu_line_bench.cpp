#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"

namespace {

/**
 * Compute elapsed milliseconds as floating-point.
 */
double ElapsedMs(
	const std::chrono::high_resolution_clock::time_point start,
	const std::chrono::high_resolution_clock::time_point stop) {
	return std::chrono::duration<double, std::milli>(stop - start).count();
}

/**
 * Fill a preallocated segment buffer with a dense wireframe grid.
 */
void FillWireframeSegments(std::vector<std::int32_t>& segments, const int width, const int height, const int frame) {
	const std::size_t values_per_line = 4U;
	const std::size_t line_count = segments.size() / values_per_line;
	for (std::size_t i = 0; i < line_count; ++i) {
		const int slot = static_cast<int>(i);
		const int x0 = (slot * 37 + frame * 3) % (width - 1);
		const int y0 = (slot * 53 + frame * 5) % (height - 1);
		const int x1 = (x0 + 24 + (slot & 7)) % width;
		const int y1 = (y0 + 16 + ((slot >> 3) & 7)) % height;
		const std::size_t base = i * values_per_line;
		segments[base + 0U] = x0;
		segments[base + 1U] = y0;
		segments[base + 2U] = x1;
		segments[base + 3U] = y1;
	}
}

} // namespace

/**
 * Benchmark TickLines fused wireframe path via Engine.
 */
int main() {
	hyperlite::Engine engine(1280, 720, hyperlite::BackendKind::kCpu, "Hyperlite Line Bench", hyperlite::PresentMode::kHeadless);
	engine.SetVsync(false);

	constexpr int frame_iterations = 120;
	constexpr std::size_t lines_per_frame = 10'000U;
	constexpr std::uint32_t clear_packed = hyperlite::PackColor({8, 12, 20, 255});
	constexpr std::uint32_t line_packed = hyperlite::PackColor({0, 255, 80, 255});

	std::vector<std::int32_t> segments(lines_per_frame * 4U);
	FillWireframeSegments(segments, 1280, 720, 0);

	const auto t0 = std::chrono::high_resolution_clock::now();
	for (int frame = 0; frame < frame_iterations; ++frame) {
		FillWireframeSegments(segments, 1280, 720, frame);
		engine.TickLines(
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
	std::cout << "total_ms=" << ms << '\n';
	std::cout << "lines_per_second=" << lines_per_second << '\n';
	return 0;
}
