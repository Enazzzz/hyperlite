#include <chrono>
#include <cstdint>
#include <iostream>

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

} // namespace

/**
 * Benchmark command submission and CPU raster execution throughput.
 */
int main() {
	hyperlite::Engine engine(1280, 720, hyperlite::BackendKind::kCpu, "Hyperlite Bench", hyperlite::PresentMode::kHeadless);
	constexpr int frame_iterations = 120;
	constexpr int draw_calls_per_frame = 50'000;

	const auto t0 = std::chrono::high_resolution_clock::now();
	for (int frame = 0; frame < frame_iterations; ++frame) {
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		for (int i = 0; i < draw_calls_per_frame; ++i) {
			const int x = (i * 37) % 1280;
			const int y = (i * 53) % 720;
			engine.PushCommand(hyperlite::MakeDrawCommand(
				hyperlite::CommandType::kPutPixel, x, y, 0, 0, hyperlite::PackColor({255, 140, 40, 255})));
		}
		engine.EndFrame();
	}
	const auto t1 = std::chrono::high_resolution_clock::now();

	const double ms = ElapsedMs(t0, t1);
	const double draws = static_cast<double>(frame_iterations) * static_cast<double>(draw_calls_per_frame);
	const double draws_per_second = draws / (ms / 1000.0);

	std::cout << "frames=" << frame_iterations << '\n';
	std::cout << "total_ms=" << ms << '\n';
	std::cout << "draws_per_second=" << draws_per_second << '\n';
	return 0;
}
