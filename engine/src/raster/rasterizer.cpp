#include "engine/rasterizer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace hyperlite::raster {

namespace {

/**
 * Store one pixel without blending.
 */
static inline void PutPixelClipped(FrameBuffer& framebuffer, const int x, const int y, const std::uint32_t packed_color) {
	if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(framebuffer.Width()) ||
		static_cast<unsigned int>(y) >= static_cast<unsigned int>(framebuffer.Height())) {
		return;
	}
	auto* ptr = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	ptr[static_cast<std::size_t>(y) * static_cast<std::size_t>(framebuffer.Width()) + static_cast<std::size_t>(x)] = packed_color;
}

enum ClipCode : int {
	kInside = 0,
	kLeft = 1,
	kRight = 2,
	kBottom = 4,
	kTop = 8
};

/**
 * Compute Cohen-Sutherland outcode for one endpoint.
 */
static inline int ComputeClipCode(const int x, const int y, const int width, const int height) {
	int code = kInside;
	if (x < 0) {
		code |= kLeft;
	} else if (x >= width) {
		code |= kRight;
	}
	if (y < 0) {
		code |= kTop;
	} else if (y >= height) {
		code |= kBottom;
	}
	return code;
}

/**
 * Clip line to framebuffer bounds to remove per-pixel bound checks.
 */
static bool ClipLineToFrame(int& x0, int& y0, int& x1, int& y1, const int width, const int height) {
	int out0 = ComputeClipCode(x0, y0, width, height);
	int out1 = ComputeClipCode(x1, y1, width, height);

	while (true) {
		if ((out0 | out1) == 0) {
			return true;
		}
		if ((out0 & out1) != 0) {
			return false;
		}

		const int out = (out0 != 0) ? out0 : out1;
		int x = 0;
		int y = 0;

		if ((out & kTop) != 0) {
			y = 0;
			x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		} else if ((out & kBottom) != 0) {
			y = height - 1;
			x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		} else if ((out & kRight) != 0) {
			x = width - 1;
			y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		} else {
			x = 0;
			y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		}

		if (out == out0) {
			x0 = x;
			y0 = y;
			out0 = ComputeClipCode(x0, y0, width, height);
		} else {
			x1 = x;
			y1 = y;
			out1 = ComputeClipCode(x1, y1, width, height);
		}
	}
}

/**
 * Draw a line using integer Bresenham traversal.
 */
static void DrawLine(FrameBuffer& framebuffer, int x0, int y0, int x1, int y1, const std::uint32_t packed_color) {
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	const bool endpoints_in_bounds =
		static_cast<unsigned int>(x0) < static_cast<unsigned int>(width) &&
		static_cast<unsigned int>(y0) < static_cast<unsigned int>(height) &&
		static_cast<unsigned int>(x1) < static_cast<unsigned int>(width) &&
		static_cast<unsigned int>(y1) < static_cast<unsigned int>(height);
	if (!endpoints_in_bounds) {
		if (!ClipLineToFrame(x0, y0, x1, y1, width, height)) {
			return;
		}
	}

	const int dx = std::abs(x1 - x0);
	const int sx = x0 < x1 ? 1 : -1;
	const int dy = -std::abs(y1 - y0);
	const int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const std::size_t stride = static_cast<std::size_t>(width);
	std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(y0) * static_cast<std::ptrdiff_t>(stride) + static_cast<std::ptrdiff_t>(x0);
	const std::ptrdiff_t step_x = static_cast<std::ptrdiff_t>(sx);
	const std::ptrdiff_t step_y = static_cast<std::ptrdiff_t>(sy) * static_cast<std::ptrdiff_t>(stride);

	while (true) {
		dst[idx] = packed_color;
		if (x0 == x1 && y0 == y1) {
			break;
		}
		const int e2 = err * 2;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
			idx += step_x;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
			idx += step_y;
		}
	}
}

/**
 * Fill axis-aligned rectangle while clipping to framebuffer bounds.
 */
static void RectFill(FrameBuffer& framebuffer, int x, int y, int w, int h, const std::uint32_t packed_color) {
	if (w <= 0 || h <= 0) {
		return;
	}
	const int x0 = std::max(x, 0);
	const int y0 = std::max(y, 0);
	const int x1 = std::min(x + w, framebuffer.Width());
	const int y1 = std::min(y + h, framebuffer.Height());
	if (x0 >= x1 || y0 >= y1) {
		return;
	}

	auto* ptr = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const std::size_t stride = static_cast<std::size_t>(framebuffer.Width());
	const std::size_t row_fill_count = static_cast<std::size_t>(x1 - x0);
	for (int row = y0; row < y1; ++row) {
		auto* row_ptr = ptr + static_cast<std::size_t>(row) * stride + static_cast<std::size_t>(x0);
		std::fill_n(row_ptr, row_fill_count, packed_color);
	}
}

/**
 * Draw rectangle border by composing four line segments.
 */
static void RectOutline(FrameBuffer& framebuffer, const int x, const int y, const int w, const int h, const std::uint32_t packed_color) {
	if (w <= 0 || h <= 0) {
		return;
	}
	DrawLine(framebuffer, x, y, x + w - 1, y, packed_color);
	DrawLine(framebuffer, x, y, x, y + h - 1, packed_color);
	DrawLine(framebuffer, x + w - 1, y, x + w - 1, y + h - 1, packed_color);
	DrawLine(framebuffer, x, y + h - 1, x + w - 1, y + h - 1, packed_color);
}

} // namespace

void ExecuteCommandBuffer(const CommandBuffer& command_buffer, FrameBuffer& framebuffer) {
	const DrawCommand* commands = command_buffer.Data();
	const std::size_t command_count = command_buffer.Size();
	for (std::size_t i = 0; i < command_count; ++i) {
		const auto& cmd = commands[i];
		const std::uint32_t packed_color = cmd.packed_color;
		switch (cmd.type) {
		case CommandType::kClear:
			{
				auto* ptr = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
				std::fill_n(ptr, framebuffer.PixelCount(), packed_color);
			}
			break;
		case CommandType::kPutPixel:
			PutPixelClipped(framebuffer, cmd.x0, cmd.y0, packed_color);
			break;
		case CommandType::kLine:
			DrawLine(framebuffer, cmd.x0, cmd.y0, cmd.x1, cmd.y1, packed_color);
			break;
		case CommandType::kRectFill:
			RectFill(framebuffer, cmd.x0, cmd.y0, cmd.x1, cmd.y1, packed_color);
			break;
		case CommandType::kRectOutline:
			RectOutline(framebuffer, cmd.x0, cmd.y0, cmd.x1, cmd.y1, packed_color);
			break;
		}
	}
}

} // namespace hyperlite::raster
