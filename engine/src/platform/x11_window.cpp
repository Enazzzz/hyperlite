#if defined(HYPERLITE_HAS_X11) && HYPERLITE_HAS_X11

#include "engine/x11_window.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace hyperlite {

namespace {

Display* AsDisplay(void* ptr) {
	return static_cast<Display*>(ptr);
}

XImage* AsImage(void* ptr) {
	return static_cast<XImage*>(ptr);
}

XShmSegmentInfo* AsShm(void* ptr) {
	return static_cast<XShmSegmentInfo*>(ptr);
}

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
 * Count trailing zero bits in a color mask (channel shift).
 */
int MaskShift(const unsigned long mask) {
	if (mask == 0UL) {
		return 0;
	}
	int shift = 0;
	unsigned long value = mask;
	while ((value & 1UL) == 0UL) {
		value >>= 1UL;
		++shift;
	}
	return shift;
}

} // namespace

X11Window::X11Window(const int width, const int height, std::string title)
	: width_(std::max(1, width)),
	  height_(std::max(1, height)) {
	Display* display = XOpenDisplay(nullptr);
	display_ = display;
	if (display == nullptr) {
		throw std::runtime_error("XOpenDisplay failed (is DISPLAY set?).");
	}

	const int screen = DefaultScreen(display);
	Visual* visual = DefaultVisual(display, screen);
	depth_ = DefaultDepth(display, screen);
	red_mask_ = visual->red_mask;
	green_mask_ = visual->green_mask;
	blue_mask_ = visual->blue_mask;

	window_ = XCreateSimpleWindow(
		display,
		RootWindow(display, screen),
		0,
		0,
		static_cast<unsigned int>(width_),
		static_cast<unsigned int>(height_),
		1,
		BlackPixel(display, screen),
		BlackPixel(display, screen));

	wm_delete_ = XInternAtom(display, "WM_DELETE_WINDOW", False);
	Atom protocols[1] = {static_cast<Atom>(wm_delete_)};
	XSetWMProtocols(display, window_, protocols, 1);
	net_wm_state_ = XInternAtom(display, "_NET_WM_STATE", False);
	net_wm_state_fullscreen_ = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);

	XStoreName(display, window_, title.c_str());
	XSelectInput(
		display,
		window_,
		ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
			PointerMotionMask | StructureNotifyMask | FocusChangeMask);

	XMapWindow(display, window_);
	XFlush(display);
	alive_ = true;

	EnsurePresentImage(width_, height_);
}

X11Window::~X11Window() {
	if (mouse_captured_) {
		SetMouseCaptured(false);
	}
	ReleasePresentImage();
	Display* display = AsDisplay(display_);
	if (display != nullptr) {
		if (window_ != 0) {
			XDestroyWindow(display, window_);
			window_ = 0;
		}
		XCloseDisplay(display);
		display_ = nullptr;
	}
	alive_ = false;
}

void X11Window::EnsurePresentImage(const int width, const int height) {
	XImage* existing = AsImage(image_);
	if (existing != nullptr && width == width_ && height == height_ &&
		convert_buffer_.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U) {
		return;
	}
	ReleasePresentImage();

	Display* display = AsDisplay(display_);
	const int bytes_per_pixel = 4;
	const int bytes_per_line = width * bytes_per_pixel;
	convert_buffer_.assign(static_cast<std::size_t>(bytes_per_line) * static_cast<std::size_t>(height), 0U);

	int major = 0;
	int minor = 0;
	Bool pixmaps = False;
	use_shm_ = XShmQueryExtension(display) == True && XShmQueryVersion(display, &major, &minor, &pixmaps);

	if (use_shm_) {
		auto* shm_info = new XShmSegmentInfo{};
		shm_info_ = shm_info;
		XImage* image = XShmCreateImage(
			display,
			DefaultVisual(display, DefaultScreen(display)),
			static_cast<unsigned int>(depth_),
			ZPixmap,
			nullptr,
			shm_info,
			static_cast<unsigned int>(width),
			static_cast<unsigned int>(height));
		image_ = image;
		if (image == nullptr) {
			use_shm_ = false;
			delete shm_info;
			shm_info_ = nullptr;
		} else {
			shm_info->shmid = shmget(
				IPC_PRIVATE,
				static_cast<size_t>(image->bytes_per_line) * static_cast<size_t>(image->height),
				IPC_CREAT | 0777);
			if (shm_info->shmid < 0) {
				XDestroyImage(image);
				image_ = nullptr;
				delete shm_info;
				shm_info_ = nullptr;
				use_shm_ = false;
			} else {
				shm_info->shmaddr = static_cast<char*>(shmat(shm_info->shmid, nullptr, 0));
				image->data = shm_info->shmaddr;
				shm_info->readOnly = False;
				if (shm_info->shmaddr == reinterpret_cast<char*>(-1) || !XShmAttach(display, shm_info)) {
					if (shm_info->shmaddr != reinterpret_cast<char*>(-1)) {
						shmdt(shm_info->shmaddr);
					}
					shmctl(shm_info->shmid, IPC_RMID, nullptr);
					XDestroyImage(image);
					image_ = nullptr;
					delete shm_info;
					shm_info_ = nullptr;
					use_shm_ = false;
				} else {
					shmctl(shm_info->shmid, IPC_RMID, nullptr);
				}
			}
		}
	}

	if (!use_shm_) {
		XImage* image = XCreateImage(
			display,
			DefaultVisual(display, DefaultScreen(display)),
			static_cast<unsigned int>(depth_),
			ZPixmap,
			0,
			reinterpret_cast<char*>(convert_buffer_.data()),
			static_cast<unsigned int>(width),
			static_cast<unsigned int>(height),
			32,
			bytes_per_line);
		image_ = image;
		if (image == nullptr) {
			throw std::runtime_error("XCreateImage failed.");
		}
		image->data = reinterpret_cast<char*>(convert_buffer_.data());
	}
}

void X11Window::ReleasePresentImage() {
	Display* display = AsDisplay(display_);
	XImage* image = AsImage(image_);
	XShmSegmentInfo* shm_info = AsShm(shm_info_);
	if (use_shm_ && shm_info != nullptr && display != nullptr) {
		XShmDetach(display, shm_info);
		if (shm_info->shmaddr != nullptr && shm_info->shmaddr != reinterpret_cast<char*>(-1)) {
			shmdt(shm_info->shmaddr);
		}
		delete shm_info;
		shm_info_ = nullptr;
		if (image != nullptr) {
			image->data = nullptr;
			XDestroyImage(image);
			image_ = nullptr;
		}
		use_shm_ = false;
	} else if (image != nullptr) {
		image->data = nullptr;
		XDestroyImage(image);
		image_ = nullptr;
	}
	convert_buffer_.clear();
}

void X11Window::ConvertRgbaToNative(const std::uint8_t* src, const int width, const int height) {
	XImage* image = AsImage(image_);
	if (src == nullptr || image == nullptr) {
		return;
	}
	const int r_shift = MaskShift(red_mask_);
	const int g_shift = MaskShift(green_mask_);
	const int b_shift = MaskShift(blue_mask_);
	char* dst_base = use_shm_ ? image->data : reinterpret_cast<char*>(convert_buffer_.data());
	const int stride = image->bytes_per_line;

	for (int y = 0; y < height; ++y) {
		auto* dst = reinterpret_cast<std::uint32_t*>(dst_base + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride));
		const std::uint8_t* row = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4U;
		for (int x = 0; x < width; ++x) {
			const std::uint8_t r = row[x * 4 + 0];
			const std::uint8_t g = row[x * 4 + 1];
			const std::uint8_t b = row[x * 4 + 2];
			dst[x] = static_cast<std::uint32_t>(
				(static_cast<unsigned long>(r) << r_shift) |
				(static_cast<unsigned long>(g) << g_shift) |
				(static_cast<unsigned long>(b) << b_shift));
		}
	}
	if (!use_shm_) {
		image->data = reinterpret_cast<char*>(convert_buffer_.data());
	}
}

int X11Window::MapKeysymToVk(const unsigned long keysym) {
	switch (keysym) {
	case XK_Escape:
		return 0x1B;
	case XK_Tab:
		return 0x09;
	case XK_Return:
	case XK_KP_Enter:
		return 0x0D;
	case XK_F11:
		return 0x7A;
	case XK_Left:
		return 0x25;
	case XK_Up:
		return 0x26;
	case XK_Right:
		return 0x27;
	case XK_Down:
		return 0x28;
	case XK_Shift_L:
	case XK_Shift_R:
		return 0x10;
	case XK_Control_L:
	case XK_Control_R:
		return 0x11;
	case XK_Alt_L:
	case XK_Alt_R:
	case XK_Meta_L:
	case XK_Meta_R:
		return 0x12;
	case XK_space:
		return 0x20;
	default:
		break;
	}
	if (keysym >= XK_a && keysym <= XK_z) {
		return static_cast<int>(keysym - XK_a + 0x41);
	}
	if (keysym >= XK_A && keysym <= XK_Z) {
		return static_cast<int>(keysym - XK_A + 0x41);
	}
	if (keysym >= XK_0 && keysym <= XK_9) {
		return static_cast<int>(keysym - XK_0 + 0x30);
	}
	if (keysym <= 0xFFUL) {
		return static_cast<int>(keysym & 0xFFUL);
	}
	return -1;
}

void X11Window::PollEvents(InputState& input_state) {
	input_state_ptr_ = &input_state;
	input_state.mouse_delta = {0, 0};
	input_state.mouse_captured = mouse_captured_;

	Display* display = AsDisplay(display_);
	while (display != nullptr && XPending(display) > 0) {
		XEvent event{};
		XNextEvent(display, &event);
		switch (event.type) {
		case ClientMessage:
			if (static_cast<Atom>(event.xclient.data.l[0]) == wm_delete_) {
				input_state.quit_requested = true;
				alive_ = false;
			}
			break;
		case DestroyNotify:
			alive_ = false;
			input_state.quit_requested = true;
			break;
		case ConfigureNotify: {
			const int new_w = std::max(1, event.xconfigure.width);
			const int new_h = std::max(1, event.xconfigure.height);
			if (new_w != width_ || new_h != height_) {
				width_ = new_w;
				height_ = new_h;
				pending_width_ = new_w;
				pending_height_ = new_h;
				resize_pending_ = true;
				EnsurePresentImage(width_, height_);
			}
			break;
		}
		case KeyPress:
		case KeyRelease: {
			const KeySym keysym = XLookupKeysym(&event.xkey, 0);
			const int vk = MapKeysymToVk(static_cast<unsigned long>(keysym));
			if (vk >= 0 && vk < static_cast<int>(input_state.key_down.size())) {
				input_state.key_down[static_cast<std::size_t>(vk)] = (event.type == KeyPress);
			}
			break;
		}
		case ButtonPress:
		case ButtonRelease: {
			const bool down = event.type == ButtonPress;
			switch (event.xbutton.button) {
			case Button1:
				SetMouseButton(&input_state, MouseButton::Left, down);
				break;
			case Button2:
				SetMouseButton(&input_state, MouseButton::Middle, down);
				break;
			case Button3:
				SetMouseButton(&input_state, MouseButton::Right, down);
				break;
			case 8:
				SetMouseButton(&input_state, MouseButton::X1, down);
				break;
			case 9:
				SetMouseButton(&input_state, MouseButton::X2, down);
				break;
			default:
				break;
			}
			input_state.mouse_pos = {event.xbutton.x, event.xbutton.y};
			break;
		}
		case MotionNotify: {
			const int x = event.xmotion.x;
			const int y = event.xmotion.y;
			if (mouse_captured_) {
				const int cx = width_ / 2;
				const int cy = height_ / 2;
				input_state.mouse_delta.x += x - cx;
				input_state.mouse_delta.y += y - cy;
				if (x != cx || y != cy) {
					XWarpPointer(display, None, window_, 0, 0, 0, 0, cx, cy);
					XFlush(display);
				}
			} else {
				input_state.mouse_delta.x += x - input_state.mouse_pos.x;
				input_state.mouse_delta.y += y - input_state.mouse_pos.y;
			}
			input_state.mouse_pos = {x, y};
			break;
		}
		default:
			break;
		}
	}
}

void X11Window::Present(const FrameBuffer& framebuffer) {
	PresentRaw(framebuffer.Data(), framebuffer.Width(), framebuffer.Height());
}

void X11Window::PresentRaw(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	Display* display = AsDisplay(display_);
	XImage* image = AsImage(image_);
	if (!alive_ || display == nullptr || pixels == nullptr || frame_width <= 0 || frame_height <= 0) {
		return;
	}
	EnsurePresentImage(frame_width, frame_height);
	image = AsImage(image_);
	ConvertRgbaToNative(pixels, frame_width, frame_height);

	if (use_shm_) {
		XShmPutImage(
			display,
			window_,
			DefaultGC(display, DefaultScreen(display)),
			image,
			0,
			0,
			0,
			0,
			static_cast<unsigned int>(frame_width),
			static_cast<unsigned int>(frame_height),
			False);
	} else {
		XPutImage(
			display,
			window_,
			DefaultGC(display, DefaultScreen(display)),
			image,
			0,
			0,
			0,
			0,
			static_cast<unsigned int>(frame_width),
			static_cast<unsigned int>(frame_height));
	}
	XFlush(display);
	(void)vsync_;
}

void X11Window::PresentRawAsync(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	PresentRaw(pixels, frame_width, frame_height);
}

void X11Window::FlushAsyncPresent() {}

void X11Window::SetAsyncPresent(const bool enabled) {
	(void)enabled;
}

bool X11Window::AsyncPresentEnabled() const {
	return false;
}

void X11Window::PresentRect(
	const FrameBuffer& framebuffer,
	const int x,
	const int y,
	const int width,
	const int height) {
	(void)x;
	(void)y;
	(void)width;
	(void)height;
	Present(framebuffer);
}

bool X11Window::IsAlive() const {
	return alive_;
}

void* X11Window::NativeHandle() const {
	return display_;
}

int X11Window::Width() const {
	return width_;
}

int X11Window::Height() const {
	return height_;
}

void X11Window::SetMouseCaptured(const bool captured) {
	Display* display = AsDisplay(display_);
	if (display == nullptr || window_ == 0 || mouse_captured_ == captured) {
		mouse_captured_ = captured;
		return;
	}
	if (captured) {
		XGrabPointer(
			display,
			window_,
			True,
			PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
			GrabModeAsync,
			GrabModeAsync,
			window_,
			None,
			CurrentTime);
		XWarpPointer(display, None, window_, 0, 0, 0, 0, width_ / 2, height_ / 2);
		XFlush(display);
	} else {
		XUngrabPointer(display, CurrentTime);
		XFlush(display);
	}
	mouse_captured_ = captured;
}

bool X11Window::MouseCaptured() const {
	return mouse_captured_;
}

void X11Window::SetFullscreen(const bool fullscreen) {
	Display* display = AsDisplay(display_);
	if (display == nullptr || window_ == 0 || fullscreen_ == fullscreen) {
		fullscreen_ = fullscreen;
		return;
	}
	XEvent event{};
	event.xclient.type = ClientMessage;
	event.xclient.window = window_;
	event.xclient.message_type = net_wm_state_;
	event.xclient.format = 32;
	event.xclient.data.l[0] = fullscreen ? 1 : 0;
	event.xclient.data.l[1] = static_cast<long>(net_wm_state_fullscreen_);
	event.xclient.data.l[2] = 0;
	event.xclient.data.l[3] = 1;
	XSendEvent(display, DefaultRootWindow(display), False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
	XFlush(display);
	fullscreen_ = fullscreen;
}

bool X11Window::IsFullscreen() const {
	return fullscreen_;
}

void X11Window::SetClientSize(const int width, const int height) {
	Display* display = AsDisplay(display_);
	if (display == nullptr || window_ == 0 || width <= 0 || height <= 0) {
		return;
	}
	XResizeWindow(display, window_, static_cast<unsigned int>(width), static_cast<unsigned int>(height));
	XFlush(display);
	width_ = width;
	height_ = height;
	pending_width_ = width;
	pending_height_ = height;
	resize_pending_ = true;
	EnsurePresentImage(width_, height_);
}

bool X11Window::ConsumeResize(int& width, int& height) {
	if (!resize_pending_) {
		return false;
	}
	width = pending_width_;
	height = pending_height_;
	resize_pending_ = false;
	return true;
}

void X11Window::SetVsync(const bool enabled) {
	vsync_ = enabled;
}

bool X11Window::VsyncEnabled() const {
	return vsync_;
}

} // namespace hyperlite

#endif
