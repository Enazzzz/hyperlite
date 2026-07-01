#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>

#include "engine/framebuffer.hpp"
#include "engine/input_state.hpp"

namespace hyperlite {

/**
 * Minimal Win32 presenter and input collector.
 */
class Win32Window {
public:
	/**
	 * Create a visible top-level window.
	 */
	Win32Window(int width, int height, std::string title);

	/**
	 * Destroy window resources.
	 */
	~Win32Window();

	Win32Window(const Win32Window&) = delete;
	Win32Window& operator=(const Win32Window&) = delete;

	/**
	 * Pump pending messages and update input snapshot.
	 */
	void PollEvents(InputState& input_state);

	/**
	 * Copy framebuffer to window client area.
	 */
	void Present(const FrameBuffer& framebuffer);

	/**
	 * Copy raw RGBA8 pixels to the window client area (used by pipelined present).
	 */
	void PresentRaw(const std::uint8_t* pixels, int frame_width, int frame_height);

	/**
	 * True when window is still valid.
	 */
	bool IsAlive() const;

	/**
	 * Static window proc bridge.
	 */
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

private:
	/**
	 * Initialize persistent DIB presentation metadata.
	 */
	void RebuildBitmapInfo(int width, int height);

	HINSTANCE instance_ = nullptr;
	HWND hwnd_ = nullptr;
	int width_ = 0;
	int height_ = 0;
	BITMAPINFO bitmap_info_{};
	InputState* input_state_ptr_ = nullptr;
};

} // namespace hyperlite

#endif
