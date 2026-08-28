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
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif
#if defined(__AVX2__)
#include <immintrin.h>
#endif

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
 * Clip-space homogeneous point (x,y,z,w) with optional UV (carried through clip lerps).
 *
 * Flat-color paths leave u/v at 0. Textured paths set them before frustum clip so
 * Sutherland–Hodgman edge splits keep perspective-ready attributes.
 */
struct ClipVert {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
	float u = 0.0f;
	float v = 0.0f;
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
 * Lerp two clip-space vertices (homogeneous), including UV attributes.
 */
inline ClipVert LerpClip(const ClipVert& a, const ClipVert& b, const float t) {
	const float s = 1.0f - t;
	ClipVert out{};
	out.x = s * a.x + t * b.x;
	out.y = s * a.y + t * b.y;
	out.z = s * a.z + t * b.z;
	out.w = s * a.w + t * b.w;
	out.u = s * a.u + t * b.u;
	out.v = s * a.v + t * b.v;
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
 * Perspective divide + viewport map without storing 1/w (flat mesh transform path).
 */
inline bool ProjectToPixelsNoIw(
	const ClipVert& clip,
	const int width,
	const int height,
	float& out_x,
	float& out_y,
	float& out_depth) {
	if (std::fabs(clip.w) < 1e-20f) {
		return false;
	}
	const float inv_w = 1.0f / clip.w;
	const float ndc_x = clip.x * inv_w;
	const float ndc_y = clip.y * inv_w;
	const float ndc_z = clip.z * inv_w;
	out_x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(width);
	out_y = (1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(height);
	out_depth = ndc_z;
	return true;
}

/**
 * Plot one in-bounds pixel with optional depth (index already validated by clip).
 */
inline void PlotDepthPixel(
	std::uint32_t* color_ptr,
	float* depth_ptr,
	const float z_win,
	const std::uint32_t packed_color,
	const std::uint32_t sa,
	const bool opaque) {
	if (depth_ptr != nullptr) {
		if (!(z_win <= *depth_ptr)) {
			return;
		}
		*depth_ptr = z_win;
	}
	if (opaque) {
		*color_ptr = packed_color;
	} else if (sa != 0U) {
		StorePixel(color_ptr, packed_color);
	}
}

/**
 * Depth-tested horizontal span [x0, x1] inclusive at fixed y (window-space z lerped).
 *
 * Opaque + depth uses AVX2 8-wide compare/store when available; depth-null uses FillSpan.
 * Depth segments stop at 128×128 panel boundaries (panel-major layout).
 */
inline void DrawHorizontalSpanDepth(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int x0,
	const int x1,
	const int y,
	const float z0_win,
	const float z1_win,
	const std::uint32_t packed_color,
	const std::uint32_t sa,
	const bool opaque,
	PixelBounds& bounds) {
	int xa = x0;
	int xb = x1;
	float za = z0_win;
	float zb = z1_win;
	if (xa > xb) {
		std::swap(xa, xb);
		std::swap(za, zb);
	}
	bounds.Expand(xa, y);
	bounds.Expand(xb, y);

	const int total = xb - xa + 1;
	const float dz = total > 1 ? (zb - za) / static_cast<float>(total - 1) : 0.0f;
	constexpr int kPanel = DepthBuffer::kPanelSize;

	int seg_x = xa;
	float z = za;
	while (seg_x <= xb) {
		const int tile_x = seg_x / kPanel;
		const int seg_end = std::min(xb, (tile_x + 1) * kPanel - 1);
		const int count = seg_end - seg_x + 1;
		std::uint32_t* color_row = dst + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) +
			static_cast<std::size_t>(seg_x);

		if (depth == nullptr || !depth->Allocated()) {
			FillSpan(color_row, static_cast<std::size_t>(count), packed_color);
		} else if (opaque) {
			float* depth_row = depth->Ptr(seg_x, y);
			int i = 0;
#if defined(__AVX2__)
			__m256 z_vec = _mm256_setr_ps(
				z,
				z + dz,
				z + 2.0f * dz,
				z + 3.0f * dz,
				z + 4.0f * dz,
				z + 5.0f * dz,
				z + 6.0f * dz,
				z + 7.0f * dz);
			const __m256 dz8 = _mm256_set1_ps(8.0f * dz);
			const __m256i color_vec = _mm256_set1_epi32(static_cast<int>(packed_color));
			for (; i + 8 <= count; i += 8) {
				float* dptr = depth_row + i;
				std::uint32_t* cptr = color_row + i;
				const __m256 old_z = _mm256_loadu_ps(dptr);
				const __m256 pass = _mm256_cmp_ps(z_vec, old_z, _CMP_LE_OQ);
				const __m256 new_z = _mm256_blendv_ps(old_z, z_vec, pass);
				_mm256_storeu_ps(dptr, new_z);
				_mm256_maskstore_epi32(reinterpret_cast<int*>(cptr), _mm256_castps_si256(pass), color_vec);
				z_vec = _mm256_add_ps(z_vec, dz8);
			}
			float z_scalar = z + static_cast<float>(i) * dz;
#else
			float z_scalar = z;
#endif
			for (; i < count; ++i, z_scalar += dz) {
				if (z_scalar <= depth_row[i]) {
					depth_row[i] = z_scalar;
					color_row[i] = packed_color;
				}
			}
		} else {
			float z_scalar = z;
			for (int i = 0; i < count; ++i, z_scalar += dz) {
				float* depth_ptr = depth->Ptr(seg_x + i, y);
				PlotDepthPixel(color_row + i, depth_ptr, z_scalar, packed_color, sa, false);
			}
		}

		seg_x = seg_end + 1;
		z += dz * static_cast<float>(count);
	}
}

/**
 * Depth-tested vertical span [y0, y1] inclusive at fixed x (window-space z lerped).
 *
 * Walks panel rows with stride kPanelSize while y stays inside one 128×128 panel.
 */
inline void DrawVerticalSpanDepth(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int x,
	const int y0,
	const int y1,
	const float z0_win,
	const float z1_win,
	const std::uint32_t packed_color,
	const std::uint32_t sa,
	const bool opaque,
	PixelBounds& bounds) {
	int ya = y0;
	int yb = y1;
	float za = z0_win;
	float zb = z1_win;
	if (ya > yb) {
		std::swap(ya, yb);
		std::swap(za, zb);
	}
	bounds.Expand(x, ya);
	bounds.Expand(x, yb);

	const int total = yb - ya + 1;
	const float dz = total > 1 ? (zb - za) / static_cast<float>(total - 1) : 0.0f;
	const std::size_t color_stride = static_cast<std::size_t>(width);
	constexpr int kPanel = DepthBuffer::kPanelSize;
	const bool has_depth = depth != nullptr && depth->Allocated();

	int seg_y = ya;
	float z = za;
	while (seg_y <= yb) {
		const int tile_y = seg_y / kPanel;
		const int seg_end = std::min(yb, (tile_y + 1) * kPanel - 1);
		const int count = seg_end - seg_y + 1;
		std::uint32_t* color_ptr = dst + (static_cast<std::size_t>(seg_y) * color_stride) +
			static_cast<std::size_t>(x);
		float* depth_ptr = has_depth ? depth->Ptr(x, seg_y) : nullptr;
		float z_seg = z;
		for (int i = 0; i < count; ++i, z_seg += dz) {
			PlotDepthPixel(color_ptr, depth_ptr, z_seg, packed_color, sa, opaque);
			color_ptr += color_stride;
			if (depth_ptr != nullptr) {
				depth_ptr += kPanel;
			}
		}
		seg_y = seg_end + 1;
		z += dz * static_cast<float>(count);
	}
}

/**
 * Depth-tested Bresenham with screen-space lerp of window depth (affine of NDC z = z/w).
 *
 * depth may be null to skip the depth test (still draws color). Exact horizontal/vertical
 * use span fills (AVX2 depth+color when opaque). General segments use pointer-walked
 * Bresenham with incremental window z — no per-pixel t, inv_w, or bounds re-check.
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

	// inv_w only participates in screen-clip lerp above; window depth is affine of NDC z.
	(void)inv_w0;
	(void)inv_w1;

	const float z0_win = NdcToWindowDepth(depth0);
	const float z1_win = NdcToWindowDepth(depth1);
	const std::uint32_t sa = packed_color >> 24U;
	const bool opaque = sa == 255U;
	const bool has_depth = depth != nullptr && depth->Allocated();

	if (y0 == y1) {
		DrawHorizontalSpanDepth(dst, depth, width, x0, x1, y0, z0_win, z1_win, packed_color, sa, opaque, bounds);
		return;
	}
	if (x0 == x1) {
		DrawVerticalSpanDepth(dst, depth, width, x0, y0, y1, z0_win, z1_win, packed_color, sa, opaque, bounds);
		return;
	}

	bounds.Expand(x0, y0);
	bounds.Expand(x1, y1);

	const int adx = std::abs(x1 - x0);
	const int ady = std::abs(y1 - y0);
	const int sx = x0 < x1 ? 1 : -1;
	const int sy = y0 < y1 ? 1 : -1;
	const std::size_t stride = static_cast<std::size_t>(width);
	const int steps = std::max(adx, ady);
	const float dz = steps > 0 ? (z1_win - z0_win) / static_cast<float>(steps) : 0.0f;

	int cx = x0;
	int cy = y0;
	std::uint32_t* color_ptr = dst + (static_cast<std::size_t>(cy) * stride) + static_cast<std::size_t>(cx);
	float z = z0_win;

	// Opaque + depth: coordinate-tracked Bresenham (panel-major depth has non-linear stride).
	if (opaque && has_depth) {
		if (adx >= ady) {
			int err = adx / 2;
			for (int step = 0; step < steps; ++step) {
				float* depth_ptr = depth->Ptr(cx, cy);
				if (z <= *depth_ptr) {
					*depth_ptr = z;
					*color_ptr = packed_color;
				}
				err -= ady;
				cx += sx;
				color_ptr += sx;
				if (err < 0) {
					cy += sy;
					color_ptr += static_cast<std::ptrdiff_t>(sy) * static_cast<std::ptrdiff_t>(stride);
					err += adx;
				}
				z += dz;
			}
		} else {
			int err = ady / 2;
			for (int step = 0; step < steps; ++step) {
				float* depth_ptr = depth->Ptr(cx, cy);
				if (z <= *depth_ptr) {
					*depth_ptr = z;
					*color_ptr = packed_color;
				}
				err -= adx;
				cy += sy;
				color_ptr += static_cast<std::ptrdiff_t>(sy) * static_cast<std::ptrdiff_t>(stride);
				if (err < 0) {
					cx += sx;
					color_ptr += sx;
					err += ady;
				}
				z += dz;
			}
		}
		float* end_d = depth->Ptr(x1, y1);
		if (z1_win <= *end_d) {
			*end_d = z1_win;
			dst[(static_cast<std::size_t>(y1) * stride) + static_cast<std::size_t>(x1)] = packed_color;
		}
		return;
	}

	if (adx >= ady) {
		int err = adx / 2;
		for (int step = 0; step < steps; ++step) {
			float* depth_ptr = has_depth ? depth->Ptr(cx, cy) : nullptr;
			PlotDepthPixel(color_ptr, depth_ptr, z, packed_color, sa, opaque);
			err -= ady;
			cx += sx;
			color_ptr += sx;
			if (err < 0) {
				cy += sy;
				color_ptr += static_cast<std::ptrdiff_t>(sy) * static_cast<std::ptrdiff_t>(stride);
				err += adx;
			}
			z += dz;
		}
	} else {
		int err = ady / 2;
		for (int step = 0; step < steps; ++step) {
			float* depth_ptr = has_depth ? depth->Ptr(cx, cy) : nullptr;
			PlotDepthPixel(color_ptr, depth_ptr, z, packed_color, sa, opaque);
			err -= adx;
			cy += sy;
			color_ptr += static_cast<std::ptrdiff_t>(sy) * static_cast<std::ptrdiff_t>(stride);
			if (err < 0) {
				cx += sx;
				color_ptr += sx;
				err += ady;
			}
			z += dz;
		}
	}

	PlotDepthPixel(
		dst + (static_cast<std::size_t>(y1) * stride) + static_cast<std::size_t>(x1),
		has_depth ? depth->Ptr(x1, y1) : nullptr,
		z1_win,
		packed_color,
		sa,
		opaque);
}

/**
 * Raster one already-clipped (or trivial-in) clip-space segment to pixels.
 */
inline void DrawClipSegment(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	ClipVert a,
	ClipVert b,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
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
 * Transform one world-space segment, clip in homogeneous space, then rasterize.
 *
 * Outcodes: trivial reject before clip; trivial accept skips Cohen–Sutherland.
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
	const int out_a = ComputeClipOutcode(a);
	const int out_b = ComputeClipOutcode(b);
	if ((out_a & out_b) != 0) {
		return;
	}
	if ((out_a | out_b) != 0) {
		if (!ClipLineHomogeneous(a, b)) {
			return;
		}
	}
	DrawClipSegment(dst, depth, width, height, a, b, packed_color, line_width, bounds);
}

#if defined(__AVX2__) && defined(__FMA__)
/**
 * Transform 8 world positions (AoS xyz packed as consecutive floats with stride 3) through MVP.
 *
 * Used to batch the two endpoints of four line segments. Writes clip xyzw into SoA temps.
 */
inline void TransformEightPositionsAvx2(
	const float* mvp,
	const float* p0,
	const float* p1,
	const float* p2,
	const float* p3,
	const float* p4,
	const float* p5,
	const float* p6,
	const float* p7,
	float* out_x,
	float* out_y,
	float* out_z,
	float* out_w) {
	const __m256 m0 = _mm256_set1_ps(mvp[0]);
	const __m256 m1 = _mm256_set1_ps(mvp[1]);
	const __m256 m2 = _mm256_set1_ps(mvp[2]);
	const __m256 m3 = _mm256_set1_ps(mvp[3]);
	const __m256 m4 = _mm256_set1_ps(mvp[4]);
	const __m256 m5 = _mm256_set1_ps(mvp[5]);
	const __m256 m6 = _mm256_set1_ps(mvp[6]);
	const __m256 m7 = _mm256_set1_ps(mvp[7]);
	const __m256 m8 = _mm256_set1_ps(mvp[8]);
	const __m256 m9 = _mm256_set1_ps(mvp[9]);
	const __m256 m10 = _mm256_set1_ps(mvp[10]);
	const __m256 m11 = _mm256_set1_ps(mvp[11]);
	const __m256 m12 = _mm256_set1_ps(mvp[12]);
	const __m256 m13 = _mm256_set1_ps(mvp[13]);
	const __m256 m14 = _mm256_set1_ps(mvp[14]);
	const __m256 m15 = _mm256_set1_ps(mvp[15]);
	const __m256 vx = _mm256_setr_ps(p0[0], p1[0], p2[0], p3[0], p4[0], p5[0], p6[0], p7[0]);
	const __m256 vy = _mm256_setr_ps(p0[1], p1[1], p2[1], p3[1], p4[1], p5[1], p6[1], p7[1]);
	const __m256 vz = _mm256_setr_ps(p0[2], p1[2], p2[2], p3[2], p4[2], p5[2], p6[2], p7[2]);
	const __m256 cx = _mm256_fmadd_ps(m0, vx, _mm256_fmadd_ps(m4, vy, _mm256_fmadd_ps(m8, vz, m12)));
	const __m256 cy = _mm256_fmadd_ps(m1, vx, _mm256_fmadd_ps(m5, vy, _mm256_fmadd_ps(m9, vz, m13)));
	const __m256 cz = _mm256_fmadd_ps(m2, vx, _mm256_fmadd_ps(m6, vy, _mm256_fmadd_ps(m10, vz, m14)));
	const __m256 cw = _mm256_fmadd_ps(m3, vx, _mm256_fmadd_ps(m7, vy, _mm256_fmadd_ps(m11, vz, m15)));
	_mm256_storeu_ps(out_x, cx);
	_mm256_storeu_ps(out_y, cy);
	_mm256_storeu_ps(out_z, cz);
	_mm256_storeu_ps(out_w, cw);
}
#endif

/**
 * Draw one world segment from pre-transformed clip endpoints + outcodes.
 */
inline void DrawWorldSegmentFromClip(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	ClipVert a,
	ClipVert b,
	const int out_a,
	const int out_b,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
	if ((out_a & out_b) != 0) {
		return;
	}
	if ((out_a | out_b) != 0) {
		if (!ClipLineHomogeneous(a, b)) {
			return;
		}
	}
	DrawClipSegment(dst, depth, width, height, a, b, packed_color, line_width, bounds);
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

/**
 * Screen-space segment after clip/project (ready to raster or bin into tiles).
 */
struct ProjectedSeg {
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	float depth0 = 0.0f;
	float depth1 = 0.0f;
	float inv_w0 = 1.0f;
	float inv_w1 = 1.0f;
};

/**
 * Per-draw scratch for depth-on 3D line project / tile bins (reused across draws).
 *
 * Process-static like MeshDrawScratch — not thread_local (TLS breaks non-PIC → Python .so).
 * Emit + bin on one thread; OpenMP workers only read bins afterward.
 */
struct Line3dDrawScratch {
	/** Projected segments for the current batch. */
	std::vector<ProjectedSeg> segs{};
	/** 64×64 tile → projected segment index lists (capacity retained). */
	std::vector<std::vector<std::uint32_t>> bins{};
};

/**
 * Process-wide 3D line scratch (project + bin are single-threaded before OpenMP fill).
 */
inline Line3dDrawScratch& GetLine3dDrawScratch() {
	static Line3dDrawScratch scratch;
	return scratch;
}

/**
 * Minimum segment count before depth-on tiled OpenMP is worth the bin/fork tax.
 */
inline constexpr std::size_t kDepthTileParallelThreshold = 384U;

/**
 * Minimum mean Chebyshev screen span (max(|dx|,|dy|)) before parallel depth raster beats serial.
 *
 * Short-diagonal default bench (~14 px mean) loses to bin/fork overhead; long multi-row spans
 * (~216 px) win with 64px-tall full-width row strips.
 */
inline constexpr int kMinMeanSpanForTiles = 48;

/**
 * Project a clipped clip-space segment into ProjectedSeg; returns false if degenerate.
 */
inline bool TryProjectClipSegment(
	const ClipVert& a,
	const ClipVert& b,
	const int width,
	const int height,
	ProjectedSeg& out) {
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
		return false;
	}
	out.x0 = static_cast<int>(std::lround(px0));
	out.y0 = static_cast<int>(std::lround(py0));
	out.x1 = static_cast<int>(std::lround(px1));
	out.y1 = static_cast<int>(std::lround(py1));
	if (out.x0 == out.x1 && out.y0 == out.y1) {
		return false;
	}
	out.depth0 = d0;
	out.depth1 = d1;
	out.inv_w0 = iw0;
	out.inv_w1 = iw1;
	return true;
}

/**
 * Clip + project one world segment (from pre-transformed clip) into the projected list.
 */
inline void EmitProjectedFromClip(
	ClipVert a,
	ClipVert b,
	const int out_a,
	const int out_b,
	const int width,
	const int height,
	std::vector<ProjectedSeg>& segs) {
	if ((out_a & out_b) != 0) {
		return;
	}
	if ((out_a | out_b) != 0) {
		if (!ClipLineHomogeneous(a, b)) {
			return;
		}
	}
	ProjectedSeg seg{};
	if (TryProjectClipSegment(a, b, width, height, seg)) {
		segs.push_back(seg);
	}
}

/**
 * Draw one projected segment (optional width) with the existing H/V + Bresenham fill.
 */
inline void DrawProjectedSeg(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const ProjectedSeg& seg,
	const std::uint32_t packed_color,
	const int line_width,
	PixelBounds& bounds) {
	const int width_clamped = std::max(1, std::min(line_width, 64));
	if (width_clamped == 1) {
		DrawThinLineDepth(
			dst,
			depth,
			width,
			height,
			seg.x0,
			seg.y0,
			seg.x1,
			seg.y1,
			seg.depth0,
			seg.depth1,
			seg.inv_w0,
			seg.inv_w1,
			packed_color,
			bounds);
		return;
	}
	const int half = width_clamped / 2;
	const int adx = std::abs(seg.x1 - seg.x0);
	const int ady = std::abs(seg.y1 - seg.y0);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DrawThinLineDepth(
				dst,
				depth,
				width,
				height,
				seg.x0,
				seg.y0 + offset,
				seg.x1,
				seg.y1 + offset,
				seg.depth0,
				seg.depth1,
				seg.inv_w0,
				seg.inv_w1,
				packed_color,
				bounds);
		} else {
			DrawThinLineDepth(
				dst,
				depth,
				width,
				height,
				seg.x0 + offset,
				seg.y0,
				seg.x1 + offset,
				seg.y1,
				seg.depth0,
				seg.depth1,
				seg.inv_w0,
				seg.inv_w1,
				packed_color,
				bounds);
		}
	}
}

/**
 * Clip one thin segment to a tile rect (exclusive max) with depth/inv_w lerp, then raster.
 *
 * Tile owns pixels → OpenMP over tiles cannot race the depth buffer.
 */
inline void DrawThinLineDepthInTile(
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
	const int tile_x0,
	const int tile_y0,
	const int tile_x1,
	const int tile_y1,
	PixelBounds& bounds) {
	if (std::max(x0, x1) < tile_x0 || std::min(x0, x1) >= tile_x1 ||
		std::max(y0, y1) < tile_y0 || std::min(y0, y1) >= tile_y1) {
		return;
	}

	const float fx0 = static_cast<float>(x0);
	const float fy0 = static_cast<float>(y0);
	const float fx1 = static_cast<float>(x1);
	const float fy1 = static_cast<float>(y1);
	int ix0 = x0;
	int iy0 = y0;
	int ix1 = x1;
	int iy1 = y1;
	if (!detail::ClipLineToRect(ix0, iy0, ix1, iy1, tile_x0, tile_y0, tile_x1, tile_y1)) {
		return;
	}
	const float len2 = (fx1 - fx0) * (fx1 - fx0) + (fy1 - fy0) * (fy1 - fy0);
	if (len2 > 1e-6f) {
		const float t0 =
			((static_cast<float>(ix0) - fx0) * (fx1 - fx0) + (static_cast<float>(iy0) - fy0) * (fy1 - fy0)) / len2;
		const float t1 =
			((static_cast<float>(ix1) - fx0) * (fx1 - fx0) + (static_cast<float>(iy1) - fy0) * (fy1 - fy0)) / len2;
		const float d0 = depth0;
		const float d1 = depth1;
		const float iw0 = inv_w0;
		const float iw1 = inv_w1;
		depth0 = d0 + (d1 - d0) * t0;
		depth1 = d0 + (d1 - d0) * t1;
		inv_w0 = iw0 + (iw1 - iw0) * t0;
		inv_w1 = iw0 + (iw1 - iw0) * t1;
	}
	DrawThinLineDepth(
		dst, depth, width, height, ix0, iy0, ix1, iy1, depth0, depth1, inv_w0, inv_w1, packed_color, bounds);
}

/**
 * Raster one projected segment clipped to a single tile (width offsets included).
 */
inline void DrawProjectedSegInTile(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const ProjectedSeg& seg,
	const std::uint32_t packed_color,
	const int line_width,
	const int tile_x0,
	const int tile_y0,
	const int tile_x1,
	const int tile_y1,
	PixelBounds& bounds) {
	const int width_clamped = std::max(1, std::min(line_width, 64));
	if (width_clamped == 1) {
		DrawThinLineDepthInTile(
			dst,
			depth,
			width,
			height,
			seg.x0,
			seg.y0,
			seg.x1,
			seg.y1,
			seg.depth0,
			seg.depth1,
			seg.inv_w0,
			seg.inv_w1,
			packed_color,
			tile_x0,
			tile_y0,
			tile_x1,
			tile_y1,
			bounds);
		return;
	}
	const int half = width_clamped / 2;
	const int adx = std::abs(seg.x1 - seg.x0);
	const int ady = std::abs(seg.y1 - seg.y0);
	for (int offset = -half; offset <= half; ++offset) {
		if (adx >= ady) {
			DrawThinLineDepthInTile(
				dst,
				depth,
				width,
				height,
				seg.x0,
				seg.y0 + offset,
				seg.x1,
				seg.y1 + offset,
				seg.depth0,
				seg.depth1,
				seg.inv_w0,
				seg.inv_w1,
				packed_color,
				tile_x0,
				tile_y0,
				tile_x1,
				tile_y1,
				bounds);
		} else {
			DrawThinLineDepthInTile(
				dst,
				depth,
				width,
				height,
				seg.x0 + offset,
				seg.y0,
				seg.x1 + offset,
				seg.y1,
				seg.depth0,
				seg.depth1,
				seg.inv_w0,
				seg.inv_w1,
				packed_color,
				tile_x0,
				tile_y0,
				tile_x1,
				tile_y1,
				bounds);
		}
	}
}

/**
 * Parallel depth-on raster: bin into 64px-tall full-width row strips, OpenMP over strips.
 *
 * Full 64×64 AABB bins were a measured loss vs serial on both short and long wireframes
 * (too many Cohen–Sutherland clip restarts). Row strips still match dirty-tile height and
 * guarantee exclusive pixel ownership (no depth races). Hybrid: serial when the batch is
 * small or mean screen span is short.
 *
 * Reuses Line3dDrawScratch bins (clear, keep capacity).
 */
inline void RasterProjectedSegsDepth(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const std::vector<ProjectedSeg>& segs,
	const std::uint32_t line_color,
	const int line_width) {
	if (segs.empty()) {
		return;
	}
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	PixelBounds global_bounds{};

	std::uint64_t span_sum = 0U;
	for (const ProjectedSeg& seg : segs) {
		const int adx = std::abs(seg.x1 - seg.x0);
		const int ady = std::abs(seg.y1 - seg.y0);
		span_sum += static_cast<std::uint64_t>(std::max(adx, ady));
	}
	const int mean_span = static_cast<int>(span_sum / static_cast<std::uint64_t>(segs.size()));

	constexpr int kRowTile = FrameBuffer::kDirtyTileSize;
	const int strip_count = std::max(1, (height + kRowTile - 1) / kRowTile);

#if defined(_OPENMP)
	InitOpenMpOnce();
	const bool use_parallel =
		depth != nullptr && depth->Allocated() && segs.size() >= kDepthTileParallelThreshold &&
		mean_span >= kMinMeanSpanForTiles && omp_get_max_threads() > 1;
#else
	(void)mean_span;
	const bool use_parallel = false;
#endif

	if (!use_parallel) {
		for (const ProjectedSeg& seg : segs) {
			DrawProjectedSeg(dst, depth, width, height, seg, line_color, line_width, global_bounds);
		}
		if (global_bounds.valid) {
			framebuffer.NoteDirtyRect(
				global_bounds.min_x, global_bounds.min_y, global_bounds.max_x + 1, global_bounds.max_y + 1);
		}
		framebuffer.FinalizeDirtyTiles();
		return;
	}

	const int half = std::max(0, std::min(line_width, 64) / 2);
	Line3dDrawScratch& scratch = GetLine3dDrawScratch();
	auto& bins = scratch.bins;
	if (static_cast<int>(bins.size()) != strip_count) {
		bins.assign(static_cast<std::size_t>(strip_count), {});
	} else {
		for (auto& bin : bins) {
			bin.clear();
		}
	}

	// Bin by Y overlap into 64px-tall strips spanning the full framebuffer width.
	for (std::uint32_t si = 0U; si < static_cast<std::uint32_t>(segs.size()); ++si) {
		const ProjectedSeg& seg = segs[si];
		const int min_y = std::min(seg.y0, seg.y1) - half;
		const int max_y = std::max(seg.y0, seg.y1) + half;
		int sy0 = min_y / kRowTile;
		int sy1 = max_y / kRowTile;
		if (min_y < 0) {
			sy0 = (min_y - kRowTile + 1) / kRowTile;
		}
		sy0 = std::max(0, sy0);
		sy1 = std::min(strip_count - 1, sy1);
		if (sy0 > sy1) {
			continue;
		}
		for (int s = sy0; s <= sy1; ++s) {
			bins[static_cast<std::size_t>(s)].push_back(si);
		}
	}

#if defined(_OPENMP)
	const int max_threads = std::max(1, omp_get_max_threads());
	std::vector<PixelBounds> thread_bounds(static_cast<std::size_t>(max_threads));
	#pragma omp parallel for schedule(dynamic)
	for (int s = 0; s < strip_count; ++s) {
		const auto& list = bins[static_cast<std::size_t>(s)];
		if (list.empty()) {
			continue;
		}
		const int tid = omp_get_thread_num();
		PixelBounds& local = thread_bounds[static_cast<std::size_t>(tid)];
		const int y0 = s * kRowTile;
		const int y1 = std::min(y0 + kRowTile, height);
		for (const std::uint32_t idx : list) {
			DrawProjectedSegInTile(
				dst, depth, width, height, segs[idx], line_color, line_width, 0, y0, width, y1, local);
		}
	}
	MergeThreadBounds(global_bounds, thread_bounds);
#endif

	if (global_bounds.valid) {
		framebuffer.NoteDirtyRect(
			global_bounds.min_x, global_bounds.min_y, global_bounds.max_x + 1, global_bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

/**
 * Transform + clip + project all world segments into scratch.segs (AVX2 MVP when available).
 */
inline void EmitProjectedWorldSegments(
	const float* view_proj16,
	const float* segments,
	const std::size_t line_count,
	const int width,
	const int height,
	std::vector<ProjectedSeg>& segs) {
	segs.clear();
	segs.reserve(line_count);

#if defined(__AVX2__) && defined(__FMA__)
	std::size_t line = 0U;
	alignas(32) float cx[8];
	alignas(32) float cy[8];
	alignas(32) float cz[8];
	alignas(32) float cw[8];
	for (; line + 4U <= line_count; line += 4U) {
		const float* s0 = segments + line * 6U;
		const float* s1 = segments + (line + 1U) * 6U;
		const float* s2 = segments + (line + 2U) * 6U;
		const float* s3 = segments + (line + 3U) * 6U;
		TransformEightPositionsAvx2(
			view_proj16,
			s0 + 0,
			s0 + 3,
			s1 + 0,
			s1 + 3,
			s2 + 0,
			s2 + 3,
			s3 + 0,
			s3 + 3,
			cx,
			cy,
			cz,
			cw);
		for (int lane = 0; lane < 4; ++lane) {
			const int i0 = lane * 2;
			const int i1 = i0 + 1;
			ClipVert a{};
			ClipVert b{};
			a.x = cx[i0];
			a.y = cy[i0];
			a.z = cz[i0];
			a.w = cw[i0];
			b.x = cx[i1];
			b.y = cy[i1];
			b.z = cz[i1];
			b.w = cw[i1];
			EmitProjectedFromClip(
				a, b, ComputeClipOutcode(a), ComputeClipOutcode(b), width, height, segs);
		}
	}
	for (; line < line_count; ++line) {
		const std::size_t base = line * 6U;
		ClipVert a = MulViewProj(view_proj16, segments[base + 0U], segments[base + 1U], segments[base + 2U]);
		ClipVert b = MulViewProj(view_proj16, segments[base + 3U], segments[base + 4U], segments[base + 5U]);
		EmitProjectedFromClip(
			a, b, ComputeClipOutcode(a), ComputeClipOutcode(b), width, height, segs);
	}
#else
	for (std::size_t line = 0U; line < line_count; ++line) {
		const std::size_t base = line * 6U;
		ClipVert a = MulViewProj(view_proj16, segments[base + 0U], segments[base + 1U], segments[base + 2U]);
		ClipVert b = MulViewProj(view_proj16, segments[base + 3U], segments[base + 4U], segments[base + 5U]);
		EmitProjectedFromClip(
			a, b, ComputeClipOutcode(a), ComputeClipOutcode(b), width, height, segs);
	}
#endif
}

} // namespace detail3d

/**
 * Raster world-space float segments [x0,y0,z0,x1,y1,z1,...] with view-proj + optional depth.
 *
 * Depth-off: OpenMP over segments (same race rule as before).
 * Depth-on: project once, then OpenMP over 64×64 tiles (each tile owns color+depth) when the
 * batch is large enough; otherwise serial projected raster (avoids bin tax on tiny batches).
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

	// Depth-on: tile-parallel path (no racy depth writes).
	if (depth != nullptr && depth->Allocated()) {
		detail3d::Line3dDrawScratch& scratch = detail3d::GetLine3dDrawScratch();
		detail3d::EmitProjectedWorldSegments(
			view_proj16, segments, line_count, width, height, scratch.segs);
		detail3d::RasterProjectedSegsDepth(framebuffer, depth, scratch.segs, line_color, line_width);
		return;
	}

#if defined(_OPENMP)
	InitOpenMpOnce();
	constexpr std::size_t kParallelLineThreshold = 384U;
	const bool parallel = line_count >= kParallelLineThreshold && omp_get_max_threads() > 1;
	if (parallel) {
		const int max_threads = omp_get_max_threads();
		std::vector<PixelBounds> thread_bounds(static_cast<std::size_t>(max_threads));
		#pragma omp parallel for schedule(static)
		for (int line = 0; line < static_cast<int>(line_count); ++line) {
			const int tid = omp_get_thread_num();
			const std::size_t base = static_cast<std::size_t>(line) * 6U;
			detail3d::DrawWorldSegment(
				dst,
				nullptr,
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
				thread_bounds[static_cast<std::size_t>(tid)]);
		}
		MergeThreadBounds(bounds, thread_bounds);
	} else
#endif
	{
#if defined(__AVX2__) && defined(__FMA__)
		std::size_t line = 0U;
		alignas(32) float cx[8];
		alignas(32) float cy[8];
		alignas(32) float cz[8];
		alignas(32) float cw[8];
		for (; line + 4U <= line_count; line += 4U) {
			const float* s0 = segments + line * 6U;
			const float* s1 = segments + (line + 1U) * 6U;
			const float* s2 = segments + (line + 2U) * 6U;
			const float* s3 = segments + (line + 3U) * 6U;
			detail3d::TransformEightPositionsAvx2(
				view_proj16,
				s0 + 0, s0 + 3,
				s1 + 0, s1 + 3,
				s2 + 0, s2 + 3,
				s3 + 0, s3 + 3,
				cx, cy, cz, cw);
			for (int lane = 0; lane < 4; ++lane) {
				const int i0 = lane * 2;
				const int i1 = i0 + 1;
				detail3d::ClipVert a{};
				detail3d::ClipVert b{};
				a.x = cx[i0];
				a.y = cy[i0];
				a.z = cz[i0];
				a.w = cw[i0];
				b.x = cx[i1];
				b.y = cy[i1];
				b.z = cz[i1];
				b.w = cw[i1];
				detail3d::DrawWorldSegmentFromClip(
					dst,
					nullptr,
					width,
					height,
					a,
					b,
					detail3d::ComputeClipOutcode(a),
					detail3d::ComputeClipOutcode(b),
					line_color,
					line_width,
					bounds);
			}
		}
		for (; line < line_count; ++line) {
			const std::size_t base = line * 6U;
			detail3d::DrawWorldSegment(
				dst,
				nullptr,
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
#else
		for (std::size_t line = 0U; line < line_count; ++line) {
			const std::size_t base = line * 6U;
			detail3d::DrawWorldSegment(
				dst,
				nullptr,
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
#endif
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
 *
 * Depth-on uses the same 64×64 tile OpenMP path as world lines (no depth races).
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

	if (depth != nullptr && depth->Allocated()) {
		detail3d::Line3dDrawScratch& scratch = detail3d::GetLine3dDrawScratch();
		auto& segs = scratch.segs;
		segs.clear();
		segs.reserve(line_count);
		for (std::size_t line = 0U; line < line_count; ++line) {
			const std::size_t base = line * 6U;
			detail3d::ProjectedSeg seg{};
			seg.x0 = static_cast<int>(std::lround(segments[base + 0U]));
			seg.y0 = static_cast<int>(std::lround(segments[base + 1U]));
			seg.x1 = static_cast<int>(std::lround(segments[base + 3U]));
			seg.y1 = static_cast<int>(std::lround(segments[base + 4U]));
			if (seg.x0 == seg.x1 && seg.y0 == seg.y1) {
				continue;
			}
			seg.depth0 = segments[base + 2U];
			seg.depth1 = segments[base + 5U];
			seg.inv_w0 = 1.0f;
			seg.inv_w1 = 1.0f;
			segs.push_back(seg);
		}
		detail3d::RasterProjectedSegsDepth(framebuffer, depth, segs, line_color, line_width);
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
			nullptr,
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
