#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"

namespace {

/**
 * FNV-1a hash over the host framebuffer for a stable smoke fingerprint.
 */
std::uint64_t HashFramebuffer(const std::uint8_t* data, const std::size_t bytes) {
	std::uint64_t hash = 14695981039346656037ULL;
	for (std::size_t i = 0; i < bytes; ++i) {
		hash ^= data[i];
		hash *= 1099511628211ULL;
	}
	return hash;
}

/**
 * Best-effort RSS in kilobytes from /proc/self/status (Linux); 0 elsewhere.
 */
long ReadRssKb() {
	FILE* file = std::fopen("/proc/self/status", "r");
	if (file == nullptr) {
		return 0;
	}
	char line[256];
	long rss_kb = 0;
	while (std::fgets(line, sizeof(line), file) != nullptr) {
		if (std::strncmp(line, "VmRSS:", 6) == 0) {
			rss_kb = std::strtol(line + 6, nullptr, 10);
			break;
		}
	}
	std::fclose(file);
	return rss_kb;
}

} // namespace

/**
 * Headless Engine smoke: clear, rect, line, present, hash a few pixels.
 */
int main() {
	using clock = std::chrono::steady_clock;

	const auto t0 = clock::now();
	hyperlite::Engine engine(
		1280,
		720,
		hyperlite::BackendKind::kCpu,
		"Hyperlite Headless Smoke",
		hyperlite::PresentMode::kHeadless);
	engine.SetVsync(false);
	const auto t1 = clock::now();
	const double startup_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
	const long rss_after_init = ReadRssKb();

	engine.BeginFrame();
	engine.PushCommand(hyperlite::MakeDrawCommand(
		hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({10, 20, 30, 255})));
	engine.PushCommand(hyperlite::MakeDrawCommand(
		hyperlite::CommandType::kRectFill, 100, 100, 64, 48, hyperlite::PackColor({255, 80, 80, 255})));
	engine.PushCommand(hyperlite::MakeDrawCommand(
		hyperlite::CommandType::kLine, 0, 0, 1279, 719, hyperlite::PackColor({0, 255, 120, 255})));
	engine.EndFrame();
	engine.Present();

	const std::uint8_t* pixels = engine.FramebufferPtr();
	const std::size_t bytes = engine.FramebufferBytes();
	if (pixels == nullptr || bytes < 4U) {
		std::cerr << "FAIL: missing framebuffer\n";
		return 1;
	}

	// Sample clear color at (0,1) — (0,0) is on the diagonal line.
	const std::size_t clear_idx = static_cast<std::size_t>((1 * 1280 + 0) * 4);
	if (pixels[clear_idx + 0] != 10 || pixels[clear_idx + 1] != 20 || pixels[clear_idx + 2] != 30) {
		std::cerr << "FAIL: clear pixel mismatch\n";
		return 1;
	}
	const std::size_t rect_idx = static_cast<std::size_t>((100 * 1280 + 100) * 4);
	if (pixels[rect_idx + 0] != 255 || pixels[rect_idx + 1] != 80 || pixels[rect_idx + 2] != 80) {
		std::cerr << "FAIL: rect pixel mismatch\n";
		return 1;
	}

	const std::uint64_t hash = HashFramebuffer(pixels, bytes);
	std::cout << "headless_smoke=ok\n";
	std::cout << "startup_ms=" << startup_ms << '\n';
	std::cout << "rss_kb_after_init=" << rss_after_init << '\n';
	std::cout << "framebuffer_hash=" << hash << '\n';
	std::cout << "is_running=" << (engine.IsRunning() ? 1 : 0) << '\n';

	// 100 fused line frames for a light RSS sample after work.
	std::vector<std::int32_t> segments(400);
	for (std::size_t i = 0; i < 100; ++i) {
		segments[i * 4 + 0] = static_cast<std::int32_t>(i % 1280);
		segments[i * 4 + 1] = 0;
		segments[i * 4 + 2] = static_cast<std::int32_t>((i * 3) % 1280);
		segments[i * 4 + 3] = 719;
	}
	for (int frame = 0; frame < 100; ++frame) {
		engine.TickLines(
			hyperlite::PackColor({8, 12, 20, 255}),
			segments.data(),
			100,
			hyperlite::PackColor({0, 255, 80, 255}),
			1);
	}
	std::cout << "rss_kb_after_100_frames=" << ReadRssKb() << '\n';
	return 0;
}
