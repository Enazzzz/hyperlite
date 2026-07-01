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
	fb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = packed_color;
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
		fb[idx] = packed_color;
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

} // namespace hyperlite::cuda_detail
