#pragma once

#include "engine/cpu_blend.hpp"
#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif

namespace hyperlite::raster {

/**
 * Inclusive pixel bounds accumulated while rasterizing.
 */
struct PixelBounds {
	int min_x = 0;
	int min_y = 0;
	int max_x = 0;
	int max_y = 0;
	bool valid = false;

	/**
	 * Merge one point into the tracked bounds.
	 */
	void Expand(const int x, const int y) {
		if (!valid) {
			min_x = max_x = x;
			min_y = max_y = y;
			valid = true;
			return;
		}
		min_x = std::min(min_x, x);
		min_y = std::min(min_y, y);
		max_x = std::max(max_x, x);
		max_y = std::max(max_y, y);
	}

	/**
	 * Merge another bounds box.
	 */
	void Merge(const PixelBounds& other) {
		if (!other.valid) {
			return;
		}
		if (!valid) {
			*this = other;
			return;
		}
		min_x = std::min(min_x, other.min_x);
		min_y = std::min(min_y, other.min_y);
		max_x = std::max(max_x, other.max_x);
		max_y = std::max(max_y, other.max_y);
	}
};

/**
 * Fold per-thread dirty bounds into one rectangle after a parallel line pass.
 */
inline void MergeThreadBounds(PixelBounds& merged, const std::vector<PixelBounds>& thread_bounds) {
	for (const PixelBounds& bounds : thread_bounds) {
		merged.Merge(bounds);
	}
}

/**
 * Tune OpenMP for stable frame times (call once per process).
 */
inline void InitOpenMpOnce() {
#if defined(_OPENMP)
	static bool initialized = false;
	if (!initialized) {
		omp_set_dynamic(0);
		initialized = true;
	}
#endif
}

namespace detail {

enum ClipCode : int {
	kInside = 0,
	kLeft = 1,
	kRight = 2,
	kBottom = 4,
	kTop = 8
};

/**
 * Cohen-Sutherland outcode for one endpoint against an exclusive-max rectangle.
 *
 * xmax / ymax are exclusive (same convention as framebuffer width / height).
 */
inline int ComputeClipCodeRect(
	const int x,
	const int y,
	const int xmin,
	const int ymin,
	const int xmax,
	const int ymax) {
	int code = kInside;
	if (x < xmin) {
		code |= kLeft;
	} else if (x >= xmax) {
		code |= kRight;
	}
	if (y < ymin) {
		code |= kTop;
	} else if (y >= ymax) {
		code |= kBottom;
	}
	return code;
}

/**
 * Compute Cohen-Sutherland outcode for one endpoint against the framebuffer.
 */
inline int ComputeClipCode(const int x, const int y, const int width, const int height) {
	return ComputeClipCodeRect(x, y, 0, 0, width, height);
}

/**
 * Clip line to an exclusive-max rectangle so each tile owns its pixels without overlap.
 *
 * Used for framebuffer clip and for 64×64 tile scissors (depth-on OpenMP).
 */
inline bool ClipLineToRect(
	int& x0,
	int& y0,
	int& x1,
	int& y1,
	const int xmin,
	const int ymin,
	const int xmax,
	const int ymax) {
	int out0 = ComputeClipCodeRect(x0, y0, xmin, ymin, xmax, ymax);
	int out1 = ComputeClipCodeRect(x1, y1, xmin, ymin, xmax, ymax);

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
			y = ymin;
			x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		} else if ((out & kBottom) != 0) {
			y = ymax - 1;
			x = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		} else if ((out & kRight) != 0) {
			x = xmax - 1;
			y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		} else {
			x = xmin;
			y = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
		}

		if (out == out0) {
			x0 = x;
			y0 = y;
			out0 = ComputeClipCodeRect(x0, y0, xmin, ymin, xmax, ymax);
		} else {
			x1 = x;
			y1 = y;
			out1 = ComputeClipCodeRect(x1, y1, xmin, ymin, xmax, ymax);
		}
	}
}

/**
 * Clip line to framebuffer bounds so the inner raster loop skips bounds checks.
 */
inline bool ClipLineToFrame(int& x0, int& y0, int& x1, int& y1, const int width, const int height) {
	return ClipLineToRect(x0, y0, x1, y1, 0, 0, width, height);
}

/**
 * Reject lines whose bounding box misses the framebuffer entirely.
 */
inline bool LineBBoxVisible(const int x0, const int y0, const int x1, const int y1, const int width, const int height) {
	const int min_x = std::min(x0, x1);
	const int max_x = std::max(x0, x1);
	const int min_y = std::min(y0, y1);
	const int max_y = std::max(y0, y1);
	return max_x >= 0 && min_x < width && max_y >= 0 && min_y < height;
}

/**
 * Draw a clipped horizontal span and expand bounds.
 */
inline void DrawHorizontalLineTracked(
	std::uint32_t* dst,
	const int width,
	const int height,
	int x0,
	int x1,
	const int y,
	const std::uint32_t packed_color,
	PixelBounds& bounds) {
	if (static_cast<unsigned int>(y) >= static_cast<unsigned int>(height)) {
		return;
	}
	if (x0 > x1) {
		std::swap(x0, x1);
	}
	x0 = std::max(x0, 0);
	x1 = std::min(x1, width);
	if (x0 >= x1) {
		return;
	}
	FillSpan(
		dst + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x0),
		static_cast<std::size_t>(x1 - x0),
		packed_color);
	bounds.Expand(x0, y);
	bounds.Expand(x1 - 1, y);
}

/**
 * Draw a clipped vertical span and expand bounds.
 */
inline void DrawVerticalLineTracked(
	std::uint32_t* dst,
	const int width,
	const int height,
	const int x,
	int y0,
	int y1,
	const std::uint32_t packed_color,
	PixelBounds& bounds) {
	if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width)) {
		return;
	}
	if (y0 > y1) {
		std::swap(y0, y1);
	}
	y0 = std::max(y0, 0);
	y1 = std::min(y1, height - 1);
	if (y0 > y1) {
		return;
	}

	const std::size_t stride = static_cast<std::size_t>(width);
	std::uint32_t* ptr = dst + (static_cast<std::size_t>(y0) * stride) + static_cast<std::size_t>(x);
	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
		for (int y = y0; y <= y1; ++y) {
			*ptr = packed_color;
			ptr += stride;
		}
	} else if (sa != 0U) {
		for (int y = y0; y <= y1; ++y) {
			StorePixel(ptr, packed_color);
			ptr += stride;
		}
	}
	bounds.Expand(x, y0);
	bounds.Expand(x, y1);
}

/**
 * Dominant-axis Bresenham for clipped 1px lines.
 */
inline void DrawThinLineTracked(
	std::uint32_t* dst,
	const int width,
	const int height,
	int x0,
	int y0,
	int x1,
	int y1,
	const std::uint32_t packed_color,
	PixelBounds& bounds) {
	if (!LineBBoxVisible(x0, y0, x1, y1, width, height)) {
		return;
	}
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

	bounds.Expand(x0, y0);
	bounds.Expand(x1, y1);

	if (y0 == y1) {
		DrawHorizontalLineTracked(dst, width, height, x0, x1, y0, packed_color, bounds);
		return;
	}
	if (x0 == x1) {
		DrawVerticalLineTracked(dst, width, height, x0, y0, y1, packed_color, bounds);
		return;
	}

	const int adx = std::abs(x1 - x0);
	const int ady = std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	const std::size_t stride = static_cast<std::size_t>(width);
	const std::uint32_t sa = packed_color >> 24U;
	const bool opaque = sa == 255U;

	if (adx >= ady) {
		int err = adx / 2;
		int y = y0;
		std::uint32_t* ptr = dst + (static_cast<std::size_t>(y0) * stride) + static_cast<std::size_t>(x0);
		for (int x = x0; x != x1; x += sx) {
			if (opaque) {
				*ptr = packed_color;
			} else if (sa != 0U) {
				StorePixel(ptr, packed_color);
			}
			err -= ady;
			if (err < 0) {
				y += sy;
				ptr += stride * static_cast<std::size_t>(sy);
				err += adx;
			}
			ptr += static_cast<std::size_t>(sx);
		}
		if (opaque) {
			*ptr = packed_color;
		} else if (sa != 0U) {
			StorePixel(ptr, packed_color);
		}
		return;
	}

	int err = ady / 2;
	int x = x0;
	std::uint32_t* ptr = dst + (static_cast<std::size_t>(y0) * stride) + static_cast<std::size_t>(x0);
	for (int y = y0; y != y1; y += sy) {
		if (opaque) {
			*ptr = packed_color;
		} else if (sa != 0U) {
			StorePixel(ptr, packed_color);
		}
		err -= adx;
		if (err < 0) {
			x += sx;
			ptr += static_cast<std::size_t>(sx);
			err += ady;
		}
		ptr += stride * static_cast<std::size_t>(sy);
	}
	if (opaque) {
		*ptr = packed_color;
	} else if (sa != 0U) {
		StorePixel(ptr, packed_color);
	}
}

/**
 * Draw one line with optional thickness.
 */
inline void DrawLineTracked(
	std::uint32_t* dst,
	const int width,
	const int height,
	int x0,
	int y0,
	int x1,
	int y1,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
	const int width_clamped = std::max(1, std::min(line_width, 64));
	if (width_clamped == 1) {
		DrawThinLineTracked(dst, width, height, x0, y0, x1, y1, packed_color, bounds);
		return;
	}

	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
		const int half = width_clamped / 2;
		const int rx0 = std::max(0, std::min(x0, x1) - half);
		const int ry0 = std::max(0, std::min(y0, y1) - half);
		const int rx1 = std::min(width, std::max(x0, x1) + half + 1);
		const int ry1 = std::min(height, std::max(y0, y1) + half + 1);
		if (rx0 < rx1 && ry0 < ry1) {
			FillRectOpaque(dst, width, rx0, ry0, rx1, ry1, packed_color);
			bounds.Expand(rx0, ry0);
			bounds.Expand(rx1 - 1, ry1 - 1);
			return;
		}
	}

	const int dx = x1 - x0;
	const int dy = y1 - y0;
	if (dx == 0 && dy == 0) {
		DrawThinLineTracked(dst, width, height, x0, y0, x1, y1, packed_color, bounds);
		return;
	}

	const int half = width_clamped / 2;
	const int adx = std::abs(dx);
	const int ady = std::abs(dy);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DrawThinLineTracked(dst, width, height, x0, y0 + offset, x1, y1 + offset, packed_color, bounds);
		} else {
			DrawThinLineTracked(dst, width, height, x0 + offset, y0, x1 + offset, y1, packed_color, bounds);
		}
	}
}

/**
 * Minimum line count before spawning worker threads.
 */
inline constexpr std::size_t kParallelLineThreshold = 384U;

/**
 * Draw a contiguous slice of line commands.
 */
inline PixelBounds DrawLineCommandSlice(
	std::uint32_t* dst,
	const int width,
	const int height,
	const DrawCommand* commands,
	const std::size_t start,
	const std::size_t end) {
	PixelBounds bounds{};
	for (std::size_t i = start; i < end; ++i) {
		const DrawCommand& cmd = commands[i];
		DrawLineTracked(
			dst,
			width,
			height,
			cmd.x0,
			cmd.y0,
			cmd.x1,
			cmd.y1,
			cmd.packed_color,
			static_cast<int>(cmd.line_width),
			bounds);
	}
	return bounds;
}

/**
 * Draw line segments from packed int32 tuples [x0,y0,x1,y1,...].
 */
inline PixelBounds DrawLineSegmentSlice(
	std::uint32_t* dst,
	const int width,
	const int height,
	const std::int32_t* segments,
	const std::size_t start_line,
	const std::size_t end_line,
	const std::uint32_t packed_color,
	const int line_width) {
	PixelBounds bounds{};
	for (std::size_t line = start_line; line < end_line; ++line) {
		const std::size_t base = line * 4U;
		DrawLineTracked(
			dst,
			width,
			height,
			segments[base + 0U],
			segments[base + 1U],
			segments[base + 2U],
			segments[base + 3U],
			packed_color,
			line_width,
			bounds);
	}
	return bounds;
}

} // namespace detail

/**
 * Rasterize many line commands with optional multi-core parallelism.
 */
inline void RasterLineBatch(
	FrameBuffer& framebuffer,
	const DrawCommand* commands,
	const std::size_t count,
	const bool allow_parallel) {
	if (count == 0U) {
		return;
	}
	InitOpenMpOnce();

	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	PixelBounds bounds{};

#if defined(_OPENMP)
	const bool parallel = allow_parallel && count >= detail::kParallelLineThreshold && omp_get_max_threads() > 1;
	if (parallel) {
		const int max_threads = omp_get_max_threads();
		std::vector<PixelBounds> thread_bounds(static_cast<std::size_t>(max_threads));
		#pragma omp parallel for schedule(static)
		for (int i = 0; i < static_cast<int>(count); ++i) {
			const int tid = omp_get_thread_num();
			const DrawCommand& cmd = commands[static_cast<std::size_t>(i)];
			detail::DrawLineTracked(
				dst,
				width,
				height,
				cmd.x0,
				cmd.y0,
				cmd.x1,
				cmd.y1,
				cmd.packed_color,
				static_cast<int>(cmd.line_width),
				thread_bounds[static_cast<std::size_t>(tid)]);
		}
		MergeThreadBounds(bounds, thread_bounds);
	} else {
		bounds = detail::DrawLineCommandSlice(dst, width, height, commands, 0U, count);
	}
#else
	bounds = detail::DrawLineCommandSlice(dst, width, height, commands, 0U, count);
#endif

	if (bounds.valid) {
		framebuffer.NoteDirtyRect(bounds.min_x, bounds.min_y, bounds.max_x + 1, bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

/**
 * Direct wireframe path: clear then raster packed int32 segments without a command buffer.
 */
inline void ClearAndRasterLineSegments(
	FrameBuffer& framebuffer,
	const std::uint32_t clear_color,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width,
	const bool allow_parallel) {
	framebuffer.ResetDirty();
	InitOpenMpOnce();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	FillSpan(dst, framebuffer.PixelCount(), clear_color);
	framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());

	if (line_count == 0U) {
		framebuffer.FinalizeDirtyTiles();
		return;
	}

	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	PixelBounds bounds{};

#if defined(_OPENMP)
	const bool parallel = allow_parallel && line_count >= detail::kParallelLineThreshold && omp_get_max_threads() > 1;
	if (parallel) {
		const int max_threads = omp_get_max_threads();
		std::vector<PixelBounds> thread_bounds(static_cast<std::size_t>(max_threads));
		#pragma omp parallel for schedule(static)
		for (int line = 0; line < static_cast<int>(line_count); ++line) {
			const int tid = omp_get_thread_num();
			const std::size_t base = static_cast<std::size_t>(line) * 4U;
			detail::DrawLineTracked(
				dst,
				width,
				height,
				segments[base + 0U],
				segments[base + 1U],
				segments[base + 2U],
				segments[base + 3U],
				line_color,
				line_width,
				thread_bounds[static_cast<std::size_t>(tid)]);
		}
		MergeThreadBounds(bounds, thread_bounds);
	} else {
		bounds = detail::DrawLineSegmentSlice(dst, width, height, segments, 0U, line_count, line_color, line_width);
	}
#else
	bounds = detail::DrawLineSegmentSlice(dst, width, height, segments, 0U, line_count, line_color, line_width);
#endif

	if (bounds.valid) {
		framebuffer.NoteDirtyRect(bounds.min_x, bounds.min_y, bounds.max_x + 1, bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

} // namespace hyperlite::raster
