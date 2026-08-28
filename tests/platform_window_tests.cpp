#include "engine/iwindow.hpp"
#include "engine/input_state.hpp"

#ifdef __APPLE__
#if defined(HYPERLITE_HAS_COCOA) && HYPERLITE_HAS_COCOA
#include "engine/macos_window.hpp"
#endif
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void Expect(const bool cond, const char* msg) {
	if (!cond) {
		std::fprintf(stderr, "FAIL: %s\n", msg);
		++g_failures;
	}
}

} // namespace

int main() {
	using namespace hyperlite;

	// Force headless so this test is safe on CI (Linux, Windows, and macOS).
#ifdef _WIN32
	_putenv("HYPERLITE_HEADLESS=1");
#else
	setenv("HYPERLITE_HEADLESS", "1", 1);
#endif

	auto window = CreatePlatformWindow(64, 64, "platform-test", PresentMode::kHeadless);
	Expect(window != nullptr, "headless window");
	Expect(window->IsAlive(), "headless alive");
	Expect(window->NativeHandle() == nullptr, "headless native handle");
	Expect(window->Width() == 64 && window->Height() == 64, "headless size");

	InputState input{};
	window->PollEvents(input);
	Expect(!input.quit_requested, "headless no quit");

	std::vector<std::uint8_t> pixels(static_cast<std::size_t>(64 * 64 * 4), 255);
	window->PresentRaw(pixels.data(), 64, 64);
	window->PresentRawAsync(pixels.data(), 64, 64);
	window->FlushAsyncPresent();
	window->SetVsync(false);
	Expect(!window->VsyncEnabled(), "vsync off");
	window->SetMouseCaptured(true);
	window->SetMouseCaptured(false);
	window->SetFullscreen(false);
	int rw = 0;
	int rh = 0;
	(void)window->ConsumeResize(rw, rh);

#if defined(HYPERLITE_HAS_COCOA) && HYPERLITE_HAS_COCOA
	Expect(MacosBackendLinked() == 1, "cocoa translation unit linked");
#endif

	if (g_failures != 0) {
		std::fprintf(stderr, "%d platform window test(s) failed\n", g_failures);
		return 1;
	}
	std::printf("platform_window_tests: ok\n");
	return 0;
}
