#include "engine/headless_window.hpp"

namespace hyperlite {

HeadlessWindow::HeadlessWindow(const int width, const int height, std::string title)
	: width_(width),
	  height_(height) {
	(void)title;
}

void HeadlessWindow::PollEvents(InputState& input_state) {
	// Headless has no OS events; preserve quit_requested if already set.
	(void)input_state;
}

void HeadlessWindow::Present(const FrameBuffer& framebuffer) {
	(void)framebuffer;
}

void HeadlessWindow::PresentRaw(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	(void)pixels;
	(void)frame_width;
	(void)frame_height;
}

void HeadlessWindow::PresentRawAsync(const std::uint8_t* pixels, const int frame_width, const int frame_height) {
	PresentRaw(pixels, frame_width, frame_height);
}

void HeadlessWindow::FlushAsyncPresent() {}

void HeadlessWindow::SetAsyncPresent(const bool enabled) {
	(void)enabled;
}

bool HeadlessWindow::AsyncPresentEnabled() const {
	return false;
}

void HeadlessWindow::PresentRect(
	const FrameBuffer& framebuffer,
	const int x,
	const int y,
	const int width,
	const int height) {
	(void)framebuffer;
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}

bool HeadlessWindow::IsAlive() const {
	return true;
}

void* HeadlessWindow::NativeHandle() const {
	return nullptr;
}

int HeadlessWindow::Width() const {
	return width_;
}

int HeadlessWindow::Height() const {
	return height_;
}

void HeadlessWindow::SetMouseCaptured(const bool captured) {
	mouse_captured_ = captured;
}

bool HeadlessWindow::MouseCaptured() const {
	return mouse_captured_;
}

void HeadlessWindow::SetFullscreen(const bool fullscreen) {
	(void)fullscreen;
}

bool HeadlessWindow::IsFullscreen() const {
	return false;
}

void HeadlessWindow::SetClientSize(const int width, const int height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	if (width == width_ && height == height_) {
		return;
	}
	width_ = width;
	height_ = height;
	pending_width_ = width;
	pending_height_ = height;
	resize_pending_ = true;
}

bool HeadlessWindow::ConsumeResize(int& width, int& height) {
	if (!resize_pending_) {
		return false;
	}
	width = pending_width_;
	height = pending_height_;
	resize_pending_ = false;
	return true;
}

void HeadlessWindow::SetVsync(const bool enabled) {
	vsync_ = enabled;
}

bool HeadlessWindow::VsyncEnabled() const {
	return vsync_;
}

} // namespace hyperlite
