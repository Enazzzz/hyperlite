#include <chrono>
#include <cstdio>
#include <vector>

#include "engine/atlas_store.hpp"
#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"

/**
 * Headless GPU blit throughput sweep.
 */
int main() {
	constexpr int width = 960;
	constexpr int height = 540;
	constexpr int sprite = 32;
	const int counts[] = {1, 10, 50, 100, 200, 500, 1000};

	auto backend = hyperlite::CreateBackend(hyperlite::BackendKind::kGpu);
	if (backend->Name() != "gpu") {
		std::printf("SKIP: CUDA backend unavailable.\n");
		return 0;
	}

	hyperlite::FrameBuffer framebuffer(width, height);
	hyperlite::AtlasStore atlases{};
	std::vector<std::uint8_t> sprite_rgba(static_cast<std::size_t>(sprite) * static_cast<std::size_t>(sprite) * 4U, 0U);
	for (int y = 0; y < sprite; ++y) {
		for (int x = 0; x < sprite; ++x) {
			const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite) + static_cast<std::size_t>(x)) * 4U;
			sprite_rgba[idx + 0U] = static_cast<std::uint8_t>((x * 8) & 255);
			sprite_rgba[idx + 1U] = static_cast<std::uint8_t>((y * 8) & 255);
			sprite_rgba[idx + 2U] = 200U;
			sprite_rgba[idx + 3U] = 255U;
		}
	}
	const int atlas = atlases.Load(sprite_rgba.data(), sprite_rgba.size(), sprite, sprite);
	backend->EnsureAtlasResident(atlas, sprite_rgba.data(), sprite_rgba.size(), sprite, sprite);

	std::printf("gpu_blit_bench %dx%d sprite=%d\n", width, height, sprite);
	for (const int count : counts) {
		hyperlite::CommandBuffer cmd{};
		cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({8, 12, 20, 255})));
		for (int i = 0; i < count; ++i) {
			const int dst_x = (i * 17) % (width - sprite);
			const int dst_y = (i * 23) % (height - sprite);
			const std::uint32_t record = cmd.PushAtlasBlit(static_cast<std::uint32_t>(atlas), 0, 0, sprite, sprite, dst_x, dst_y);
			cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kDrawSprite, dst_x, dst_y, sprite, sprite, record));
		}

		const auto start = std::chrono::steady_clock::now();
		constexpr int frames = 120;
		for (int frame = 0; frame < frames; ++frame) {
			backend->Render(cmd, framebuffer, atlases);
			backend->ReadbackToHost(framebuffer);
		}
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const double fps = frames / (ms / 1000.0);
		std::printf("  blits=%4d  fps=%8.1f  frame_ms=%6.3f\n", count, fps, ms / static_cast<double>(frames));
	}
	return 0;
}
