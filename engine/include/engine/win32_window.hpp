#pragma once

#ifdef _WIN32

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
	 * Queue a full-frame present on a worker thread (returns after copy to staging).
	 */
	void PresentRawAsync(const std::uint8_t* pixels, int frame_width, int frame_height);

	/**
	 * Block until any queued async present finishes.
	 */
	void FlushAsyncPresent();

	/**
	 * Enable a background thread for GDI presents (overlaps with next frame raster).
	 */
	void SetAsyncPresent(bool enabled);

	/**
	 * Whether async GDI present is active.
	 */
	bool AsyncPresentEnabled() const { return async_present_; }

	/**
	 * Copy one dirty sub-rectangle from a framebuffer to the window.
	 */
	void PresentRect(const FrameBuffer& framebuffer, int x, int y, int width, int height);

	/**
	 * True when window is still valid.
	 */
	bool IsAlive() const;

	/**
	 * Static window proc bridge.
	 */
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

	/**
	 * Return native window handle when available.
	 */
	void* NativeHandle() const { return hwnd_; }

	/**
	 * Client width in pixels.
	 */
	int Width() const { return width_; }

	/**
	 * Client height in pixels.
	 */
	int Height() const { return height_; }

	/**
	 * Capture mouse for FPS-style relative movement (hides cursor, warps to center).
	 */
	void SetMouseCaptured(bool captured);

	/**
	 * Whether the cursor is currently captured.
	 */
	bool MouseCaptured() const { return mouse_captured_; }

	/**
	 * Toggle borderless fullscreen on the active monitor.
	 */
	void SetFullscreen(bool fullscreen);

	/**
	 * Whether borderless fullscreen is active.
	 */
	bool IsFullscreen() const { return fullscreen_; }

	/**
	 * Resize the window so its client area matches the requested pixel size.
	 */
	void SetClientSize(int width, int height);

	/**
	 * Consume a pending client-area resize (returns false when unchanged).
	 */
	bool ConsumeResize(int& width, int& height);

	/**
	 * Enable DwmFlush after GDI present (vertical sync on desktop compositor).
	 */
	void SetVsync(bool enabled);

	/**
	 * Whether vsync is enabled for GDI presents.
	 */
	bool VsyncEnabled() const { return vsync_; }

private:
	/**
	 * Initialize persistent DIB presentation metadata.
	 */
	void RebuildBitmapInfo(int width, int height);

	/**
	 * Allocate or reuse the off-screen DIB used for GDI presents.
	 */
	void EnsurePresentationBuffer(int width, int height);

	/**
	 * Release the off-screen DIB and compatible DC.
	 */
	void ReleasePresentationBuffer();

	/**
	 * Read client rect into width_/height_ and flag a pending resize.
	 */
	void NoteClientSize(int width, int height);

	/**
	 * Warp the OS cursor to the client-area center (screen coordinates).
	 */
	void WarpCursorToCenter();

	/**
	 * Show or hide the system cursor (tracks display count).
	 */
	void SetCursorVisible(bool visible);

	/**
	 * Start the background present worker thread.
	 */
	void StartPresentWorker();

	/**
	 * Stop the background present worker thread.
	 */
	void StopPresentWorker();

	/**
	 * Background loop that BitBlts queued frames to the window.
	 */
	void PresentWorkerLoop();

	HINSTANCE instance_ = nullptr;
	HWND hwnd_ = nullptr;
	HDC mem_dc_ = nullptr;
	HBITMAP dib_bitmap_ = nullptr;
	void* dib_bits_ = nullptr;
	int dib_width_ = 0;
	int dib_height_ = 0;
	int width_ = 0;
	int height_ = 0;
	BITMAPINFO bitmap_info_{};
	InputState* input_state_ptr_ = nullptr;
	bool mouse_captured_ = false;
	bool fullscreen_ = false;
	bool resize_pending_ = false;
	bool vsync_ = true;
	bool async_present_ = false;
	bool present_shutdown_ = false;
	bool present_job_ready_ = false;
	bool present_worker_idle_ = true;
	int pending_width_ = 0;
	int pending_height_ = 0;
	int staging_submit_index_ = 0;
	std::size_t staging_bytes_ = 0;
	const std::uint8_t* pending_pixels_ = nullptr;
	std::vector<std::uint8_t> staging_[2];
	std::thread present_thread_{};
	std::mutex present_mutex_{};
	std::condition_variable present_cv_{};
	DWORD saved_style_ = 0;
	DWORD saved_ex_style_ = 0;
	WINDOWPLACEMENT saved_placement_{};
};

} // namespace hyperlite

#endif
