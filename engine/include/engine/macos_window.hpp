#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "engine/iwindow.hpp"

namespace hyperlite {

/**
 * Cocoa / AppKit window that blits host RGBA8 frames into a layer-backed NSView.
 *
 * Hyperlite still owns every pixel: this class never rasterizes with Metal or
 * OpenGL. The framebuffer is converted to BGRA and assigned to CALayer.contents
 * (nearest-neighbor, stretched to the view — including Retina backing stores).
 *
 * Compiled only on Apple (see CMakeLists.txt). AppKit calls must run on the
 * main thread.
 */
class MacosWindow final : public IWindow {
public:
	/**
	 * Create an NSApplication (if needed) and a titled NSWindow.
	 *
	 * Throws std::runtime_error when Cocoa cannot attach a surface so the
	 * platform factory can fall back to headless.
	 */
	MacosWindow(int width, int height, std::string title);

	~MacosWindow() override;

	MacosWindow(const MacosWindow&) = delete;
	MacosWindow& operator=(const MacosWindow&) = delete;

	void PollEvents(InputState& input_state) override;
	void Present(const FrameBuffer& framebuffer) override;
	void PresentRaw(const std::uint8_t* pixels, int frame_width, int frame_height) override;
	void PresentRawAsync(const std::uint8_t* pixels, int frame_width, int frame_height) override;
	void FlushAsyncPresent() override;
	void SetAsyncPresent(bool enabled) override;
	bool AsyncPresentEnabled() const override;
	void PresentRect(const FrameBuffer& framebuffer, int x, int y, int width, int height) override;
	bool IsAlive() const override;
	void* NativeHandle() const override;
	int Width() const override;
	int Height() const override;
	void SetMouseCaptured(bool captured) override;
	bool MouseCaptured() const override;
	void SetFullscreen(bool fullscreen) override;
	bool IsFullscreen() const override;
	void SetClientSize(int width, int height) override;
	bool ConsumeResize(int& width, int& height) override;
	void SetVsync(bool enabled) override;
	bool VsyncEnabled() const override;

	struct Impl;

private:
	/** Map a Mac virtual key code onto the Win32 VK indices used by Keys. */
	static int MapKeyCodeToVk(unsigned short key_code);

	/** Convert host RGBA8 into the persistent BGRA present buffer. */
	void ConvertRgbaToBgra(const std::uint8_t* src, int width, int height);

	/** Push the BGRA buffer into the view layer (main thread). */
	void UploadLayer();

	/** Wait for the next CVDisplayLink tick when vsync is enabled. */
	void WaitVsync();

	std::unique_ptr<Impl> impl_;
};

/**
 * Link-time probe used by tests: 1 when the Cocoa translation unit is linked.
 */
int MacosBackendLinked();

} // namespace hyperlite
