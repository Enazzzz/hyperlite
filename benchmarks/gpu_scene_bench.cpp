#include <chrono>
#include <cstdint>
#include <iostream>

#include "engine/backend_interface.hpp"
#include "engine/framebuffer.hpp"
#include "engine/types.hpp"

namespace {

/**
 * One complexity point in the sweep.
 */
struct SceneConfig {
	int instances;
	int segments;
};

/**
 * Wall-clock milliseconds between two timestamps.
 */
double ElapsedMs(
	const std::chrono::high_resolution_clock::time_point start,
	const std::chrono::high_resolution_clock::time_point stop) {
	return std::chrono::duration<double, std::milli>(stop - start).count();
}

} // namespace

/**
 * Headless GPU complexity sweep: clears, generates, and rasterizes the spiro
 * scene fully on device, reading back each frame, with no window or Python in
 * the loop. Prints frame time and stage timing per complexity point so the GPU
 * path can be tuned in isolation from presentation overhead.
 */
int main() {
	constexpr int width = 1280;
	constexpr int height = 720;
	constexpr int frames_per_config = 300;
	constexpr int warmup_frames = 30;

	auto backend = hyperlite::CreateBackend(hyperlite::BackendKind::kGpu);
	if (backend->Name() != "gpu") {
		std::cout << "CUDA backend unavailable; skipping GPU scene benchmark.\n";
		return 0;
	}

	hyperlite::FrameBuffer framebuffer(width, height);
	backend->EnsureSized(width, height);
	const std::uint32_t clear_packed = hyperlite::PackColor({10, 10, 16, 255});

	const SceneConfig configs[] = {
		{48, 96},
		{256, 256},
		{512, 256},
		{512, 512},
		{1024, 512},
		{2048, 512},
		{2048, 1024},
	};

	std::cout << "headless GPU scene sweep (" << width << "x" << height << ")\n";
	std::cout << "complexity,instances,segments,avg_frame_ms,fps,draws_per_s,kernel_ms,d2h_ms\n";

	for (const SceneConfig& cfg : configs) {
		// Warm up so allocation and first-launch costs are excluded.
		for (int frame = 0; frame < warmup_frames; ++frame) {
			const double phase = static_cast<double>(frame) * 0.013;
			backend->ClearDevice(clear_packed);
			backend->SpiroSceneDevice(width, height, cfg.instances, cfg.segments, phase, 0.016);
			backend->ReadbackToHost(framebuffer);
		}

		long long total_draws = 0;
		const auto t0 = std::chrono::high_resolution_clock::now();
		for (int frame = 0; frame < frames_per_config; ++frame) {
			const double phase = static_cast<double>(frame) * 0.019;
			backend->ClearDevice(clear_packed);
			total_draws += backend->SpiroSceneDevice(width, height, cfg.instances, cfg.segments, phase, 0.016);
			backend->ReadbackToHost(framebuffer);
		}
		const auto t1 = std::chrono::high_resolution_clock::now();

		const double ms = ElapsedMs(t0, t1);
		const double avg_frame_ms = ms / static_cast<double>(frames_per_config);
		const double fps = 1000.0 / avg_frame_ms;
		const double draws_per_s = static_cast<double>(total_draws) / (ms / 1000.0);
		const hyperlite::GpuTimings timings = backend->LastTimings();
		const long long complexity = static_cast<long long>(cfg.instances) * static_cast<long long>(cfg.segments);

		std::cout << complexity << ','
				  << cfg.instances << ','
				  << cfg.segments << ','
				  << avg_frame_ms << ','
				  << fps << ','
				  << draws_per_s << ','
				  << timings.kernel_ms << ','
				  << timings.readback_ms << '\n';
	}

	return 0;
}
