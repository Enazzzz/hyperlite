#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/framebuffer.hpp"
#include "engine/input_state.hpp"

namespace hyperlite {

/**
 * How the engine attaches to a display surface.
 *
 * kAuto picks a window when a display is available, otherwise headless.
 * Override with HYPERLITE_PRESENT=headless|window or HYPERLITE_HEADLESS=1.
 */
enum class PresentMode {
	kAuto = 0,
	kHeadless = 1,
	kWindow = 2,
};

/**
 * Platform window + present surface used by Engine.
 *
 * Windows implements this with Win32 + GDI; Linux with X11 or a headless stub.
 * DXGI remains a Windows-only overlay on top of the window native handle.
 */
class IWindow {
public:
	virtual ~IWindow() = default;

	/** Pump pending OS events into the input snapshot. */
	virtual void PollEvents(InputState& input_state) = 0;

	/** Copy a full framebuffer to the surface (no-op when headless). */
	virtual void Present(const FrameBuffer& framebuffer) = 0;

	/** Copy raw RGBA8 pixels to the surface. */
	virtual void PresentRaw(const std::uint8_t* pixels, int frame_width, int frame_height) = 0;

	/** Queue an async present when supported; otherwise present synchronously. */
	virtual void PresentRawAsync(const std::uint8_t* pixels, int frame_width, int frame_height) = 0;

	/** Block until any queued async present finishes. */
	virtual void FlushAsyncPresent() = 0;

	/** Enable a background present worker when the platform supports it. */
	virtual void SetAsyncPresent(bool enabled) = 0;

	/** Whether async present is active. */
	virtual bool AsyncPresentEnabled() const = 0;

	/** Copy one dirty sub-rectangle to the surface. */
	virtual void PresentRect(const FrameBuffer& framebuffer, int x, int y, int width, int height) = 0;

	/** True while the window (or headless session) is still valid. */
	virtual bool IsAlive() const = 0;

	/** Native OS handle (HWND / Display*+Window), or nullptr when headless. */
	virtual void* NativeHandle() const = 0;

	/** Client width in pixels. */
	virtual int Width() const = 0;

	/** Client height in pixels. */
	virtual int Height() const = 0;

	/** Capture mouse for relative movement (may stub on some platforms). */
	virtual void SetMouseCaptured(bool captured) = 0;

	/** Whether the cursor is currently captured. */
	virtual bool MouseCaptured() const = 0;

	/** Toggle borderless fullscreen when supported. */
	virtual void SetFullscreen(bool fullscreen) = 0;

	/** Whether borderless fullscreen is active. */
	virtual bool IsFullscreen() const = 0;

	/** Resize the client area to the requested pixel size. */
	virtual void SetClientSize(int width, int height) = 0;

	/** Consume a pending client-area resize (false when unchanged). */
	virtual bool ConsumeResize(int& width, int& height) = 0;

	/** Best-effort vertical sync; must never crash when unsupported. */
	virtual void SetVsync(bool enabled) = 0;

	/** Whether vsync is requested. */
	virtual bool VsyncEnabled() const = 0;
};

/**
 * Resolve PresentMode::kAuto using env vars and display availability.
 */
PresentMode ResolvePresentMode(PresentMode requested);

/**
 * Create the platform window for the resolved present mode.
 *
 * Falls back to headless when a real window cannot be opened.
 */
std::unique_ptr<IWindow> CreatePlatformWindow(int width, int height, std::string title, PresentMode mode);

} // namespace hyperlite
