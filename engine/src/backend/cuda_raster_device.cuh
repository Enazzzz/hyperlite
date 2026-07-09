#pragma once

#include <cstdint>

#include <cuda_runtime.h>

namespace hyperlite::cuda_detail {

// Cohen-Sutherland outcodes, identical to the CPU rasterizer so the device
// clipper produces bit-for-bit matching results.
enum DeviceClipCode : int {
	kInside = 0,
	kLeft = 1,
	kRight = 2,
	kBottom = 4,
	kTop = 8
};

/**
 * Compute Cohen-Sutherland outcode for one endpoint (matches CPU path).
 */
__device__ inline int ComputeClipCode(const int x, const int y, const int width, const int height) {
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
 * Clip a line to framebuffer bounds, matching the CPU clipper exactly.
 */
__device__ inline bool ClipLineToFrame(int& x0, int& y0, int& x1, int& y1, const int width, const int height) {
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
 * Fast straight-alpha composite (src over dst), matching the CPU BlendOver path.
 */
__device__ inline std::uint32_t DeviceBlendOver(const std::uint32_t dst, const std::uint32_t src) {
	const std::uint32_t sa = src >> 24U;
	if (sa == 0U) {
		return dst;
	}
	if (sa == 255U) {
		return src;
	}
	const std::uint32_t inv_sa = 255U - sa;
	const std::uint32_t dst_rb = dst & 0x00FF00FFU;
	const std::uint32_t src_rb = src & 0x00FF00FFU;
	const std::uint32_t dst_ag = (dst >> 8U) & 0x00FF00FFU;
	const std::uint32_t src_ag = (src >> 8U) & 0x00FF00FFU;
	const std::uint32_t out_rb = ((src_rb * sa + dst_rb * inv_sa) >> 8U) & 0x00FF00FFU;
	const std::uint32_t out_ag = ((src_ag * sa + dst_ag * inv_sa) >> 8U) & 0x00FF00FFU;
	const std::uint32_t da = dst >> 24U;
	const std::uint32_t out_a = sa + ((da * inv_sa + 127U) / 255U);
	return out_rb | (out_ag << 8U) | (out_a << 24U);
}

/**
 * Write one pixel with optional alpha blending (matches CPU StorePixel).
 */
__device__ inline void DeviceStorePixel(std::uint32_t* dst, const std::uint32_t packed_color) {
	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
		*dst = packed_color;
	} else if (sa != 0U) {
		*dst = DeviceBlendOver(*dst, packed_color);
	}
}

/**
 * Write a single clipped pixel (matches CPU PutPixelClipped).
 */
__device__ inline void DevicePutPixel(
	std::uint32_t* fb,
	const int x,
	const int y,
	const int width,
	const int height,
	const std::uint32_t packed_color) {
	if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width) ||
		static_cast<unsigned int>(y) >= static_cast<unsigned int>(height)) {
		return;
	}
	DeviceStorePixel(
		&fb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)],
		packed_color);
}

/**
 * Draw a line via integer Bresenham, matching CPU DrawLine exactly.
 */
__device__ inline void DeviceDrawLine(
	std::uint32_t* fb,
	int x0,
	int y0,
	int x1,
	int y1,
	const int width,
	const int height,
	const std::uint32_t packed_color) {
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

	const int dx = abs(x1 - x0);
	const int sx = x0 < x1 ? 1 : -1;
	const int dy = -abs(y1 - y0);
	const int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width);
	std::ptrdiff_t idx = static_cast<std::ptrdiff_t>(y0) * stride + static_cast<std::ptrdiff_t>(x0);
	const std::ptrdiff_t step_x = static_cast<std::ptrdiff_t>(sx);
	const std::ptrdiff_t step_y = static_cast<std::ptrdiff_t>(sy) * stride;

	while (true) {
		DeviceStorePixel(&fb[idx], packed_color);
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
 * Fill an axis-aligned rectangle, clipped to framebuffer bounds.
 */
__device__ inline void DeviceRectFill(
	std::uint32_t* fb,
	const int x,
	const int y,
	const int w,
	const int h,
	const int width,
	const int height,
	const std::uint32_t packed_color) {
	if (w <= 0 || h <= 0) {
		return;
	}
	int cx0 = x < 0 ? 0 : x;
	int cy0 = y < 0 ? 0 : y;
	int cx1 = x + w;
	int cy1 = y + h;
	if (cx1 > width) {
		cx1 = width;
	}
	if (cy1 > height) {
		cy1 = height;
	}
	if (cx0 >= cx1 || cy0 >= cy1) {
		return;
	}
	for (int py = cy0; py < cy1; ++py) {
		const std::size_t row = static_cast<std::size_t>(py) * static_cast<std::size_t>(width);
		for (int px = cx0; px < cx1; ++px) {
			DeviceStorePixel(&fb[row + static_cast<std::size_t>(px)], packed_color);
		}
	}
}

/**
 * Draw one line with optional thickness (matches CPU DrawLineTracked).
 */
__device__ inline void DeviceDrawLineWithWidth(
	std::uint32_t* fb,
	int x0,
	int y0,
	int x1,
	int y1,
	const int width,
	const int height,
	const std::uint32_t packed_color,
	const int line_width) {
	int width_clamped = line_width;
	if (width_clamped < 1) {
		width_clamped = 1;
	} else if (width_clamped > 64) {
		width_clamped = 64;
	}
	if (width_clamped == 1) {
		DeviceDrawLine(fb, x0, y0, x1, y1, width, height, packed_color);
		return;
	}

	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
		const int half = width_clamped / 2;
		const int min_x = x0 < x1 ? x0 : x1;
		const int max_x = x0 > x1 ? x0 : x1;
		const int min_y = y0 < y1 ? y0 : y1;
		const int max_y = y0 > y1 ? y0 : y1;
		const int rx0 = min_x - half < 0 ? 0 : min_x - half;
		const int ry0 = min_y - half < 0 ? 0 : min_y - half;
		const int rx1 = max_x + half + 1 > width ? width : max_x + half + 1;
		const int ry1 = max_y + half + 1 > height ? height : max_y + half + 1;
		if (rx0 < rx1 && ry0 < ry1) {
			DeviceRectFill(fb, rx0, ry0, rx1 - rx0, ry1 - ry0, width, height, packed_color);
			return;
		}
	}

	const int dx = x1 - x0;
	const int dy = y1 - y0;
	if (dx == 0 && dy == 0) {
		DeviceDrawLine(fb, x0, y0, x1, y1, width, height, packed_color);
		return;
	}

	const int half = width_clamped / 2;
	const int adx = abs(dx);
	const int ady = abs(dy);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DeviceDrawLine(fb, x0, y0 + offset, x1, y1 + offset, width, height, packed_color);
		} else {
			DeviceDrawLine(fb, x0 + offset, y0, x1 + offset, y1, width, height, packed_color);
		}
	}
}

} // namespace hyperlite::cuda_detail
