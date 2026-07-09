#include <cstddef>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <vector>

#include "engine/atlas_store.hpp"
#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"
#include "engine/cpu_blend.hpp"
#include "engine/framebuffer.hpp"
#include "engine/types.hpp"

/**
 * Assert helper for deterministic render tests.
 */
static bool Expect(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		return false;
	}
	return true;
}

/**
 * Validate framebuffer pixel bytes at one coordinate.
 */
static bool ExpectPixel(
	const hyperlite::FrameBuffer& framebuffer,
	const int x,
	const int y,
	const hyperlite::Color expected,
	const char* message) {
	const std::size_t idx = static_cast<std::size_t>((y * framebuffer.Width() + x) * 4);
	const auto* data = framebuffer.Data();
	return Expect(
		data[idx + 0U] == expected.r &&
			data[idx + 1U] == expected.g &&
			data[idx + 2U] == expected.b &&
			data[idx + 3U] == expected.a,
		message);
}

/**
 * Unpack one RGBA channel tuple from a packed pixel.
 */
static hyperlite::Color UnpackColor(const std::uint32_t packed) {
	return {
		static_cast<std::uint8_t>(packed & 0xFFU),
		static_cast<std::uint8_t>((packed >> 8U) & 0xFFU),
		static_cast<std::uint8_t>((packed >> 16U) & 0xFFU),
		static_cast<std::uint8_t>((packed >> 24U) & 0xFFU),
	};
}

/**
 * Validate framebuffer pixel bytes within a small per-channel tolerance.
 */
static bool ExpectPixelApprox(
	const hyperlite::FrameBuffer& framebuffer,
	const int x,
	const int y,
	const hyperlite::Color expected,
	const int tolerance,
	const char* message) {
	const std::size_t idx = static_cast<std::size_t>((y * framebuffer.Width() + x) * 4);
	const auto* data = framebuffer.Data();
	const auto within = [tolerance](const int actual, const int target) {
		return std::abs(actual - target) <= tolerance;
	};
	return Expect(
		within(data[idx + 0U], expected.r) &&
			within(data[idx + 1U], expected.g) &&
			within(data[idx + 2U], expected.b) &&
			within(data[idx + 3U], expected.a),
		message);
}

/**
 * Compare CPU and GPU backend output for same command stream.
 */
int main() {
	hyperlite::CommandBuffer cmd{};
	cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({5, 10, 20, 255})));
	cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kPutPixel, 3, 4, 0, 0, hyperlite::PackColor({255, 0, 0, 255})));
	cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kRectFill, 8, 8, 10, 6, hyperlite::PackColor({0, 255, 0, 255})));
	cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kLine, 0, 0, 31, 31, hyperlite::PackColor({0, 0, 255, 255})));
	cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kRectOutline, 20, 20, 8, 8, hyperlite::PackColor({255, 255, 0, 255})));

	hyperlite::FrameBuffer cpu_fb(64, 64);
	hyperlite::FrameBuffer gpu_fb(64, 64);
	hyperlite::AtlasStore atlases{};

	auto cpu_backend = hyperlite::CreateBackend(hyperlite::BackendKind::kCpu);
	auto gpu_backend = hyperlite::CreateBackend(hyperlite::BackendKind::kGpu);

	cpu_backend->Render(cmd, cpu_fb, atlases);
	cpu_backend->ReadbackToHost(cpu_fb);
	gpu_backend->Render(cmd, gpu_fb, atlases);
	gpu_backend->ReadbackToHost(gpu_fb);

	bool ok = true;
	ok &= Expect(cpu_fb.SizeBytes() == gpu_fb.SizeBytes(), "Framebuffer sizes should match.");
	ok &= ExpectPixel(cpu_fb, 3, 4, {255, 0, 0, 255}, "CPU pixel mismatch.");
	ok &= ExpectPixel(gpu_fb, 3, 4, {255, 0, 0, 255}, "GPU pixel mismatch.");

	for (std::size_t i = 0; i < cpu_fb.SizeBytes(); ++i) {
		if (cpu_fb.Data()[i] != gpu_fb.Data()[i]) {
			ok = false;
			std::cerr << "FAIL: CPU and GPU framebuffers diverged at byte " << i << '\n';
			break;
		}
	}

	// Validate full-frame upload path parity.
	std::vector<std::uint8_t> upload(cpu_fb.SizeBytes(), 0U);
	for (std::size_t i = 0; i + 3U < upload.size(); i += 4U) {
		upload[i + 0U] = static_cast<std::uint8_t>((i / 4U) & 255U);
		upload[i + 1U] = static_cast<std::uint8_t>(((i / 4U) * 3U) & 255U);
		upload[i + 2U] = static_cast<std::uint8_t>(((i / 4U) * 7U) & 255U);
		upload[i + 3U] = 255U;
	}
	hyperlite::CommandBuffer upload_cmd{};
	upload_cmd.StageUploadFrame(upload.data(), upload.size());
	upload_cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kUploadFrame, 0, 0, 0, 0, 0U));
	cpu_backend->Render(upload_cmd, cpu_fb, atlases);
	gpu_backend->Render(upload_cmd, gpu_fb, atlases);
	gpu_backend->ReadbackToHost(gpu_fb);
	for (std::size_t i = 0; i < cpu_fb.SizeBytes(); ++i) {
		if (cpu_fb.Data()[i] != gpu_fb.Data()[i]) {
			ok = false;
			std::cerr << "FAIL: Upload framebuffer mismatch at byte " << i << '\n';
			break;
		}
	}

	// Validate deferred blit path parity.
	const int blit_w = 11;
	const int blit_h = 9;
	std::vector<std::uint8_t> blit(static_cast<std::size_t>(blit_w) * static_cast<std::size_t>(blit_h) * 4U, 0U);
	for (int y = 0; y < blit_h; ++y) {
		for (int x = 0; x < blit_w; ++x) {
			const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(blit_w) + static_cast<std::size_t>(x)) * 4U;
			blit[idx + 0U] = static_cast<std::uint8_t>((x * 17) & 255);
			blit[idx + 1U] = static_cast<std::uint8_t>((y * 31) & 255);
			blit[idx + 2U] = static_cast<std::uint8_t>(((x + y) * 13) & 255);
			blit[idx + 3U] = 255U;
		}
	}
	hyperlite::CommandBuffer blit_cmd{};
	blit_cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
	const std::uint32_t blit_index = blit_cmd.PushInlineBlit(blit.data(), blit.size(), 5, 7, blit_w, blit_h);
	blit_cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kBlit, 5, 7, blit_w, blit_h, blit_index));
	cpu_backend->Render(blit_cmd, cpu_fb, atlases);
	gpu_backend->Render(blit_cmd, gpu_fb, atlases);
	gpu_backend->ReadbackToHost(gpu_fb);
	for (std::size_t i = 0; i < cpu_fb.SizeBytes(); ++i) {
		if (cpu_fb.Data()[i] != gpu_fb.Data()[i]) {
			ok = false;
			std::cerr << "FAIL: Blit mismatch at byte " << i << '\n';
			break;
		}
	}

	// CPU-only alpha compositing: semi-transparent rect over opaque background.
	{
		const hyperlite::Color background{40, 80, 120, 255};
		const hyperlite::Color overlay{200, 100, 50, 128};
		const std::uint32_t background_packed = hyperlite::PackColor(background);
		const std::uint32_t overlay_packed = hyperlite::PackColor(overlay);
		const hyperlite::Color expected = UnpackColor(hyperlite::raster::BlendOver(background_packed, overlay_packed));

		hyperlite::CommandBuffer alpha_cmd{};
		alpha_cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kClear, 0, 0, 0, 0, background_packed));
		alpha_cmd.Push(hyperlite::MakeDrawCommand(hyperlite::CommandType::kRectFill, 16, 16, 32, 24, overlay_packed));

		hyperlite::FrameBuffer alpha_fb(64, 64);
		cpu_backend->Render(alpha_cmd, alpha_fb, atlases);
		cpu_backend->ReadbackToHost(alpha_fb);

		ok &= ExpectPixelApprox(alpha_fb, 24, 24, expected, 1, "CPU alpha rect fill center mismatch.");
		ok &= ExpectPixel(alpha_fb, 4, 4, background, "CPU alpha rect fill should preserve untouched pixels.");
	}

	if (ok) {
		std::cout << "PASS\n";
		return 0;
	}
	return 1;
}
