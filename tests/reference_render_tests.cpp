#include <cstddef>
#include <cstdint>
#include <iostream>

#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"

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
 * Compare CPU and GPU backend output for same command stream.
 */
int main() {
	hyperlite::CommandBuffer cmd{};
	cmd.Push({hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({5, 10, 20, 255})});
	cmd.Push({hyperlite::CommandType::kPutPixel, 3, 4, 0, 0, hyperlite::PackColor({255, 0, 0, 255})});
	cmd.Push({hyperlite::CommandType::kRectFill, 8, 8, 10, 6, hyperlite::PackColor({0, 255, 0, 255})});
	cmd.Push({hyperlite::CommandType::kLine, 0, 0, 31, 31, hyperlite::PackColor({0, 0, 255, 255})});
	cmd.Push({hyperlite::CommandType::kRectOutline, 20, 20, 8, 8, hyperlite::PackColor({255, 255, 0, 255})});

	hyperlite::FrameBuffer cpu_fb(64, 64);
	hyperlite::FrameBuffer gpu_fb(64, 64);

	auto cpu_backend = hyperlite::CreateBackend(hyperlite::BackendKind::kCpu);
	auto gpu_backend = hyperlite::CreateBackend(hyperlite::BackendKind::kGpu);

	cpu_backend->Render(cmd, cpu_fb);
	cpu_backend->ReadbackToHost(cpu_fb);
	gpu_backend->Render(cmd, gpu_fb);
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

	if (ok) {
		std::cout << "PASS\n";
		return 0;
	}
	return 1;
}
