#include "engine/win32_window.hpp"

#ifdef _WIN32

#include <cstddef>
#include <stdexcept>

namespace hyperlite {

namespace {

constexpr wchar_t kWindowClassName[] = L"HyperliteWindowClass";

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
	hwnd_ = CreateWindowExW(
		0,
		kWindowClassName,
		wide_title.c_str(),
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width_,
		height_,
		nullptr,
		nullptr,
		instance_,
		nullptr);

	if (!hwnd_) {
		throw std::runtime_error("CreateWindowExW failed.");
	}
	SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	RebuildBitmapInfo(width_, height_);
}

Win32Window::~Win32Window() {
	if (hwnd_) {
		DestroyWindow(hwnd_);
		hwnd_ = nullptr;
	}
}

void Win32Window::PollEvents(InputState& input_state) {
	input_state_ptr_ = &input_state;
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
	input_state.mouse_pos = {point.x, point.y};
	input_state_ptr_ = nullptr;
}

void Win32Window::Present(const FrameBuffer& framebuffer) {
	PresentRaw(framebuffer.Data(), framebuffer.Width(), framebuffer.Height());
}

void Win32Window::PresentRaw(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	if (pixels == nullptr || frame_width <= 0 || frame_height <= 0) {
		return;
	}
	if (bitmap_info_.bmiHeader.biWidth != frame_width || bitmap_info_.bmiHeader.biHeight != -frame_height) {
		RebuildBitmapInfo(frame_width, frame_height);
	}

	HDC hdc = GetDC(hwnd_);
	if (frame_width == width_ && frame_height == height_) {
		// No scaling: SetDIBitsToDevice is faster than StretchDIBits.
		SetDIBitsToDevice(
			hdc,
			0,
			0,
			static_cast<UINT>(frame_width),
			static_cast<UINT>(frame_height),
			0,
			0,
			0,
			static_cast<UINT>(frame_height),
			pixels,
			&bitmap_info_,
			DIB_RGB_COLORS);
	} else {
		StretchDIBits(
			hdc,
			0,
			0,
			width_,
			height_,
			0,
			0,
			frame_width,
			frame_height,
			pixels,
			&bitmap_info_,
			DIB_RGB_COLORS,
			SRCCOPY);
	}
	ReleaseDC(hwnd_, hdc);
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

void Win32Window::RebuildBitmapInfo(const int width, const int height) {
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
