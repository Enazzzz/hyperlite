#include "engine/iwindow.hpp"

#include "engine/headless_window.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include "engine/win32_window.hpp"
#endif

#ifdef __APPLE__
#if defined(HYPERLITE_HAS_COCOA) && HYPERLITE_HAS_COCOA
#include "engine/macos_window.hpp"
#endif
#endif

#if defined(HYPERLITE_HAS_X11) && HYPERLITE_HAS_X11
#include "engine/x11_window.hpp"
#endif

namespace hyperlite {

namespace {

/**
 * True when the process should prefer a headless present path.
 */
bool EnvRequestsHeadless() {
	const char* headless = std::getenv("HYPERLITE_HEADLESS");
	if (headless != nullptr && headless[0] != '\0' && std::strcmp(headless, "0") != 0) {
		return true;
	}
	const char* present = std::getenv("HYPERLITE_PRESENT");
	if (present != nullptr) {
		if (std::strcmp(present, "headless") == 0) {
			return true;
		}
		if (std::strcmp(present, "window") == 0) {
			return false;
		}
	}
	return false;
}

/**
 * True when HYPERLITE_PRESENT=window was set explicitly.
 */
bool EnvRequestsWindow() {
	const char* present = std::getenv("HYPERLITE_PRESENT");
	return present != nullptr && std::strcmp(present, "window") == 0;
}

/**
 * True when a display server appears available for windowed present.
 */
bool DisplayAvailable() {
#ifdef _WIN32
	return true;
#elif defined(__APPLE__)
	// Aqua is available on a logged-in Mac session, including GitHub-hosted
	// macOS runners. HYPERLITE_HEADLESS is handled before this check.
	return true;
#else
	const char* display = std::getenv("DISPLAY");
	if (display != nullptr && display[0] != '\0') {
		return true;
	}
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	return wayland != nullptr && wayland[0] != '\0';
#endif
}

} // namespace

PresentMode ResolvePresentMode(const PresentMode requested) {
	if (EnvRequestsHeadless() && !EnvRequestsWindow()) {
		return PresentMode::kHeadless;
	}
	if (EnvRequestsWindow()) {
		return PresentMode::kWindow;
	}
	if (requested == PresentMode::kHeadless) {
		return PresentMode::kHeadless;
	}
	if (requested == PresentMode::kWindow) {
		return PresentMode::kWindow;
	}
	// kAuto: headless when no display (typical CI / cloud VM).
	return DisplayAvailable() ? PresentMode::kWindow : PresentMode::kHeadless;
}

std::unique_ptr<IWindow> CreatePlatformWindow(
	const int width,
	const int height,
	std::string title,
	const PresentMode mode) {
	const PresentMode resolved = ResolvePresentMode(mode);
	if (resolved == PresentMode::kHeadless) {
		return std::make_unique<HeadlessWindow>(width, height, std::move(title));
	}

#ifdef _WIN32
	return std::make_unique<Win32Window>(width, height, std::move(title));
#elif defined(HYPERLITE_HAS_COCOA) && HYPERLITE_HAS_COCOA
	try {
		return std::make_unique<MacosWindow>(width, height, std::move(title));
	} catch (const std::exception& ex) {
		std::cerr << "[hyperlite] Cocoa window failed (" << ex.what() << "); falling back to headless.\n";
		return std::make_unique<HeadlessWindow>(width, height, "hyperlite-headless");
	}
#elif defined(HYPERLITE_HAS_X11) && HYPERLITE_HAS_X11
	try {
		return std::make_unique<X11Window>(width, height, std::move(title));
	} catch (const std::exception& ex) {
		std::cerr << "[hyperlite] X11 window failed (" << ex.what() << "); falling back to headless.\n";
		return std::make_unique<HeadlessWindow>(width, height, "hyperlite-headless");
	}
#else
	std::cerr << "[hyperlite] Built without a window backend; using headless present.\n";
	return std::make_unique<HeadlessWindow>(width, height, std::move(title));
#endif
}

} // namespace hyperlite
