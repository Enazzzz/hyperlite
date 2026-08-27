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
#include "engine/iwindow.hpp"

namespace hyperlite {

/**
 * Minimal Win32 presenter and input collector.
 */
class Win32Window final : public IWindow {
public:
	/**
	 * Create a visible top-level window.
	 */
	Win32Window(int width, int height, std::string title);

	/**
	 * Destroy window resources.
	 */
	~Win32Window() override;

	Win32Window(const Win32Window&) = delete;
	Win32Window& operator=(const Win32Window&) = delete;

	void PollEvents(InputState& input_state) override;
	void Present(const FrameBuffer& framebuffer) override;
	void PresentRaw(const std::uint8_t* pixels, int frame_width, int frame_height) override;
	void PresentRawAsync(const std::uint8_t* pixels, int frame_width, int frame_height) override;
	void FlushAsyncPresent() override;
	void SetAsyncPresent(bool enabled) override;
	bool AsyncPresentEnabled() const override { return async_present_; }
	void PresentRect(const FrameBuffer& framebuffer, int x, int y, int width, int height) override;
	bool IsAlive() const override;
	void* NativeHandle() const override { return hwnd_; }
	int Width() const override { return width_; }
	int Height() const override { return height_; }
	void SetMouseCaptured(bool captured) override;
	bool MouseCaptured() const override { return mouse_captured_; }
	void SetFullscreen(bool fullscreen) override;
	bool IsFullscreen() const override { return fullscreen_; }
	void SetClientSize(int width, int height) override;
	bool ConsumeResize(int& width, int& height) override;
	void SetVsync(bool enabled) override;
	bool VsyncEnabled() const override { return vsync_; }

	/**
	 * Static window proc bridge.
	 */
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

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
