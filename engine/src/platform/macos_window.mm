#include "engine/macos_window.hpp"

#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#import <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace hyperlite {

struct MacosWindow::Impl {
	NSWindow* window = nil;
	NSView* view = nil;
	id delegate = nil;
	CVDisplayLinkRef display_link = nullptr;
	std::mutex vsync_mu{};
	std::condition_variable vsync_cv{};
	bool vsync_pulse = false;
	bool vsync = true;
	bool alive = true;
	bool mouse_captured = false;
	bool fullscreen = false;
	bool resize_pending = false;
	bool close_requested = false;
	int width = 1;
	int height = 1;
	int pending_width = 1;
	int pending_height = 1;
	int last_mouse_x = 0;
	int last_mouse_y = 0;
	std::vector<std::uint8_t> bgra{};
	NSRect windowed_frame{};
};

} // namespace hyperlite

@interface HyperliteContentView : NSView
@end

@implementation HyperliteContentView
- (BOOL)acceptsFirstResponder {
	return YES;
}
- (BOOL)isOpaque {
	return YES;
}
- (BOOL)wantsLayer {
	return YES;
}
- (void)viewDidChangeBackingProperties {
	[super viewDidChangeBackingProperties];
	self.layer.contentsScale = 1.0;
	self.layer.magnificationFilter = kCAFilterNearest;
	self.layer.minificationFilter = kCAFilterNearest;
	self.layer.contentsGravity = kCAGravityResize;
}
@end

@interface HyperliteWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) hyperlite::MacosWindow::Impl* impl;
@end

@implementation HyperliteWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
	(void)sender;
	if (self.impl != nullptr) {
		self.impl->alive = false;
		self.impl->close_requested = true;
	}
	return NO;
}
- (void)windowDidResize:(NSNotification*)notification {
	(void)notification;
	if (self.impl == nullptr || self.impl->view == nil) {
		return;
	}
	const NSRect bounds = self.impl->view.bounds;
	const int new_w = std::max(1, static_cast<int>(bounds.size.width));
	const int new_h = std::max(1, static_cast<int>(bounds.size.height));
	if (new_w != self.impl->width || new_h != self.impl->height) {
		self.impl->width = new_w;
		self.impl->height = new_h;
		self.impl->pending_width = new_w;
		self.impl->pending_height = new_h;
		self.impl->resize_pending = true;
	}
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
	(void)notification;
	if (self.impl != nullptr) {
		self.impl->fullscreen = true;
	}
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
	(void)notification;
	if (self.impl != nullptr) {
		self.impl->fullscreen = false;
	}
}
@end

namespace hyperlite {
namespace {

/**
 * CVDisplayLink pulse: wake Present() waiters on the next refresh.
 */
CVReturn DisplayLinkCallback(
	CVDisplayLinkRef,
	const CVTimeStamp*,
	const CVTimeStamp*,
	CVOptionFlags,
	CVOptionFlags*,
	void* context) {
	auto* impl = static_cast<MacosWindow::Impl*>(context);
	if (impl == nullptr) {
		return kCVReturnSuccess;
	}
	{
		std::lock_guard<std::mutex> lock(impl->vsync_mu);
		impl->vsync_pulse = true;
	}
	impl->vsync_cv.notify_one();
	return kCVReturnSuccess;
}

/**
 * Set one mouse button on the active snapshot.
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
 * Convert an AppKit window-local point (Y-up) into Hyperlite client pixels (Y-down).
 */
void ClientMouse(const NSView* view, const NSPoint window_point, int& x, int& y) {
	if (view == nil) {
		x = 0;
		y = 0;
		return;
	}
	const NSPoint local = [view convertPoint:window_point fromView:nil];
	const NSRect bounds = view.bounds;
	x = static_cast<int>(local.x);
	y = static_cast<int>(bounds.size.height - local.y);
}

/**
 * Ensure NSApp exists and is a regular GUI process with a minimal menu.
 */
void EnsureNSApplication() {
	NSApplication* app = [NSApplication sharedApplication];
	if (app == nil) {
		throw std::runtime_error("NSApplication sharedApplication returned nil.");
	}
	[app setActivationPolicy:NSApplicationActivationPolicyRegular];
	if (app.mainMenu == nil) {
		NSMenu* menubar = [[NSMenu alloc] init];
		NSMenuItem* app_item = [[NSMenuItem alloc] init];
		[menubar addItem:app_item];
		NSMenu* app_menu = [[NSMenu alloc] init];
		[app_menu addItemWithTitle:@"Quit"
							action:@selector(terminate:)
					 keyEquivalent:@"q"];
		app_item.submenu = app_menu;
		app.mainMenu = menubar;
	}
	[app finishLaunching];
}

} // namespace

int MacosBackendLinked() {
	return 1;
}

int MacosWindow::MapKeyCodeToVk(const unsigned short key_code) {
	// Hardware key codes (ANSI) → Win32 VK used by hyperlite.Keys / X11 map.
	switch (key_code) {
	case 0x00:
		return 0x41; // A
	case 0x01:
		return 0x53; // S
	case 0x02:
		return 0x44; // D
	case 0x03:
		return 0x46; // F
	case 0x04:
		return 0x48; // H
	case 0x05:
		return 0x47; // G
	case 0x06:
		return 0x5A; // Z
	case 0x07:
		return 0x58; // X
	case 0x08:
		return 0x43; // C
	case 0x09:
		return 0x56; // V
	case 0x0B:
		return 0x42; // B
	case 0x0C:
		return 0x51; // Q
	case 0x0D:
		return 0x57; // W
	case 0x0E:
		return 0x45; // E
	case 0x0F:
		return 0x52; // R
	case 0x10:
		return 0x59; // Y
	case 0x11:
		return 0x54; // T
	case 0x12:
		return 0x31; // 1
	case 0x13:
		return 0x32; // 2
	case 0x14:
		return 0x33; // 3
	case 0x15:
		return 0x34; // 4
	case 0x16:
		return 0x36; // 6
	case 0x17:
		return 0x35; // 5
	case 0x18:
		return 0xBB; // OEM_PLUS
	case 0x19:
		return 0x39; // 9
	case 0x1A:
		return 0x37; // 7
	case 0x1B:
		return 0xBD; // OEM_MINUS
	case 0x1C:
		return 0x38; // 8
	case 0x1D:
		return 0x30; // 0
	case 0x1E:
		return 0xDD; // OEM_6 ]
	case 0x1F:
		return 0x4F; // O
	case 0x20:
		return 0x55; // U
	case 0x21:
		return 0xDB; // OEM_4 [
	case 0x22:
		return 0x49; // I
	case 0x23:
		return 0x50; // P
	case 0x24:
		return 0x0D; // Return
	case 0x25:
		return 0x4C; // L
	case 0x26:
		return 0x4A; // J
	case 0x27:
		return 0xDE; // OEM_7 '
	case 0x28:
		return 0x4B; // K
	case 0x29:
		return 0xBA; // OEM_1 ;
	case 0x2A:
		return 0xDC; // OEM_5 backslash
	case 0x2B:
		return 0xBC; // OEM_COMMA
	case 0x2C:
		return 0xBF; // OEM_2 /
	case 0x2D:
		return 0x4E; // N
	case 0x2E:
		return 0x4D; // M
	case 0x2F:
		return 0xBE; // OEM_PERIOD
	case 0x30:
		return 0x09; // Tab
	case 0x31:
		return 0x20; // Space
	case 0x32:
		return 0xC0; // OEM_3 `
	case 0x33:
		return 0x08; // Delete (backspace)
	case 0x35:
		return 0x1B; // Escape
	case 0x36:
		return 0x5C; // Right Command → VK_RWIN
	case 0x37:
		return 0x5B; // Command → VK_LWIN
	case 0x38:
	case 0x3C:
		return 0x10; // Shift
	case 0x39:
		return 0x14; // Caps Lock
	case 0x3A:
	case 0x3D:
		return 0x12; // Option → VK_MENU
	case 0x3B:
	case 0x3E:
		return 0x11; // Control
	case 0x41:
		return 0x6E; // keypad decimal
	case 0x43:
		return 0x6A; // keypad *
	case 0x45:
		return 0x6B; // keypad +
	case 0x4B:
		return 0x6F; // keypad /
	case 0x4C:
		return 0x0D; // keypad enter
	case 0x4E:
		return 0x6D; // keypad -
	case 0x52:
		return 0x60; // keypad 0
	case 0x53:
		return 0x61;
	case 0x54:
		return 0x62;
	case 0x55:
		return 0x63;
	case 0x56:
		return 0x64;
	case 0x57:
		return 0x65;
	case 0x58:
		return 0x66;
	case 0x59:
		return 0x67;
	case 0x5B:
		return 0x68;
	case 0x5C:
		return 0x69; // keypad 9
	case 0x60:
		return 0x74; // F5
	case 0x61:
		return 0x75; // F6
	case 0x62:
		return 0x76; // F7
	case 0x63:
		return 0x72; // F3
	case 0x64:
		return 0x77; // F8
	case 0x65:
		return 0x78; // F9
	case 0x67:
		return 0x7A; // F11
	case 0x6D:
		return 0x79; // F10
	case 0x6F:
		return 0x7B; // F12
	case 0x73:
		return 0x24; // Home
	case 0x74:
		return 0x21; // Page Up
	case 0x75:
		return 0x2E; // Forward Delete
	case 0x76:
		return 0x73; // F4
	case 0x77:
		return 0x23; // End
	case 0x78:
		return 0x71; // F2
	case 0x79:
		return 0x22; // Page Down
	case 0x7A:
		return 0x70; // F1
	case 0x7B:
		return 0x25; // Left
	case 0x7C:
		return 0x27; // Right
	case 0x7D:
		return 0x28; // Down
	case 0x7E:
		return 0x26; // Up
	default:
		return -1;
	}
}

MacosWindow::MacosWindow(const int width, const int height, std::string title)
	: impl_(std::make_unique<Impl>()) {
	@autoreleasepool {
		EnsureNSApplication();
		impl_->width = std::max(1, width);
		impl_->height = std::max(1, height);
		impl_->pending_width = impl_->width;
		impl_->pending_height = impl_->height;

		const NSRect frame = NSMakeRect(
			100.0,
			100.0,
			static_cast<CGFloat>(impl_->width),
			static_cast<CGFloat>(impl_->height));
		const NSWindowStyleMask style =
			NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
			NSWindowStyleMaskResizable;
		NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
													  styleMask:style
														backing:NSBackingStoreBuffered
														  defer:NO];
		if (window == nil) {
			throw std::runtime_error("NSWindow init failed.");
		}
		window.title = [NSString stringWithUTF8String:title.c_str()];
		window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
		window.releasedWhenClosed = NO;
		window.acceptsMouseMovedEvents = YES;

		HyperliteContentView* view = [[HyperliteContentView alloc] initWithFrame:frame];
		view.wantsLayer = YES;
		view.layer.magnificationFilter = kCAFilterNearest;
		view.layer.minificationFilter = kCAFilterNearest;
		view.layer.contentsGravity = kCAGravityResize;
		view.layer.contentsScale = 1.0;
		window.contentView = view;

		HyperliteWindowDelegate* delegate = [[HyperliteWindowDelegate alloc] init];
		delegate.impl = impl_.get();
		window.delegate = delegate;

		impl_->window = window;
		impl_->view = view;
		impl_->delegate = delegate;
		impl_->windowed_frame = window.frame;

		[window makeKeyAndOrderFront:nil];
		[window makeFirstResponder:view];
		[NSApp activateIgnoringOtherApps:YES];

		if (CVDisplayLinkCreateWithActiveCGDisplays(&impl_->display_link) == kCVReturnSuccess &&
			impl_->display_link != nullptr) {
			CVDisplayLinkSetOutputCallback(impl_->display_link, &DisplayLinkCallback, impl_.get());
			CVDisplayLinkStart(impl_->display_link);
		}
		impl_->alive = true;
	}
}

MacosWindow::~MacosWindow() {
	@autoreleasepool {
		if (impl_ && impl_->mouse_captured) {
			SetMouseCaptured(false);
		}
		if (impl_ && impl_->display_link != nullptr) {
			CVDisplayLinkStop(impl_->display_link);
			CVDisplayLinkRelease(impl_->display_link);
			impl_->display_link = nullptr;
		}
		if (impl_ && impl_->window != nil) {
			impl_->window.delegate = nil;
			[impl_->window close];
			impl_->window = nil;
			impl_->view = nil;
			impl_->delegate = nil;
		}
		if (impl_) {
			impl_->alive = false;
		}
	}
}

void MacosWindow::ConvertRgbaToBgra(const std::uint8_t* src, const int width, const int height) {
	const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	impl_->bgra.resize(n * 4U);
	std::uint8_t* dst = impl_->bgra.data();
	for (std::size_t i = 0; i < n; ++i) {
		const std::size_t o = i * 4U;
		dst[o + 0] = src[o + 2];
		dst[o + 1] = src[o + 1];
		dst[o + 2] = src[o + 0];
		dst[o + 3] = src[o + 3];
	}
}

void MacosWindow::UploadLayer() {
	if (impl_->view == nil || impl_->bgra.empty()) {
		return;
	}
	@autoreleasepool {
		const int w = impl_->width;
		const int h = impl_->height;
		const std::size_t expected =
			static_cast<std::size_t>(std::max(1, w)) * static_cast<std::size_t>(std::max(1, h)) * 4U;
		if (impl_->bgra.size() < expected) {
			return;
		}
		CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
		CGContextRef ctx = CGBitmapContextCreate(
			impl_->bgra.data(),
			static_cast<size_t>(w),
			static_cast<size_t>(h),
			8,
			static_cast<size_t>(w) * 4U,
			cs,
			kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
		CGColorSpaceRelease(cs);
		if (ctx == nullptr) {
			return;
		}
		CGImageRef image = CGBitmapContextCreateImage(ctx);
		CGContextRelease(ctx);
		if (image == nullptr) {
			return;
		}
		impl_->view.layer.contents = (__bridge id)image;
		CGImageRelease(image);
	}
}

void MacosWindow::WaitVsync() {
	if (!impl_->vsync || impl_->display_link == nullptr) {
		return;
	}
	std::unique_lock<std::mutex> lock(impl_->vsync_mu);
	impl_->vsync_pulse = false;
	impl_->vsync_cv.wait_for(lock, std::chrono::milliseconds(50), [this]() {
		return impl_->vsync_pulse;
	});
}

void MacosWindow::PollEvents(InputState& input_state) {
	input_state.mouse_delta = {0, 0};
	input_state.mouse_captured = impl_->mouse_captured;
	if (impl_->close_requested) {
		input_state.quit_requested = true;
		impl_->alive = false;
	}

	@autoreleasepool {
		NSEvent* event = nil;
		while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
										   untilDate:[NSDate distantPast]
											  inMode:NSDefaultRunLoopMode
											 dequeue:YES]) != nil) {
			const NSEventType type = event.type;
			switch (type) {
			case NSEventTypeKeyDown:
			case NSEventTypeKeyUp: {
				const int vk = MapKeyCodeToVk(event.keyCode);
				if (vk >= 0 && vk < static_cast<int>(input_state.key_down.size())) {
					input_state.key_down[static_cast<std::size_t>(vk)] = (type == NSEventTypeKeyDown);
				}
				// Swallow repeats into the view so the system beep does not fire.
				if (type == NSEventTypeKeyDown && event.ARepeat) {
					continue;
				}
				break;
			}
			case NSEventTypeFlagsChanged: {
				const NSEventModifierFlags flags = event.modifierFlags;
				input_state.key_down[0x10] = (flags & NSEventModifierFlagShift) != 0;
				input_state.key_down[0x11] = (flags & NSEventModifierFlagControl) != 0;
				input_state.key_down[0x12] = (flags & NSEventModifierFlagOption) != 0;
				input_state.key_down[0x5B] = (flags & NSEventModifierFlagCommand) != 0;
				break;
			}
			case NSEventTypeLeftMouseDown:
			case NSEventTypeLeftMouseUp:
				SetMouseButton(&input_state, MouseButton::Left, type == NSEventTypeLeftMouseDown);
				break;
			case NSEventTypeRightMouseDown:
			case NSEventTypeRightMouseUp:
				SetMouseButton(&input_state, MouseButton::Right, type == NSEventTypeRightMouseDown);
				break;
			case NSEventTypeOtherMouseDown:
			case NSEventTypeOtherMouseUp:
				SetMouseButton(&input_state, MouseButton::Middle, type == NSEventTypeOtherMouseDown);
				break;
			case NSEventTypeMouseMoved:
			case NSEventTypeLeftMouseDragged:
			case NSEventTypeRightMouseDragged:
			case NSEventTypeOtherMouseDragged: {
				int x = 0;
				int y = 0;
				ClientMouse(impl_->view, event.locationInWindow, x, y);
				if (impl_->mouse_captured) {
					input_state.mouse_delta.x += static_cast<int>(event.deltaX);
					input_state.mouse_delta.y += static_cast<int>(event.deltaY);
					const int cx = impl_->width / 2;
					const int cy = impl_->height / 2;
					input_state.mouse_pos = {cx, cy};
					if (impl_->window != nil) {
						const NSRect screen_rect =
							[impl_->window convertRectToScreen:NSMakeRect(
																   static_cast<CGFloat>(cx),
																   impl_->view.bounds.size.height - static_cast<CGFloat>(cy),
																   1.0,
																   1.0)];
						const CGPoint warp =
							CGPointMake(screen_rect.origin.x, CGDisplayBounds(CGMainDisplayID()).size.height - screen_rect.origin.y);
						CGWarpMouseCursorPosition(warp);
					}
				} else {
					input_state.mouse_delta.x += x - impl_->last_mouse_x;
					input_state.mouse_delta.y += y - impl_->last_mouse_y;
					input_state.mouse_pos = {x, y};
					impl_->last_mouse_x = x;
					impl_->last_mouse_y = y;
				}
				break;
			}
			default:
				break;
			}
			[NSApp sendEvent:event];
		}
	}
}

void MacosWindow::Present(const FrameBuffer& framebuffer) {
	PresentRaw(framebuffer.Data(), framebuffer.Width(), framebuffer.Height());
}

void MacosWindow::PresentRaw(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	if (!impl_->alive || pixels == nullptr || frame_width <= 0 || frame_height <= 0) {
		return;
	}
	// Present the software framebuffer as-is; the layer stretches to the view.
	impl_->width = frame_width;
	impl_->height = frame_height;
	ConvertRgbaToBgra(pixels, frame_width, frame_height);
	UploadLayer();
	WaitVsync();
}

void MacosWindow::PresentRawAsync(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	PresentRaw(pixels, frame_width, frame_height);
}

void MacosWindow::FlushAsyncPresent() {}

void MacosWindow::SetAsyncPresent(const bool enabled) {
	(void)enabled;
}

bool MacosWindow::AsyncPresentEnabled() const {
	return false;
}

void MacosWindow::PresentRect(
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

bool MacosWindow::IsAlive() const {
	return impl_ && impl_->alive;
}

void* MacosWindow::NativeHandle() const {
	if (impl_ == nullptr) {
		return nullptr;
	}
	return (__bridge void*)impl_->window;
}

int MacosWindow::Width() const {
	return impl_ ? impl_->width : 0;
}

int MacosWindow::Height() const {
	return impl_ ? impl_->height : 0;
}

void MacosWindow::SetMouseCaptured(const bool captured) {
	if (impl_->mouse_captured == captured) {
		return;
	}
	impl_->mouse_captured = captured;
	@autoreleasepool {
		if (captured) {
			CGAssociateMouseAndMouseCursorPosition(false);
			[NSCursor hide];
			if (impl_->window != nil && impl_->view != nil) {
				const int cx = impl_->width / 2;
				const int cy = impl_->height / 2;
				const NSRect screen_rect = [impl_->window convertRectToScreen:NSMakeRect(
																				  static_cast<CGFloat>(cx),
																				  impl_->view.bounds.size.height - static_cast<CGFloat>(cy),
																				  1.0,
																				  1.0)];
				const CGPoint warp =
					CGPointMake(screen_rect.origin.x, CGDisplayBounds(CGMainDisplayID()).size.height - screen_rect.origin.y);
				CGWarpMouseCursorPosition(warp);
			}
		} else {
			CGAssociateMouseAndMouseCursorPosition(true);
			[NSCursor unhide];
		}
	}
}

bool MacosWindow::MouseCaptured() const {
	return impl_ && impl_->mouse_captured;
}

void MacosWindow::SetFullscreen(const bool fullscreen) {
	if (impl_->window == nil || impl_->fullscreen == fullscreen) {
		impl_->fullscreen = fullscreen;
		return;
	}
	@autoreleasepool {
		if (!impl_->fullscreen) {
			impl_->windowed_frame = impl_->window.frame;
		}
		[impl_->window toggleFullScreen:nil];
		impl_->fullscreen = fullscreen;
	}
}

bool MacosWindow::IsFullscreen() const {
	return impl_ && impl_->fullscreen;
}

void MacosWindow::SetClientSize(const int width, const int height) {
	if (impl_->window == nil) {
		return;
	}
	@autoreleasepool {
		const int w = std::max(1, width);
		const int h = std::max(1, height);
		[impl_->window setContentSize:NSMakeSize(static_cast<CGFloat>(w), static_cast<CGFloat>(h))];
		impl_->width = w;
		impl_->height = h;
		impl_->pending_width = w;
		impl_->pending_height = h;
		impl_->resize_pending = true;
	}
}

bool MacosWindow::ConsumeResize(int& width, int& height) {
	if (!impl_->resize_pending) {
		return false;
	}
	impl_->resize_pending = false;
	width = impl_->pending_width;
	height = impl_->pending_height;
	return true;
}

void MacosWindow::SetVsync(const bool enabled) {
	impl_->vsync = enabled;
}

bool MacosWindow::VsyncEnabled() const {
	return impl_ && impl_->vsync;
}

} // namespace hyperlite
