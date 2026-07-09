#include "engine/win32_window.hpp"

#ifdef _WIN32

#include <cstddef>
#include <cstring>
#include <stdexcept>

#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace hyperlite {

namespace {

constexpr wchar_t kWindowClassName[] = L"HyperliteWindowClass";

/**
 * Set one mouse button bit on the active input snapshot.
 */
void SetMouseButton(InputState* input_state, const MouseButton button, const bool down) {
	if (input_state == nullptr) {
		return;
	}
	const std::size_t index = static_cast<std::size_t>(button);
	if (index < input_state->mouse_buttons.size()) {
		input_state->mouse_buttons[index] = down;
	}
}

/**
 * Register one process-local Win32 class for the presenter window.
 */
void EnsureWindowClassRegistered(const HINSTANCE instance) {
	static bool registered = false;
	if (registered) {
		return;
	}

	WNDCLASSW wc{};
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = Win32Window::WndProc;
	wc.hInstance = instance;
	wc.lpszClassName = kWindowClassName;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	if (!RegisterClassW(&wc)) {
		throw std::runtime_error("RegisterClassW failed.");
	}
	registered = true;
}

} // namespace

Win32Window::Win32Window(const int width, const int height, std::string title)
	: instance_(GetModuleHandleW(nullptr)),
	  width_(width),
	  height_(height) {
	EnsureWindowClassRegistered(instance_);

	const std::wstring wide_title(title.begin(), title.end());
	RECT client_rect{0, 0, width, height};
	AdjustWindowRect(&client_rect, WS_OVERLAPPEDWINDOW, FALSE);
	const int window_width = client_rect.right - client_rect.left;
	const int window_height = client_rect.bottom - client_rect.top;

	hwnd_ = CreateWindowExW(
		0,
		kWindowClassName,
		wide_title.c_str(),
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		window_width,
		window_height,
		nullptr,
		nullptr,
		instance_,
		nullptr);

	if (!hwnd_) {
		throw std::runtime_error("CreateWindowExW failed.");
	}
	SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

	RECT actual_client{};
	GetClientRect(hwnd_, &actual_client);
	width_ = actual_client.right - actual_client.left;
	height_ = actual_client.bottom - actual_client.top;
	RebuildBitmapInfo(width_, height_);
}

Win32Window::~Win32Window() {
	StopPresentWorker();
	ReleasePresentationBuffer();
	if (mouse_captured_) {
		SetMouseCaptured(false);
	}
	if (hwnd_) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

void Win32Window::SetCursorVisible(const bool visible) {
	if (visible) {
		while (ShowCursor(TRUE) < 0) {
		}
	} else {
		while (ShowCursor(FALSE) >= 0) {
		}
	}
}

void Win32Window::WarpCursorToCenter() {
	if (!hwnd_) {
		return;
	}
	RECT client{};
	GetClientRect(hwnd_, &client);
	POINT center{
		client.left + (client.right - client.left) / 2,
		client.top + (client.bottom - client.top) / 2};
	ClientToScreen(hwnd_, &center);
	SetCursorPos(center.x, center.y);
}

void Win32Window::SetMouseCaptured(const bool captured) {
	if (!hwnd_ || mouse_captured_ == captured) {
		return;
	}
	mouse_captured_ = captured;
	if (captured) {
		SetCapture(hwnd_);
		SetCursorVisible(false);
		WarpCursorToCenter();
	} else {
		ReleaseCapture();
		ClipCursor(nullptr);
		SetCursorVisible(true);
	}
	if (input_state_ptr_) {
		input_state_ptr_->mouse_captured = captured;
	}
}

void Win32Window::SetFullscreen(const bool fullscreen) {
	if (!hwnd_ || fullscreen_ == fullscreen) {
		return;
	}

	if (fullscreen) {
		saved_style_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
		saved_ex_style_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_EXSTYLE));
		saved_placement_.length = sizeof(WINDOWPLACEMENT);
		GetWindowPlacement(hwnd_, &saved_placement_);

		MONITORINFO monitor_info{};
		monitor_info.cbSize = sizeof(MONITORINFO);
		GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor_info);

		SetWindowLongPtr(
			hwnd_,
			GWL_STYLE,
			static_cast<LONG_PTR>(saved_style_ & ~(WS_CAPTION | WS_THICKFRAME)));
		SetWindowLongPtr(
			hwnd_,
			GWL_EXSTYLE,
			static_cast<LONG_PTR>(
				saved_ex_style_ &
				~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)));

		SetWindowPos(
			hwnd_,
			HWND_TOP,
			monitor_info.rcMonitor.left,
			monitor_info.rcMonitor.top,
			monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
			monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
		fullscreen_ = true;
	} else {
		SetWindowLongPtr(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(saved_style_));
		SetWindowLongPtr(hwnd_, GWL_EXSTYLE, static_cast<LONG_PTR>(saved_ex_style_));
		SetWindowPlacement(hwnd_, &saved_placement_);
		SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		fullscreen_ = false;
	}

	RECT client{};
	GetClientRect(hwnd_, &client);
	NoteClientSize(client.right - client.left, client.bottom - client.top);
	if (mouse_captured_) {
		WarpCursorToCenter();
	}
}

void Win32Window::SetClientSize(const int width, const int height) {
	if (!hwnd_ || width <= 0 || height <= 0 || fullscreen_) {
		return;
	}

	RECT window_rect{};
	GetWindowRect(hwnd_, &window_rect);
	RECT client_rect{0, 0, width, height};
	const DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
	const BOOL has_menu = GetMenu(hwnd_) != nullptr;
	AdjustWindowRect(&client_rect, style, has_menu);
	const int window_width = client_rect.right - client_rect.left;
	const int window_height = client_rect.bottom - client_rect.top;
	SetWindowPos(hwnd_, nullptr, window_rect.left, window_rect.top, window_width, window_height, SWP_NOZORDER);

	RECT actual_client{};
	GetClientRect(hwnd_, &actual_client);
	NoteClientSize(actual_client.right - actual_client.left, actual_client.bottom - actual_client.top);
}

void Win32Window::NoteClientSize(const int width, const int height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	if (width_ == width && height_ == height) {
		return;
	}
	width_ = width;
	height_ = height;
	resize_pending_ = true;
	RebuildBitmapInfo(width_, height_);
}

bool Win32Window::ConsumeResize(int& width, int& height) {
	if (!resize_pending_) {
		return false;
	}
	width = width_;
	height = height_;
	resize_pending_ = false;
	return true;
}

void Win32Window::SetVsync(const bool enabled) {
	vsync_ = enabled;
}

void Win32Window::PollEvents(InputState& input_state) {
	input_state_ptr_ = &input_state;
	input_state.mouse_delta = {0, 0};
	input_state.mouse_captured = mouse_captured_;

	MSG msg{};
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			input_state.quit_requested = true;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	POINT point{};
	GetCursorPos(&point);
	ScreenToClient(hwnd_, &point);

	if (mouse_captured_) {
		RECT client{};
		GetClientRect(hwnd_, &client);
		const int center_x = (client.left + client.right) / 2;
		const int center_y = (client.top + client.bottom) / 2;
		input_state.mouse_delta = {point.x - center_x, point.y - center_y};
		input_state.mouse_pos = {center_x, center_y};
		WarpCursorToCenter();
	} else {
		input_state.mouse_pos = {point.x, point.y};
	}

	input_state_ptr_ = nullptr;
}

void Win32Window::Present(const FrameBuffer& framebuffer) {
	PresentRaw(framebuffer.Data(), framebuffer.Width(), framebuffer.Height());
}

void Win32Window::PresentRaw(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	if (pixels == nullptr || frame_width <= 0 || frame_height <= 0) {
		return;
	}
	EnsurePresentationBuffer(frame_width, frame_height);
	if (dib_bits_ == nullptr) {
		return;
	}

	const std::size_t bytes =
		static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 4U;
	std::memcpy(dib_bits_, pixels, bytes);

	HDC hdc = GetDC(hwnd_);
	if (frame_width == width_ && frame_height == height_) {
		BitBlt(hdc, 0, 0, frame_width, frame_height, mem_dc_, 0, 0, SRCCOPY);
	} else {
		StretchBlt(
			hdc,
			0,
			0,
			width_,
			height_,
			mem_dc_,
			0,
			0,
			frame_width,
			frame_height,
			SRCCOPY);
	}
	ReleaseDC(hwnd_, hdc);
	if (vsync_) {
		DwmFlush();
	}
}

void Win32Window::SetAsyncPresent(const bool enabled) {
	if (async_present_ == enabled) {
		return;
	}
	if (!enabled && async_present_) {
		StopPresentWorker();
	}
	async_present_ = enabled;
	if (enabled) {
		StartPresentWorker();
	}
}

void Win32Window::StartPresentWorker() {
	if (present_thread_.joinable()) {
		return;
	}
	present_shutdown_ = false;
	present_worker_idle_ = true;
	present_job_ready_ = false;
	present_thread_ = std::thread(&Win32Window::PresentWorkerLoop, this);
}

void Win32Window::StopPresentWorker() {
	if (!present_thread_.joinable()) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(present_mutex_);
		present_shutdown_ = true;
	}
	present_cv_.notify_all();
	present_thread_.join();
	present_shutdown_ = false;
	present_job_ready_ = false;
	present_worker_idle_ = true;
}

void Win32Window::FlushAsyncPresent() {
	if (!async_present_) {
		return;
	}
	std::unique_lock<std::mutex> lock(present_mutex_);
	present_cv_.wait(lock, [this] { return present_worker_idle_ && !present_job_ready_; });
}

void Win32Window::PresentRawAsync(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	if (pixels == nullptr || frame_width <= 0 || frame_height <= 0) {
		return;
	}
	if (!async_present_) {
		PresentRaw(pixels, frame_width, frame_height);
		return;
	}

	const std::size_t bytes =
		static_cast<std::size_t>(frame_width) * static_cast<std::size_t>(frame_height) * 4U;
	std::unique_lock<std::mutex> lock(present_mutex_);
	present_cv_.wait(lock, [this] { return present_worker_idle_; });

	if (staging_bytes_ < bytes) {
		staging_[0].resize(bytes);
		staging_[1].resize(bytes);
		staging_bytes_ = bytes;
	}

	const int slot = staging_submit_index_;
	staging_submit_index_ ^= 1;
	std::memcpy(staging_[slot].data(), pixels, bytes);

	pending_pixels_ = staging_[slot].data();
	pending_width_ = frame_width;
	pending_height_ = frame_height;
	present_worker_idle_ = false;
	present_job_ready_ = true;
	lock.unlock();
	present_cv_.notify_one();
}

void Win32Window::PresentWorkerLoop() {
	for (;;) {
		std::unique_lock<std::mutex> lock(present_mutex_);
		present_cv_.wait(lock, [this] { return present_shutdown_ || present_job_ready_; });
		if (present_shutdown_ && !present_job_ready_) {
			break;
		}

		const std::uint8_t* pixels = pending_pixels_;
		const int frame_width = pending_width_;
		const int frame_height = pending_height_;
		present_job_ready_ = false;
		lock.unlock();

		PresentRaw(pixels, frame_width, frame_height);

		lock.lock();
		present_worker_idle_ = true;
		present_cv_.notify_all();
	}
}

void Win32Window::PresentRect(const FrameBuffer& framebuffer, const int x, const int y, const int width, const int height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	const int frame_width = framebuffer.Width();
	const int frame_height = framebuffer.Height();
	EnsurePresentationBuffer(frame_width, frame_height);
	if (dib_bits_ == nullptr) {
		return;
	}

	const int row_bytes = width * 4;
	const int src_stride = frame_width * 4;
	const std::uint8_t* src = framebuffer.Data();
	auto* dst = static_cast<std::uint8_t*>(dib_bits_);
	for (int row = 0; row < height; ++row) {
		const int row_y = y + row;
		std::memcpy(
			dst + static_cast<std::size_t>(row_y) * static_cast<std::size_t>(src_stride) + static_cast<std::size_t>(x) * 4U,
			src + static_cast<std::size_t>(row_y) * static_cast<std::size_t>(src_stride) + static_cast<std::size_t>(x) * 4U,
			static_cast<std::size_t>(row_bytes));
	}

	HDC hdc = GetDC(hwnd_);
	BitBlt(hdc, x, y, width, height, mem_dc_, x, y, SRCCOPY);
	ReleaseDC(hwnd_, hdc);
	if (vsync_) {
		DwmFlush();
	}
}

bool Win32Window::IsAlive() const {
	return hwnd_ != nullptr && IsWindow(hwnd_) != 0;
}

LRESULT CALLBACK Win32Window::WndProc(HWND hwnd, const UINT message, const WPARAM w_param, const LPARAM l_param) {
	auto* self = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	switch (message) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (self && self->input_state_ptr_) {
			const std::size_t key = static_cast<std::size_t>(w_param & 0xFF);
			self->input_state_ptr_->key_down[key] = true;
		}
		return 0;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (self && self->input_state_ptr_) {
			const std::size_t key = static_cast<std::size_t>(w_param & 0xFF);
			self->input_state_ptr_->key_down[key] = false;
		}
		return 0;
	case WM_LBUTTONDOWN:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Left, true);
		return 0;
	case WM_LBUTTONUP:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Left, false);
		return 0;
	case WM_RBUTTONDOWN:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Right, true);
		return 0;
	case WM_RBUTTONUP:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Right, false);
		return 0;
	case WM_MBUTTONDOWN:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Middle, true);
		return 0;
	case WM_MBUTTONUP:
		SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::Middle, false);
		return 0;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP: {
		const bool down = message == WM_XBUTTONDOWN;
		const WORD xbutton = GET_XBUTTON_WPARAM(w_param);
		if (xbutton == XBUTTON1) {
			SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::X1, down);
		} else if (xbutton == XBUTTON2) {
			SetMouseButton(self ? self->input_state_ptr_ : nullptr, MouseButton::X2, down);
		}
		return TRUE;
	}
	case WM_SIZE:
		if (self && w_param != SIZE_MINIMIZED) {
			self->NoteClientSize(LOWORD(l_param), HIWORD(l_param));
			if (self->mouse_captured_) {
				self->WarpCursorToCenter();
			}
		}
		return 0;
	case WM_KILLFOCUS:
		if (self) {
			if (self->mouse_captured_) {
				self->SetMouseCaptured(false);
			}
			if (self->input_state_ptr_) {
				self->input_state_ptr_->mouse_buttons.fill(false);
			}
		}
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
		if (self && self->input_state_ptr_) {
			self->input_state_ptr_->quit_requested = true;
		}
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProcW(hwnd, message, w_param, l_param);
	}
}

void Win32Window::ReleasePresentationBuffer() {
	if (mem_dc_ != nullptr) {
		if (dib_bitmap_ != nullptr) {
			if (hwnd_ != nullptr) {
				HDC screen_dc = GetDC(hwnd_);
				if (screen_dc != nullptr) {
					HBITMAP dummy = CreateCompatibleBitmap(screen_dc, 1, 1);
					if (dummy != nullptr) {
						SelectObject(mem_dc_, dummy);
						DeleteObject(dummy);
					}
					ReleaseDC(hwnd_, screen_dc);
				}
			}
			DeleteObject(dib_bitmap_);
			dib_bitmap_ = nullptr;
		}
		DeleteDC(mem_dc_);
		mem_dc_ = nullptr;
	}
	dib_bits_ = nullptr;
	dib_width_ = 0;
	dib_height_ = 0;
}

void Win32Window::EnsurePresentationBuffer(const int width, const int height) {
	if (width <= 0 || height <= 0 || hwnd_ == nullptr) {
		return;
	}
	if (dib_bits_ != nullptr && dib_width_ == width && dib_height_ == height) {
		return;
	}

	ReleasePresentationBuffer();
	RebuildBitmapInfo(width, height);

	HDC screen_dc = GetDC(hwnd_);
	mem_dc_ = CreateCompatibleDC(screen_dc);
	ReleaseDC(hwnd_, screen_dc);
	if (mem_dc_ == nullptr) {
		return;
	}

	void* bits = nullptr;
	dib_bitmap_ = CreateDIBSection(mem_dc_, &bitmap_info_, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (dib_bitmap_ == nullptr || bits == nullptr) {
		ReleasePresentationBuffer();
		return;
	}
	SelectObject(mem_dc_, dib_bitmap_);
	dib_bits_ = bits;
	dib_width_ = width;
	dib_height_ = height;
}

void Win32Window::RebuildBitmapInfo(const int width, const int height) {
	if (bitmap_info_.bmiHeader.biWidth != width || bitmap_info_.bmiHeader.biHeight != -height) {
		ReleasePresentationBuffer();
	}
	bitmap_info_ = {};
	bitmap_info_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmap_info_.bmiHeader.biWidth = width;
	bitmap_info_.bmiHeader.biHeight = -height;
	bitmap_info_.bmiHeader.biPlanes = 1;
	bitmap_info_.bmiHeader.biBitCount = 32;
	bitmap_info_.bmiHeader.biCompression = BI_RGB;
}

} // namespace hyperlite

#endif
