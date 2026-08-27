#pragma once

#include "engine/cpu_blend.hpp"
#include "engine/cpu_line_raster.hpp"
#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hyperlite::raster {

namespace detail3d {

/**
 * Homogeneous clip-space outcodes for the six ±w planes.
 */
enum ClipOut : int {
	kIn = 0,
	kLeft = 1,
	kRight = 2,
	kBottom = 4,
	kTop = 8,
	kNear = 16,
	kFar = 32
};

/**
 * Clip-space homogeneous point (x,y,z,w).
 */
struct ClipVert {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
};

/**
 * Column-major 4x4 * float4 (world → clip).
 */
inline ClipVert MulViewProj(const float* m, const float x, const float y, const float z) {
	ClipVert out{};
	out.x = m[0] * x + m[4] * y + m[8] * z + m[12];
	out.y = m[1] * x + m[5] * y + m[9] * z + m[13];
	out.z = m[2] * x + m[6] * y + m[10] * z + m[14];
	out.w = m[3] * x + m[7] * y + m[11] * z + m[15];
	return out;
}

/**
 * Outcodes from clip-space ±w planes (must run before perspective divide).
 */
inline int ComputeClipOutcode(const ClipVert& v) {
	int code = kIn;
	if (v.x < -v.w) {
		code |= kLeft;
	} else if (v.x > v.w) {
		code |= kRight;
	}
	if (v.y < -v.w) {
		code |= kBottom;
	} else if (v.y > v.w) {
		code |= kTop;
	}
	if (v.z < -v.w) {
		code |= kNear;
	} else if (v.z > v.w) {
		code |= kFar;
	}
	return code;
}

/**
 * Lerp two clip-space vertices (homogeneous).
 */
inline ClipVert LerpClip(const ClipVert& a, const ClipVert& b, const float t) {
	const float u = 1.0f - t;
	ClipVert out{};
	out.x = u * a.x + t * b.x;
	out.y = u * a.y + t * b.y;
	out.z = u * a.z + t * b.z;
	out.w = u * a.w + t * b.w;
	return out;
}

/**
 * Intersect segment against one clip plane; returns t in [0,1] from a→b.
 *
 * Plane forms (a on inside when value >= 0):
 *   Left:   x + w >= 0
 *   Right:  w - x >= 0
 *   Bottom: y + w >= 0
 *   Top:    w - y >= 0
 *   Near:   z + w >= 0
 *   Far:    w - z >= 0
 */
inline float ClipPlaneT(const ClipVert& a, const ClipVert& b, const int plane) {
	float va = 0.0f;
	float vb = 0.0f;
	switch (plane) {
	case kLeft:
		va = a.x + a.w;
		vb = b.x + b.w;
		break;
	case kRight:
		va = a.w - a.x;
		vb = b.w - b.x;
		break;
	case kBottom:
		va = a.y + a.w;
		vb = b.y + b.w;
		break;
	case kTop:
		va = a.w - a.y;
		vb = b.w - b.y;
		break;
	case kNear:
		va = a.z + a.w;
		vb = b.z + b.w;
		break;
	case kFar:
		va = a.w - a.z;
		vb = b.w - b.z;
		break;
	default:
		return 0.0f;
	}
	const float denom = va - vb;
	if (std::fabs(denom) < 1e-20f) {
		return 0.0f;
	}
	return va / denom;
}

/**
 * Cohen-Sutherland clip of a homogeneous segment against the view frustum.
 *
 * Returns false when the segment is trivial-out or degenerates after clipping.
 */
inline bool ClipLineHomogeneous(ClipVert& a, ClipVert& b) {
	int out_a = ComputeClipOutcode(a);
	int out_b = ComputeClipOutcode(b);
	constexpr int kPlanes[6] = {kLeft, kRight, kBottom, kTop, kNear, kFar};

	for (int iter = 0; iter < 12; ++iter) {
		if ((out_a | out_b) == 0) {
			return true;
		}
		if ((out_a & out_b) != 0) {
			return false;
		}
		const int out = (out_a != 0) ? out_a : out_b;
		int plane = 0;
		for (const int candidate : kPlanes) {
			if ((out & candidate) != 0) {
				plane = candidate;
				break;
			}
		}
		const float t = ClipPlaneT(a, b, plane);
		const ClipVert clipped = LerpClip(a, b, t);
		if (out == out_a) {
			a = clipped;
			out_a = ComputeClipOutcode(a);
		} else {
			b = clipped;
			out_b = ComputeClipOutcode(b);
		}
	}
	return (out_a | out_b) == 0;
}

/**
 * Map NDC z in [-1,1] to window depth in [0,1] (clear = 1.0 far).
 */
inline float NdcToWindowDepth(const float z_ndc) {
	return z_ndc * 0.5f + 0.5f;
}

/**
 * Perspective divide + viewport map. Viewport = full framebuffer.
 *
 * NDC y is flipped so +Y is up in clip/NDC and down in pixels (GL-style).
 */
inline bool ProjectToPixels(
	const ClipVert& clip,
	const int width,
	const int height,
	float& out_x,
	float& out_y,
	float& out_depth,
	float& out_inv_w) {
	if (std::fabs(clip.w) < 1e-20f) {
		return false;
	}
	const float inv_w = 1.0f / clip.w;
	const float ndc_x = clip.x * inv_w;
	const float ndc_y = clip.y * inv_w;
	const float ndc_z = clip.z * inv_w;
	out_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
	out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(height);
	// out_depth carries NDC z (= z/w); window mapping happens per-pixel.
	out_depth = ndc_z;
	out_inv_w = inv_w;
	return true;
}

/**
 * Depth-tested Bresenham with perspective-correct depth (lerp z/w and 1/w in screen space).
 *
 * depth may be null to skip the depth test (still draws color).
 */
inline void DrawThinLineDepth(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	int x0,
	int y0,
	int x1,
	int y1,
	float depth0,
	float depth1,
	float inv_w0,
	float inv_w1,
	const std::uint32_t packed_color,
	PixelBounds& bounds) {
	if (!detail::LineBBoxVisible(x0, y0, x1, y1, width, height)) {
		return;
	}
	const bool endpoints_in_bounds =
		static_cast<unsigned int>(x0) < static_cast<unsigned int>(width) &&
		static_cast<unsigned int>(y0) < static_cast<unsigned int>(height) &&
		static_cast<unsigned int>(x1) < static_cast<unsigned int>(width) &&
		static_cast<unsigned int>(y1) < static_cast<unsigned int>(height);
	if (!endpoints_in_bounds) {
		// Screen-space clip for pixel endpoints (depth lerped by clipped t is approximate).
		float fx0 = static_cast<float>(x0);
		float fy0 = static_cast<float>(y0);
		float fx1 = static_cast<float>(x1);
		float fy1 = static_cast<float>(y1);
		int ix0 = x0;
		int iy0 = y0;
		int ix1 = x1;
		int iy1 = y1;
		if (!detail::ClipLineToFrame(ix0, iy0, ix1, iy1, width, height)) {
			return;
		}
		const float len2 = (fx1 - fx0) * (fx1 - fx0) + (fy1 - fy0) * (fy1 - fy0);
		if (len2 > 1e-6f) {
			const float t0 = ((static_cast<float>(ix0) - fx0) * (fx1 - fx0) + (static_cast<float>(iy0) - fy0) * (fy1 - fy0)) / len2;
			const float t1 = ((static_cast<float>(ix1) - fx0) * (fx1 - fx0) + (static_cast<float>(iy1) - fy0) * (fy1 - fy0)) / len2;
			const float d0 = depth0;
			const float d1 = depth1;
			const float iw0 = inv_w0;
			const float iw1 = inv_w1;
			depth0 = d0 + (d1 - d0) * t0;
			depth1 = d0 + (d1 - d0) * t1;
			inv_w0 = iw0 + (iw1 - iw0) * t0;
			inv_w1 = iw0 + (iw1 - iw0) * t1;
		}
		x0 = ix0;
		y0 = iy0;
		x1 = ix1;
		y1 = iy1;
	}

	if (x0 == x1 && y0 == y1) {
		return;
	}

	bounds.Expand(x0, y0);
	bounds.Expand(x1, y1);

	const int adx = std::abs(x1 - x0);
	const int ady = std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	const std::size_t stride = static_cast<std::size_t>(width);
	const std::uint32_t sa = packed_color >> 24U;
	const bool opaque = sa == 255U;
	const int steps = std::max(adx, ady);
	const float step_scale = steps > 0 ? 1.0f / static_cast<float>(steps) : 0.0f;

	/**
	 * Perspective-correct depth at screen parameter t in [0,1]:
	 * interpolate z/w and 1/w in screen space, recover NDC z, map to [0,1].
	 * For attribute A = clip z: (lerp(z/w) / lerp(1/w)) * lerp(1/w) = lerp(z/w).
	 */
	auto depth_at = [&](const float t) -> float {
		const float zw = depth0 + (depth1 - depth0) * t;
		const float inv_w = inv_w0 + (inv_w1 - inv_w0) * t;
		float z_ndc = zw;
		if (std::fabs(inv_w) > 1e-20f) {
			const float clip_z = zw / inv_w;
			z_ndc = clip_z * inv_w;
		}
		return NdcToWindowDepth(z_ndc);
	};

	auto plot = [&](const int x, const int y, const float t) {
		if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width) ||
			static_cast<unsigned int>(y) >= static_cast<unsigned int>(height)) {
			return;
		}
		const float z = depth_at(t);
		if (depth != nullptr && !depth->TestAndWrite(x, y, z)) {
			return;
		}
		std::uint32_t* ptr = dst + (static_cast<std::size_t>(y) * stride) + static_cast<std::size_t>(x);
		if (opaque) {
			*ptr = packed_color;
		} else if (sa != 0U) {
			StorePixel(ptr, packed_color);
		}
	};

	if (adx >= ady) {
		int err = adx / 2;
		int y = y0;
		int step = 0;
		for (int x = x0; x != x1; x += sx, ++step) {
			plot(x, y, static_cast<float>(step) * step_scale);
			err -= ady;
			if (err < 0) {
				y += sy;
				err += adx;
			}
		}
		plot(x1, y1, 1.0f);
		return;
	}

	int err = ady / 2;
	int x = x0;
	int step = 0;
	for (int y = y0; y != y1; y += sy, ++step) {
		plot(x, y, static_cast<float>(step) * step_scale);
		err -= adx;
		if (err < 0) {
			x += sx;
			err += ady;
		}
	}
	plot(x1, y1, 1.0f);
}

/**
 * Transform one world-space segment, clip in homogeneous space, then rasterize.
 */
inline void DrawWorldSegment(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const float* view_proj,
	const float x0,
	const float y0,
	const float z0,
	const float x1,
	const float y1,
	const float z1,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
	ClipVert a = MulViewProj(view_proj, x0, y0, z0);
	ClipVert b = MulViewProj(view_proj, x1, y1, z1);
	if (!ClipLineHomogeneous(a, b)) {
		return;
	}

	float px0 = 0.0f;
	float py0 = 0.0f;
	float d0 = 0.0f;
	float iw0 = 1.0f;
	float px1 = 0.0f;
	float py1 = 0.0f;
	float d1 = 0.0f;
	float iw1 = 1.0f;
	if (!ProjectToPixels(a, width, height, px0, py0, d0, iw0) ||
		!ProjectToPixels(b, width, height, px1, py1, d1, iw1)) {
		return;
	}

	const int ix0 = static_cast<int>(std::lround(px0));
	const int iy0 = static_cast<int>(std::lround(py0));
	const int ix1 = static_cast<int>(std::lround(px1));
	const int iy1 = static_cast<int>(std::lround(py1));
	if (ix0 == ix1 && iy0 == iy1) {
		return;
	}

	const int width_clamped = std::max(1, std::min(line_width, 64));
	if (width_clamped == 1) {
		DrawThinLineDepth(dst, depth, width, height, ix0, iy0, ix1, iy1, d0, d1, iw0, iw1, packed_color, bounds);
		return;
	}

	const int half = width_clamped / 2;
	const int adx = std::abs(ix1 - ix0);
	const int ady = std::abs(iy1 - iy0);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DrawThinLineDepth(
				dst, depth, width, height, ix0, iy0 + offset, ix1, iy1 + offset, d0, d1, iw0, iw1, packed_color, bounds);
		} else {
			DrawThinLineDepth(
				dst, depth, width, height, ix0 + offset, iy0, ix1 + offset, iy1, d0, d1, iw0, iw1, packed_color, bounds);
		}
	}
}

/**
 * Raster one screen-space segment: pixel xy + NDC z in [-1,1] (no view-proj / frustum clip).
 */
inline void DrawScreenSegment(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const float x0,
	const float y0,
	const float z_ndc0,
	const float x1,
	const float y1,
	const float z_ndc1,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
	const int ix0 = static_cast<int>(std::lround(x0));
	const int iy0 = static_cast<int>(std::lround(y0));
	const int ix1 = static_cast<int>(std::lround(x1));
	const int iy1 = static_cast<int>(std::lround(y1));
	if (ix0 == ix1 && iy0 == iy1) {
		return;
	}
	const int width_clamped = std::max(1, std::min(line_width, 64));
	if (width_clamped == 1) {
		DrawThinLineDepth(
			dst, depth, width, height, ix0, iy0, ix1, iy1, z_ndc0, z_ndc1, 1.0f, 1.0f, packed_color, bounds);
		return;
	}
	const int half = width_clamped / 2;
	const int adx = std::abs(ix1 - ix0);
	const int ady = std::abs(iy1 - iy0);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DrawThinLineDepth(
				dst, depth, width, height, ix0, iy0 + offset, ix1, iy1 + offset, z_ndc0, z_ndc1, 1.0f, 1.0f, packed_color, bounds);
		} else {
			DrawThinLineDepth(
				dst, depth, width, height, ix0 + offset, iy0, ix1 + offset, iy1, z_ndc0, z_ndc1, 1.0f, 1.0f, packed_color, bounds);
		}
	}
}

} // namespace detail3d

/**
 * Raster world-space float segments [x0,y0,z0,x1,y1,z1,...] with view-proj + optional depth.
 *
 * When depth is non-null, OpenMP is disabled to avoid racy depth writes.
 */
inline void RasterLines3dWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const float* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width) {
	if (line_count == 0U || segments == nullptr || view_proj16 == nullptr) {
		return;
	}
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	PixelBounds bounds{};
	for (std::size_t line = 0U; line < line_count; ++line) {
		const std::size_t base = line * 6U;
		detail3d::DrawWorldSegment(
			dst,
			depth,
			width,
			height,
			view_proj16,
			segments[base + 0U],
			segments[base + 1U],
			segments[base + 2U],
			segments[base + 3U],
			segments[base + 4U],
			segments[base + 5U],
			line_color,
			line_width,
			bounds);
	}
	if (bounds.valid) {
		framebuffer.NoteDirtyRect(bounds.min_x, bounds.min_y, bounds.max_x + 1, bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

/**
 * Clear color (+ depth when provided) then raster world-space 3D lines.
 */
inline void ClearAndRasterLines3dWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const std::uint32_t clear_color,
	const float* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width) {
	framebuffer.ResetDirty();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	FillSpan(dst, framebuffer.PixelCount(), clear_color);
	framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
	if (depth != nullptr && depth->Allocated()) {
		depth->Clear(1.0f);
	}
	RasterLines3dWorld(framebuffer, depth, view_proj16, segments, line_count, line_color, line_width);
}

/**
 * Raster screen-space escape-hatch segments: pixel xy + NDC z [-1,1] per endpoint.
 */
inline void RasterLines3dScreen(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width) {
	if (line_count == 0U || segments == nullptr) {
		return;
	}
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	PixelBounds bounds{};
	for (std::size_t line = 0U; line < line_count; ++line) {
		const std::size_t base = line * 6U;
		detail3d::DrawScreenSegment(
			dst,
			depth,
			width,
			height,
			segments[base + 0U],
			segments[base + 1U],
			segments[base + 2U],
			segments[base + 3U],
			segments[base + 4U],
			segments[base + 5U],
			line_color,
			line_width,
			bounds);
	}
	if (bounds.valid) {
		framebuffer.NoteDirtyRect(bounds.min_x, bounds.min_y, bounds.max_x + 1, bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

} // namespace hyperlite::raster
