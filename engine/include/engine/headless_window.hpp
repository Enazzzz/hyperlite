#pragma once

#include "engine/iwindow.hpp"

namespace hyperlite {

/**
 * Offscreen present surface for CI, benchmarks, and DISPLAY-less hosts.
 *
 * Raster and framebuffer APIs work normally; Present* calls are no-ops.
 */
class HeadlessWindow final : public IWindow {
public:
	/**
	 * Create a headless surface with a fixed client size.
	 */
	HeadlessWindow(int width, int height, std::string title);

	~HeadlessWindow() override = default;

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
	int width_ = 0;
	int height_ = 0;
	bool vsync_ = true;
	bool mouse_captured_ = false;
	bool resize_pending_ = false;
	int pending_width_ = 0;
	int pending_height_ = 0;
};

} // namespace hyperlite
