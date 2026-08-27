#pragma once

#if defined(HYPERLITE_HAS_X11) && HYPERLITE_HAS_X11

#include <cstdint>
#include <string>
#include <vector>

#include "engine/iwindow.hpp"

namespace hyperlite {

/**
 * Lightweight X11 window that blits host RGBA8 frames via MIT-SHM (or XPutImage).
 */
class X11Window final : public IWindow {
public:
	/**
	 * Open a mapped top-level X11 window.
	 */
	X11Window(int width, int height, std::string title);

	~X11Window() override;

	X11Window(const X11Window&) = delete;
	X11Window& operator=(const X11Window&) = delete;

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

private:
	/** Allocate or resize the shared/present image buffer. */
	void EnsurePresentImage(int width, int height);

	/** Release shared-memory / XImage resources. */
	void ReleasePresentImage();

	/** Convert host RGBA8 into the X visual's native pixel layout. */
	void ConvertRgbaToNative(const std::uint8_t* src, int width, int height);

	/** Map an X11 keysym onto the Win32 VK codes used by hyperlite.Keys. */
	static int MapKeysymToVk(unsigned long keysym);

	void* display_ = nullptr; // Display*
	unsigned long window_ = 0; // Window
	unsigned long wm_delete_ = 0;
	unsigned long net_wm_state_ = 0;
	unsigned long net_wm_state_fullscreen_ = 0;
	void* image_ = nullptr; // XImage*
	void* shm_info_ = nullptr; // XShmSegmentInfo*
	bool use_shm_ = false;
	bool alive_ = false;
	bool vsync_ = true;
	bool mouse_captured_ = false;
	bool fullscreen_ = false;
	bool resize_pending_ = false;
	int width_ = 0;
	int height_ = 0;
	int pending_width_ = 0;
	int pending_height_ = 0;
	int depth_ = 24;
	unsigned long red_mask_ = 0;
	unsigned long green_mask_ = 0;
	unsigned long blue_mask_ = 0;
	std::vector<std::uint8_t> convert_buffer_{};
	InputState* input_state_ptr_ = nullptr;
};

} // namespace hyperlite

#endif
