#pragma once

#include "engine/cpu_blend.hpp"
#include "engine/cpu_line_raster.hpp"
#include "engine/cpu_line_raster_3d.hpp"
#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#if defined(_OPENMP)
#include <omp.h>
#endif
#if defined(__AVX512F__) || defined(__AVX2__)
#include <immintrin.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace hyperlite::raster {

namespace detail3d {

/**
 * Screen-space triangle after project (pixel xy, z/w, 1/w) with flat color or atlas UVs.
 *
 * When atlas_rgba is non-null, the fill samples the atlas with perspective-correct UVs
 * (nearest, clamp). Flat-color draws leave atlas_rgba null and use `color`.
 */
struct ScreenTri {
	float x0 = 0.0f;
	float y0 = 0.0f;
	float zw0 = 0.0f;
	float iw0 = 1.0f;
	float u0 = 0.0f;
	float v0 = 0.0f;
	float x1 = 0.0f;
	float y1 = 0.0f;
	float zw1 = 0.0f;
	float iw1 = 1.0f;
	float u1 = 0.0f;
	float v1 = 0.0f;
	float x2 = 0.0f;
	float y2 = 0.0f;
	float zw2 = 0.0f;
	float iw2 = 1.0f;
	float u2 = 0.0f;
	float v2 = 0.0f;
	std::uint32_t color = 0U;
	/** RGBA8 atlas pixels; null = flat `color` fill. */
	const std::uint8_t* atlas_rgba = nullptr;
	int atlas_w = 0;
	int atlas_h = 0;
};

/**
 * Signed screen-space area * 2 (positive = CCW in pixel coords, y-down).
 *
 * OpenGL-front faces (CCW in NDC, y-up) project to CW / negative area after the
 * viewport Y flip used by ProjectToPixels. Cull keeps negative area when enabled.
 */
inline float ScreenSignedArea2(
	const float x0,
	const float y0,
	const float x1,
	const float y1,
	const float x2,
	const float y2) {
	return (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
}

/**
 * Inside-test for one clip plane (value >= 0 means inside).
 */
inline float PlaneValue(const ClipVert& v, const int plane) {
	switch (plane) {
	case kLeft:
		return v.x + v.w;
	case kRight:
		return v.w - v.x;
	case kBottom:
		return v.y + v.w;
	case kTop:
		return v.w - v.y;
	case kNear:
		return v.z + v.w;
	case kFar:
		return v.w - v.z;
	default:
		return 0.0f;
	}
}

/**
 * Sutherland–Hodgman clip of a convex polygon against one ±w plane.
 *
 * in_count/out_count are vertex counts; buffers must hold at least 16 verts.
 */
inline void ClipPolyAgainstPlane(
	const ClipVert* in_verts,
	const int in_count,
	ClipVert* out_verts,
	int& out_count,
	const int plane) {
	out_count = 0;
	if (in_count <= 0) {
		return;
	}
	const ClipVert* prev = &in_verts[in_count - 1];
	float prev_val = PlaneValue(*prev, plane);
	bool prev_in = prev_val >= 0.0f;
	for (int i = 0; i < in_count; ++i) {
		const ClipVert& curr = in_verts[i];
		const float curr_val = PlaneValue(curr, plane);
		const bool curr_in = curr_val >= 0.0f;
		if (curr_in != prev_in) {
			const float denom = prev_val - curr_val;
			const float t = (std::fabs(denom) < 1e-20f) ? 0.0f : (prev_val / denom);
			if (out_count < 16) {
				out_verts[out_count++] = LerpClip(*prev, curr, t);
			}
		}
		if (curr_in && out_count < 16) {
			out_verts[out_count++] = curr;
		}
		prev = &curr;
		prev_val = curr_val;
		prev_in = curr_in;
	}
}

/**
 * Clip a triangle in homogeneous clip space against the view frustum.
 *
 * Returns the output vertex count (0 if fully clipped). out_verts holds up to 16.
 * Near plane is mandatory; a clipped tri may become a quad/ngon (fan later).
 */
inline int ClipTriangleHomogeneous(const ClipVert& a, const ClipVert& b, const ClipVert& c, ClipVert* out_verts) {
	ClipVert buf_a[16];
	ClipVert buf_b[16];
	buf_a[0] = a;
	buf_a[1] = b;
	buf_a[2] = c;
	int count = 3;
	constexpr int kPlanes[6] = {kLeft, kRight, kBottom, kTop, kNear, kFar};
	ClipVert* src = buf_a;
	ClipVert* dst = buf_b;
	for (const int plane : kPlanes) {
		int out_count = 0;
		ClipPolyAgainstPlane(src, count, dst, out_count, plane);
		count = out_count;
		if (count < 3) {
			return 0;
		}
		ClipVert* tmp = src;
		src = dst;
		dst = tmp;
	}
	for (int i = 0; i < count; ++i) {
		out_verts[i] = src[i];
	}
	return count;
}

/**
 * Top-left edge flag for a CCW edge a→b (y-down pixel space).
 *
 * Top edge: horizontal going left (dy == 0 && dx < 0).
 * Left edge: strictly downward in screen y (dy > 0).
 * Shared edges: one tri owns top/left, the other uses strict > so no double-write / holes.
 */
inline bool IsTopLeftEdge(const float ax, const float ay, const float bx, const float by) {
	const float dy = by - ay;
	const float dx = bx - ax;
	return (dy > 0.0f) || (dy == 0.0f && dx < 0.0f);
}

/**
 * Edge function E(p) = (b - a) × (p - a); positive when p is to the left of a→b
 * in math coordinates (same sign as ScreenSignedArea2 for the third vertex).
 */
inline float EdgeFn(
	const float ax,
	const float ay,
	const float bx,
	const float by,
	const float px,
	const float py) {
	return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/**
 * Nearest-neighbor atlas sample with UV clamp to [0,1] over the full atlas.
 *
 * Returns packed RGBA. Out-of-range atlas dims yield 0 (skip).
 */
inline std::uint32_t SampleAtlasNearest(
	const std::uint8_t* rgba,
	const int atlas_w,
	const int atlas_h,
	float u,
	float v) {
	if (rgba == nullptr || atlas_w <= 0 || atlas_h <= 0) {
		return 0U;
	}
	if (u < 0.0f) {
		u = 0.0f;
	} else if (u > 1.0f) {
		u = 1.0f;
	}
	if (v < 0.0f) {
		v = 0.0f;
	} else if (v > 1.0f) {
		v = 1.0f;
	}
	int tx = static_cast<int>(u * static_cast<float>(atlas_w));
	int ty = static_cast<int>(v * static_cast<float>(atlas_h));
	if (tx >= atlas_w) {
		tx = atlas_w - 1;
	}
	if (ty >= atlas_h) {
		ty = atlas_h - 1;
	}
	if (tx < 0) {
		tx = 0;
	}
	if (ty < 0) {
		ty = 0;
	}
	const std::uint8_t* px =
		rgba + (static_cast<std::size_t>(ty) * static_cast<std::size_t>(atlas_w) + static_cast<std::size_t>(tx)) * 4U;
	return static_cast<std::uint32_t>(px[0]) |
		(static_cast<std::uint32_t>(px[1]) << 8U) |
		(static_cast<std::uint32_t>(px[2]) << 16U) |
		(static_cast<std::uint32_t>(px[3]) << 24U);
}

/**
 * Half-plane edge as E(p) = A*px + B*py + C (pixel-center coordinates).
 *
 * Matches EdgeFn(a→b, p): A = ay-by, B = bx-ax, C = -A*ax - B*ay.
 */
struct HalfEdgeCoef {
	float a = 0.0f;
	float b = 0.0f;
	float c = 0.0f;
	bool top_left = false;
};

/**
 * Build edge coefficients for oriented edge a→b (same sign as EdgeFn).
 */
inline HalfEdgeCoef MakeHalfEdge(
	const float ax,
	const float ay,
	const float bx,
	const float by) {
	HalfEdgeCoef e{};
	e.a = ay - by; // dE/dpx
	e.b = bx - ax; // dE/dpy
	e.c = -e.a * ax - e.b * ay;
	e.top_left = IsTopLeftEdge(ax, ay, bx, by);
	return e;
}

/**
 * Evaluate half-edge at a pixel center.
 */
inline float EvalHalfEdge(const HalfEdgeCoef& e, const float px, const float py) {
	return e.a * px + e.b * py + e.c;
}

/**
 * Maximum of E over an inclusive pixel-center AABB (axis-aligned).
 *
 * Used for trivial reject: if max is still outside the half-plane, skip the tile.
 */
inline float HalfEdgeMaxOverBox(
	const HalfEdgeCoef& e,
	const float px_lo,
	const float px_hi,
	const float py_lo,
	const float py_hi) {
	const float px = (e.a > 0.0f) ? px_hi : px_lo;
	const float py = (e.b > 0.0f) ? py_hi : py_lo;
	return EvalHalfEdge(e, px, py);
}

/**
 * Minimum of E over an inclusive pixel-center AABB (axis-aligned).
 *
 * Used for trivial accept: if min is still inside the half-plane, the box is covered.
 */
inline float HalfEdgeMinOverBox(
	const HalfEdgeCoef& e,
	const float px_lo,
	const float px_hi,
	const float py_lo,
	const float py_hi) {
	const float px = (e.a > 0.0f) ? px_lo : px_hi;
	const float py = (e.b > 0.0f) ? py_lo : py_hi;
	return EvalHalfEdge(e, px, py);
}

/**
 * True when the pixel-center box is entirely outside one half-plane (top-left aware).
 */
inline bool HalfEdgeBoxTrivialOut(
	const HalfEdgeCoef& e,
	const float px_lo,
	const float px_hi,
	const float py_lo,
	const float py_hi) {
	const float e_max = HalfEdgeMaxOverBox(e, px_lo, px_hi, py_lo, py_hi);
	// Top-left: inside if E >= 0 → out if E_max < 0.
	// Non-top-left: inside if E > 0 → out if E_max <= 0.
	return e.top_left ? (e_max < 0.0f) : (e_max <= 0.0f);
}

/**
 * True when the pixel-center box is entirely inside one half-plane (top-left aware).
 */
inline bool HalfEdgeBoxTrivialIn(
	const HalfEdgeCoef& e,
	const float px_lo,
	const float px_hi,
	const float py_lo,
	const float py_hi) {
	const float e_min = HalfEdgeMinOverBox(e, px_lo, px_hi, py_lo, py_hi);
	return e.top_left ? (e_min >= 0.0f) : (e_min > 0.0f);
}

/**
 * Coverage test matching the scalar top-left rule.
 */
inline bool InsideHalfEdge(const float w, const bool top_left) {
	if (w > 0.0f) {
		return true;
	}
	if (w < 0.0f) {
		return false;
	}
	return top_left;
}

/**
 * Write one covered pixel (flat or textured) with depth + dirty tracking.
 *
 * Shared by scalar remainder and non-SIMD paths so conventions stay identical.
 */
inline void ShadeAndStoreTriPixel(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int x,
	const int y,
	const std::size_t stride,
	const float z_win,
	std::uint32_t packed,
	const bool textured,
	const ScreenTri& tri,
	const float b0,
	const float b1,
	const float b2,
	const float uw0,
	const float uw1,
	const float uw2,
	const float vw0,
	const float vw1,
	const float vw2,
	bool& touched,
	int& dirty_x0,
	int& dirty_y0,
	int& dirty_x1,
	int& dirty_y1) {
	if (textured) {
		const float iw = b0 * tri.iw0 + b1 * tri.iw1 + b2 * tri.iw2;
		if (std::fabs(iw) < 1e-20f) {
			return;
		}
		const float uw = b0 * uw0 + b1 * uw1 + b2 * uw2;
		const float vw = b0 * vw0 + b1 * vw1 + b2 * vw2;
		packed = SampleAtlasNearest(tri.atlas_rgba, tri.atlas_w, tri.atlas_h, uw / iw, vw / iw);
	}

	const std::uint32_t sa = packed >> 24U;
	if (sa == 0U) {
		return; // fully transparent texel / color: skip (no depth write)
	}
	const bool opaque = sa == 255U;
	const bool write_depth = opaque; // translucent: test only; no depth write

	if (depth != nullptr) {
		if (write_depth) {
			if (!depth->TestAndWrite(x, y, z_win)) {
				return;
			}
		} else {
			// Translucent: depth-test only (discard if behind); do not write depth.
			if (z_win > depth->At(x, y)) {
				return;
			}
		}
	}

	std::uint32_t* ptr = dst + (static_cast<std::size_t>(y) * stride) + static_cast<std::size_t>(x);
	if (opaque) {
		*ptr = packed;
	} else {
		StorePixel(ptr, packed);
	}

	touched = true;
	dirty_x0 = std::min(dirty_x0, x);
	dirty_y0 = std::min(dirty_y0, y);
	dirty_x1 = std::max(dirty_x1, x);
	dirty_y1 = std::max(dirty_y1, y);
}

#if defined(__AVX512F__) && defined(__AVX512VL__)
/**
 * AVX-512VL: 8-wide opaque flat fill using k-masks on ymm (no zmm).
 *
 * Same lane width as AVX2; mask registers avoid movemask/cast round-trips.
 * 16-wide zmm fill was measured slower on this VM (see docs/simd-tri-fill.md).
 */
inline int FillOpaqueFlatBlock8Vl(
	std::uint32_t* row_dst,
	float* row_depth,
	const __m256 w0,
	const __m256 w1,
	const __m256 w2,
	const __m256 z_win,
	const __m256i color_i,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth) {
	const __m256 zero = _mm256_setzero_ps();
	const __mmask8 m0 =
		tl0 ? _mm256_cmp_ps_mask(w0, zero, _CMP_GE_OQ) : _mm256_cmp_ps_mask(w0, zero, _CMP_GT_OQ);
	const __mmask8 m1 =
		tl1 ? _mm256_cmp_ps_mask(w1, zero, _CMP_GE_OQ) : _mm256_cmp_ps_mask(w1, zero, _CMP_GT_OQ);
	const __mmask8 m2 =
		tl2 ? _mm256_cmp_ps_mask(w2, zero, _CMP_GE_OQ) : _mm256_cmp_ps_mask(w2, zero, _CMP_GT_OQ);
	__mmask8 cov = static_cast<__mmask8>(m0 & m1 & m2);
	if (cov == 0) {
		return 0;
	}
	if (has_depth && row_depth != nullptr) {
		const __m256 d = _mm256_loadu_ps(row_depth);
		const __mmask8 pass = _mm256_cmp_ps_mask(z_win, d, _CMP_LE_OQ);
		cov = static_cast<__mmask8>(cov & pass);
		if (cov == 0) {
			return 0;
		}
		_mm256_mask_storeu_ps(row_depth, cov, z_win);
	}
	_mm256_mask_storeu_epi32(reinterpret_cast<int*>(row_dst), cov, color_i);
	return static_cast<int>(cov);
}
#endif // __AVX512F__ && __AVX512VL__

#if defined(__AVX2__)
/**
 * AVX2: 8-wide opaque flat fill (coverage + depth test/write + color store).
 *
 * Returns movemask of pixels actually written (for dirty bounds).
 */
inline int FillOpaqueFlatBlock8(
	std::uint32_t* row_dst,
	float* row_depth,
	const __m256 w0,
	const __m256 w1,
	const __m256 w2,
	const __m256 z_win,
	const __m256i color_i,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth) {
	const __m256 zero = _mm256_setzero_ps();
	// Top-left: E >= 0; else E > 0.
	__m256 c0 = tl0 ? _mm256_cmp_ps(w0, zero, _CMP_GE_OQ) : _mm256_cmp_ps(w0, zero, _CMP_GT_OQ);
	__m256 c1 = tl1 ? _mm256_cmp_ps(w1, zero, _CMP_GE_OQ) : _mm256_cmp_ps(w1, zero, _CMP_GT_OQ);
	__m256 c2 = tl2 ? _mm256_cmp_ps(w2, zero, _CMP_GE_OQ) : _mm256_cmp_ps(w2, zero, _CMP_GT_OQ);
	__m256 cov = _mm256_and_ps(_mm256_and_ps(c0, c1), c2);
	int cov_bits = _mm256_movemask_ps(cov);
	if (cov_bits == 0) {
		return 0;
	}

	__m256i store_mask = _mm256_castps_si256(cov);
	if (has_depth && row_depth != nullptr) {
		const __m256 d = _mm256_loadu_ps(row_depth);
		const __m256 pass = _mm256_cmp_ps(z_win, d, _CMP_LE_OQ);
		const __m256 write = _mm256_and_ps(cov, pass);
		store_mask = _mm256_castps_si256(write);
		const int write_bits = _mm256_movemask_ps(write);
		if (write_bits == 0) {
			return 0;
		}
		_mm256_maskstore_ps(row_depth, store_mask, z_win);
	}
	_mm256_maskstore_epi32(reinterpret_cast<int*>(row_dst), store_mask, color_i);
	return _mm256_movemask_ps(_mm256_castsi256_ps(store_mask));
}
#elif defined(__SSE4_2__)
/**
 * SSE4.2: 4-wide opaque flat fill (coverage + depth + color).
 */
inline int FillOpaqueFlatBlock4(
	std::uint32_t* row_dst,
	float* row_depth,
	const __m128 w0,
	const __m128 w1,
	const __m128 w2,
	const __m128 z_win,
	const __m128i color_i,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth) {
	const __m128 zero = _mm_setzero_ps();
	__m128 c0 = tl0 ? _mm_cmpge_ps(w0, zero) : _mm_cmpgt_ps(w0, zero);
	__m128 c1 = tl1 ? _mm_cmpge_ps(w1, zero) : _mm_cmpgt_ps(w1, zero);
	__m128 c2 = tl2 ? _mm_cmpge_ps(w2, zero) : _mm_cmpgt_ps(w2, zero);
	__m128 cov = _mm_and_ps(_mm_and_ps(c0, c1), c2);
	int cov_bits = _mm_movemask_ps(cov);
	if (cov_bits == 0) {
		return 0;
	}

	__m128 write = cov;
	if (has_depth && row_depth != nullptr) {
		const __m128 d = _mm_loadu_ps(row_depth);
		const __m128 pass = _mm_cmple_ps(z_win, d);
		write = _mm_and_ps(cov, pass);
		const int write_bits = _mm_movemask_ps(write);
		if (write_bits == 0) {
			return 0;
		}
		// Manual masked depth store (no SSE maskstore).
		alignas(16) float z_tmp[4];
		alignas(16) float d_tmp[4];
		_mm_store_ps(z_tmp, z_win);
		_mm_store_ps(d_tmp, d);
		for (int i = 0; i < 4; ++i) {
			if ((write_bits & (1 << i)) != 0) {
				d_tmp[i] = z_tmp[i];
			}
		}
		_mm_storeu_ps(row_depth, _mm_load_ps(d_tmp));
	}
	const int write_bits = _mm_movemask_ps(write);
	alignas(16) std::uint32_t c_tmp[4];
	_mm_store_si128(reinterpret_cast<__m128i*>(c_tmp), color_i);
	for (int i = 0; i < 4; ++i) {
		if ((write_bits & (1 << i)) != 0) {
			row_dst[i] = c_tmp[i];
		}
	}
	return write_bits;
}
#endif

/**
 * Expand dirty AABB from a block write bitmask starting at pixel x.
 */
inline void ExpandDirtyFromMask(
	const int x0,
	const int y,
	const int mask,
	const int width_bits,
	bool& touched,
	int& dirty_x0,
	int& dirty_y0,
	int& dirty_x1,
	int& dirty_y1) {
	if (mask == 0) {
		return;
	}
	(void)width_bits;
	touched = true;
	dirty_y0 = std::min(dirty_y0, y);
	dirty_y1 = std::max(dirty_y1, y);
	// Bitscan over written lanes (sparse edge blocks benefit most).
	unsigned m = static_cast<unsigned>(mask);
	while (m != 0U) {
		const int i = __builtin_ctz(m);
		const int x = x0 + i;
		dirty_x0 = std::min(dirty_x0, x);
		dirty_x1 = std::max(dirty_x1, x);
		m &= m - 1U;
	}
}

/**
 * Raster one screen-space triangle into a tile AABB using half-space coverage.
 *
 * Expects CCW winding in pixel space (caller reorders). Top-left fill rule.
 * Depth: interpolate z/w (and 1/w) in screen space; window z = ndc*0.5+0.5.
 * Opaque (a=255): depth test+write. Translucent: src-over, no depth write.
 * Textured: perspective-correct UV (u/w,v/w,1/w), nearest clamp sample; a==0 skips.
 *
 * Hot path: incremental edge/attribute setup, trivial tile reject vs half-planes,
 * then opaque flat blocks: AVX-512VL 8-wide k-masks when available, else AVX2 /
 * SSE4.2, with scalar remainder. Textured stays scalar (gather SIMD lost here).
 */
inline void RasterScreenTriTile(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const int tile_x0,
	const int tile_y0,
	const int tile_x1,
	const int tile_y1,
	ScreenTri tri,
	PixelBounds& bounds) {
	(void)height;
	// Ensure CCW for half-space signs / top-left rule.
	float area2 = ScreenSignedArea2(tri.x0, tri.y0, tri.x1, tri.y1, tri.x2, tri.y2);
	if (area2 < 0.0f) {
		std::swap(tri.x1, tri.x2);
		std::swap(tri.y1, tri.y2);
		std::swap(tri.zw1, tri.zw2);
		std::swap(tri.iw1, tri.iw2);
		std::swap(tri.u1, tri.u2);
		std::swap(tri.v1, tri.v2);
		area2 = -area2;
	}
	if (area2 < 1e-8f) {
		return;
	}

	const float min_x_f = std::min({tri.x0, tri.x1, tri.x2});
	const float min_y_f = std::min({tri.y0, tri.y1, tri.y2});
	const float max_x_f = std::max({tri.x0, tri.x1, tri.x2});
	const float max_y_f = std::max({tri.y0, tri.y1, tri.y2});

	int ix0 = std::max(tile_x0, static_cast<int>(std::floor(min_x_f)));
	int iy0 = std::max(tile_y0, static_cast<int>(std::floor(min_y_f)));
	int ix1 = std::min(tile_x1, static_cast<int>(std::ceil(max_x_f)));
	int iy1 = std::min(tile_y1, static_cast<int>(std::ceil(max_y_f)));
	ix0 = std::max(ix0, 0);
	iy0 = std::max(iy0, 0);
	ix1 = std::min(ix1, width);
	iy1 = std::min(iy1, height);
	if (ix0 >= ix1 || iy0 >= iy1) {
		return;
	}

	// Edge 0 = v1→v2 (bary w0), edge 1 = v2→v0, edge 2 = v0→v1.
	const HalfEdgeCoef e0 = MakeHalfEdge(tri.x1, tri.y1, tri.x2, tri.y2);
	const HalfEdgeCoef e1 = MakeHalfEdge(tri.x2, tri.y2, tri.x0, tri.y0);
	const HalfEdgeCoef e2 = MakeHalfEdge(tri.x0, tri.y0, tri.x1, tri.y1);

	const float px_lo = static_cast<float>(ix0) + 0.5f;
	const float px_hi = static_cast<float>(ix1 - 1) + 0.5f;
	const float py_lo = static_cast<float>(iy0) + 0.5f;
	const float py_hi = static_cast<float>(iy1 - 1) + 0.5f;
	// Trivial reject: tile AABB vs half-plane signs (no Hi-Z).
	if (HalfEdgeBoxTrivialOut(e0, px_lo, px_hi, py_lo, py_hi) ||
		HalfEdgeBoxTrivialOut(e1, px_lo, px_hi, py_lo, py_hi) ||
		HalfEdgeBoxTrivialOut(e2, px_lo, px_hi, py_lo, py_hi)) {
		return;
	}

	const float inv_area = 1.0f / area2;
	const bool textured = tri.atlas_rgba != nullptr && tri.atlas_w > 0 && tri.atlas_h > 0;
	const std::uint32_t flat_packed = tri.color;
	const std::uint32_t flat_a = flat_packed >> 24U;
	const bool opaque_flat = !textured && flat_a == 255U;
	const bool has_depth = depth != nullptr && depth->Allocated();
	float* depth_base = has_depth ? depth->Data() : nullptr;
	const std::size_t stride = static_cast<std::size_t>(width);

	// Hoist perspective UV numerators (u/w, v/w) for textured path.
	const float uw0 = tri.u0 * tri.iw0;
	const float uw1 = tri.u1 * tri.iw1;
	const float uw2 = tri.u2 * tri.iw2;
	const float vw0 = tri.v0 * tri.iw0;
	const float vw1 = tri.v1 * tri.iw1;
	const float vw2 = tri.v2 * tri.iw2;

	// Screen-linear z/w → window depth: z_win = 0.5 * zw + 0.5.
	// Incremental in x: dz_win = 0.5 * inv_area * (A0*zw0 + A1*zw1 + A2*zw2).
	const float dzw_dx = inv_area * (e0.a * tri.zw0 + e1.a * tri.zw1 + e2.a * tri.zw2);
	const float dzw_dy = inv_area * (e0.b * tri.zw0 + e1.b * tri.zw1 + e2.b * tri.zw2);
	const float dz_win_dx = 0.5f * dzw_dx;
	const float dz_win_dy = 0.5f * dzw_dy;

	bool touched = false;
	int dirty_x0 = ix1;
	int dirty_y0 = iy1;
	int dirty_x1 = ix0;
	int dirty_y1 = iy0;

	// Row-start edge + depth at (ix0+0.5, iy0+0.5); step by B each row, A each column.
	float w0_row = EvalHalfEdge(e0, px_lo, py_lo);
	float w1_row = EvalHalfEdge(e1, px_lo, py_lo);
	float w2_row = EvalHalfEdge(e2, px_lo, py_lo);
	const float zw_row0 =
		(w0_row * tri.zw0 + w1_row * tri.zw1 + w2_row * tri.zw2) * inv_area;
	float z_win_row = NdcToWindowDepth(zw_row0);

	// Convex half-spaces: if the AABB is inside all three edges, skip coverage tests.
	const bool box_fully_covered =
		HalfEdgeBoxTrivialIn(e0, px_lo, px_hi, py_lo, py_hi) &&
		HalfEdgeBoxTrivialIn(e1, px_lo, px_hi, py_lo, py_hi) &&
		HalfEdgeBoxTrivialIn(e2, px_lo, px_hi, py_lo, py_hi);

	if (opaque_flat) {
#if defined(__AVX2__)
		const __m256i color_i = _mm256_set1_epi32(static_cast<int>(flat_packed));
		const __m256 a0_v = _mm256_set1_ps(e0.a);
		const __m256 a1_v = _mm256_set1_ps(e1.a);
		const __m256 a2_v = _mm256_set1_ps(e2.a);
		const __m256 dz_dx_v = _mm256_set1_ps(dz_win_dx);
		const __m256 lane = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
		const __m256 a0_lane = _mm256_mul_ps(a0_v, lane);
		const __m256 a1_lane = _mm256_mul_ps(a1_v, lane);
		const __m256 a2_lane = _mm256_mul_ps(a2_v, lane);
		const __m256 dz_lane = _mm256_mul_ps(dz_dx_v, lane);
		constexpr int kBlock = 8;
#elif defined(__SSE4_2__)
		const __m128i color_i = _mm_set1_epi32(static_cast<int>(flat_packed));
		const __m128 a0_v = _mm_set1_ps(e0.a);
		const __m128 a1_v = _mm_set1_ps(e1.a);
		const __m128 a2_v = _mm_set1_ps(e2.a);
		const __m128 dz_dx_v = _mm_set1_ps(dz_win_dx);
		const __m128 lane = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);
		const __m128 a0_lane = _mm_mul_ps(a0_v, lane);
		const __m128 a1_lane = _mm_mul_ps(a1_v, lane);
		const __m128 a2_lane = _mm_mul_ps(a2_v, lane);
		const __m128 dz_lane = _mm_mul_ps(dz_dx_v, lane);
		constexpr int kBlock = 4;
#endif
		for (int y = iy0; y < iy1; ++y) {
			float w0 = w0_row;
			float w1 = w1_row;
			float w2 = w2_row;
			float z_win = z_win_row;
			std::uint32_t* row_dst = dst + static_cast<std::size_t>(y) * stride;
			float* row_depth = has_depth ? (depth_base + static_cast<std::size_t>(y) * stride) : nullptr;

			int x = ix0;
#if defined(__AVX2__) || defined(__SSE4_2__)
			if (box_fully_covered) {
				// Interior tile: depth + color only (no edge compares).
				for (; x + kBlock <= ix1; x += kBlock) {
#if defined(__AVX2__)
					const __m256 zv = _mm256_add_ps(_mm256_set1_ps(z_win), dz_lane);
					int written = 0xFF;
					if (has_depth) {
						const __m256 d = _mm256_loadu_ps(row_depth + x);
						const __m256 pass = _mm256_cmp_ps(zv, d, _CMP_LE_OQ);
						const __m256i store_mask = _mm256_castps_si256(pass);
						written = _mm256_movemask_ps(pass);
						if (written != 0) {
							_mm256_maskstore_ps(row_depth + x, store_mask, zv);
							_mm256_maskstore_epi32(reinterpret_cast<int*>(row_dst + x), store_mask, color_i);
						}
					} else {
						_mm256_storeu_si256(reinterpret_cast<__m256i*>(row_dst + x), color_i);
					}
#elif defined(__SSE4_2__)
					const __m128 zv = _mm_add_ps(_mm_set1_ps(z_win), dz_lane);
					int written = 0xF;
					if (has_depth) {
						const __m128 d = _mm_loadu_ps(row_depth + x);
						const __m128 pass = _mm_cmple_ps(zv, d);
						written = _mm_movemask_ps(pass);
						if (written != 0) {
							alignas(16) float z_tmp[4];
							alignas(16) float d_tmp[4];
							_mm_store_ps(z_tmp, zv);
							_mm_store_ps(d_tmp, d);
							for (int i = 0; i < 4; ++i) {
								if ((written & (1 << i)) != 0) {
									d_tmp[i] = z_tmp[i];
									row_dst[x + i] = flat_packed;
								}
							}
							_mm_storeu_ps(row_depth + x, _mm_load_ps(d_tmp));
						}
					} else {
						_mm_storeu_si128(reinterpret_cast<__m128i*>(row_dst + x), color_i);
					}
#endif
					ExpandDirtyFromMask(x, y, written, kBlock, touched, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
					z_win += dz_win_dx * static_cast<float>(kBlock);
				}
			} else {
				for (; x + kBlock <= ix1; x += kBlock) {
#if defined(__AVX2__)
					const __m256 w0v = _mm256_add_ps(_mm256_set1_ps(w0), a0_lane);
					const __m256 w1v = _mm256_add_ps(_mm256_set1_ps(w1), a1_lane);
					const __m256 w2v = _mm256_add_ps(_mm256_set1_ps(w2), a2_lane);
					const __m256 zv = _mm256_add_ps(_mm256_set1_ps(z_win), dz_lane);
#if defined(__AVX512F__) && defined(__AVX512VL__)
					const int written = FillOpaqueFlatBlock8Vl(
						row_dst + x,
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						color_i,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth);
#else
					const int written = FillOpaqueFlatBlock8(
						row_dst + x,
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						color_i,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth);
#endif
#elif defined(__SSE4_2__)
					const __m128 w0v = _mm_add_ps(_mm_set1_ps(w0), a0_lane);
					const __m128 w1v = _mm_add_ps(_mm_set1_ps(w1), a1_lane);
					const __m128 w2v = _mm_add_ps(_mm_set1_ps(w2), a2_lane);
					const __m128 zv = _mm_add_ps(_mm_set1_ps(z_win), dz_lane);
					const int written = FillOpaqueFlatBlock4(
						row_dst + x,
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						color_i,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth);
#endif
					ExpandDirtyFromMask(x, y, written, kBlock, touched, dirty_x0, dirty_y0, dirty_x1, dirty_y1);
					w0 += e0.a * static_cast<float>(kBlock);
					w1 += e1.a * static_cast<float>(kBlock);
					w2 += e2.a * static_cast<float>(kBlock);
					z_win += dz_win_dx * static_cast<float>(kBlock);
				}
			}
#endif
			for (; x < ix1; ++x) {
				const bool cover = box_fully_covered ||
					(InsideHalfEdge(w0, e0.top_left) && InsideHalfEdge(w1, e1.top_left) &&
						InsideHalfEdge(w2, e2.top_left));
				if (cover) {
					if (has_depth) {
						float& slot = row_depth[x];
						if (z_win <= slot) {
							slot = z_win;
							row_dst[x] = flat_packed;
							touched = true;
							dirty_x0 = std::min(dirty_x0, x);
							dirty_y0 = std::min(dirty_y0, y);
							dirty_x1 = std::max(dirty_x1, x);
							dirty_y1 = std::max(dirty_y1, y);
						}
					} else {
						row_dst[x] = flat_packed;
						touched = true;
						dirty_x0 = std::min(dirty_x0, x);
						dirty_y0 = std::min(dirty_y0, y);
						dirty_x1 = std::max(dirty_x1, x);
						dirty_y1 = std::max(dirty_y1, y);
					}
				}
				if (!box_fully_covered) {
					w0 += e0.a;
					w1 += e1.a;
					w2 += e2.a;
				}
				z_win += dz_win_dx;
			}

			w0_row += e0.b;
			w1_row += e1.b;
			w2_row += e2.b;
			z_win_row += dz_win_dy;
		}
	} else if (textured) {
		// Textured: incremental barycentrics + u/w,v/w,1/w; opaque texels use fast depth write.
		const float db0_dx = e0.a * inv_area;
		const float db1_dx = e1.a * inv_area;
		const float db2_dx = e2.a * inv_area;
		const float db0_dy = e0.b * inv_area;
		const float db1_dy = e1.b * inv_area;
		const float db2_dy = e2.b * inv_area;
		float b0_row = w0_row * inv_area;
		float b1_row = w1_row * inv_area;
		float b2_row = w2_row * inv_area;
		const float diw_dx = db0_dx * tri.iw0 + db1_dx * tri.iw1 + db2_dx * tri.iw2;
		const float diw_dy = db0_dy * tri.iw0 + db1_dy * tri.iw1 + db2_dy * tri.iw2;
		const float duw_dx = db0_dx * uw0 + db1_dx * uw1 + db2_dx * uw2;
		const float duw_dy = db0_dy * uw0 + db1_dy * uw1 + db2_dy * uw2;
		const float dvw_dx = db0_dx * vw0 + db1_dx * vw1 + db2_dx * vw2;
		const float dvw_dy = db0_dy * vw0 + db1_dy * vw1 + db2_dy * vw2;
		float iw_row = b0_row * tri.iw0 + b1_row * tri.iw1 + b2_row * tri.iw2;
		float uw_row = b0_row * uw0 + b1_row * uw1 + b2_row * uw2;
		float vw_row = b0_row * vw0 + b1_row * vw1 + b2_row * vw2;

		for (int y = iy0; y < iy1; ++y) {
			float w0 = w0_row;
			float w1 = w1_row;
			float w2 = w2_row;
			float z_win = z_win_row;
			float iw = iw_row;
			float uw = uw_row;
			float vw = vw_row;
			std::uint32_t* row_dst = dst + static_cast<std::size_t>(y) * stride;
			float* row_depth = has_depth ? (depth_base + static_cast<std::size_t>(y) * stride) : nullptr;

			for (int x = ix0; x < ix1; ++x) {
				const bool cover = box_fully_covered ||
					(InsideHalfEdge(w0, e0.top_left) && InsideHalfEdge(w1, e1.top_left) &&
						InsideHalfEdge(w2, e2.top_left));
				if (cover && std::fabs(iw) >= 1e-20f) {
					const std::uint32_t packed =
						SampleAtlasNearest(tri.atlas_rgba, tri.atlas_w, tri.atlas_h, uw / iw, vw / iw);
					const std::uint32_t sa = packed >> 24U;
					if (sa != 0U) {
						const bool opaque = sa == 255U;
						bool pass = true;
						if (has_depth) {
							if (opaque) {
								float& slot = row_depth[x];
								if (z_win <= slot) {
									slot = z_win;
								} else {
									pass = false;
								}
							} else if (z_win > row_depth[x]) {
								pass = false;
							}
						}
						if (pass) {
							if (opaque) {
								row_dst[x] = packed;
							} else {
								StorePixel(row_dst + x, packed);
							}
							touched = true;
							dirty_x0 = std::min(dirty_x0, x);
							dirty_y0 = std::min(dirty_y0, y);
							dirty_x1 = std::max(dirty_x1, x);
							dirty_y1 = std::max(dirty_y1, y);
						}
					}
				}
				if (!box_fully_covered) {
					w0 += e0.a;
					w1 += e1.a;
					w2 += e2.a;
				}
				z_win += dz_win_dx;
				iw += diw_dx;
				uw += duw_dx;
				vw += dvw_dx;
			}
			w0_row += e0.b;
			w1_row += e1.b;
			w2_row += e2.b;
			z_win_row += dz_win_dy;
			iw_row += diw_dy;
			uw_row += duw_dy;
			vw_row += dvw_dy;
		}
	} else {
		// Translucent flat: incremental edges + src-over (no depth write).
		for (int y = iy0; y < iy1; ++y) {
			float w0 = w0_row;
			float w1 = w1_row;
			float w2 = w2_row;
			float z_win = z_win_row;
			for (int x = ix0; x < ix1; ++x) {
				if (InsideHalfEdge(w0, e0.top_left) && InsideHalfEdge(w1, e1.top_left) &&
					InsideHalfEdge(w2, e2.top_left)) {
					ShadeAndStoreTriPixel(
						dst,
						depth,
						x,
						y,
						stride,
						z_win,
						flat_packed,
						false,
						tri,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						touched,
						dirty_x0,
						dirty_y0,
						dirty_x1,
						dirty_y1);
				}
				w0 += e0.a;
				w1 += e1.a;
				w2 += e2.a;
				z_win += dz_win_dx;
			}
			w0_row += e0.b;
			w1_row += e1.b;
			w2_row += e2.b;
			z_win_row += dz_win_dy;
		}
	}

	if (touched) {
		bounds.Expand(dirty_x0, dirty_y0);
		bounds.Expand(dirty_x1, dirty_y1);
	}
}

/**
 * Append a projected screen tri if it survives backface cull (when enabled).
 *
 * cull_backfaces: discard non-negative screen area (CCW / degenerate in y-down).
 * Keeps OpenGL-front (CW after Y flip).
 */
inline void TryAppendScreenTri(
	std::vector<ScreenTri>& out,
	float x0,
	float y0,
	float zw0,
	float iw0,
	float u0,
	float v0,
	float x1,
	float y1,
	float zw1,
	float iw1,
	float u1,
	float v1,
	float x2,
	float y2,
	float zw2,
	float iw2,
	float u2,
	float v2,
	const std::uint32_t color,
	const bool cull_backfaces,
	const std::uint8_t* atlas_rgba = nullptr,
	const int atlas_w = 0,
	const int atlas_h = 0) {
	const float area = ScreenSignedArea2(x0, y0, x1, y1, x2, y2);
	if (cull_backfaces) {
		// Keep OpenGL-front (negative area after viewport Y flip); drop CCW/zero.
		if (area >= 0.0f) {
			return;
		}
	} else if (std::fabs(area) < 1e-8f) {
		return;
	}
	ScreenTri tri{};
	tri.x0 = x0;
	tri.y0 = y0;
	tri.zw0 = zw0;
	tri.iw0 = iw0;
	tri.u0 = u0;
	tri.v0 = v0;
	tri.x1 = x1;
	tri.y1 = y1;
	tri.zw1 = zw1;
	tri.iw1 = iw1;
	tri.u1 = u1;
	tri.v1 = v1;
	tri.x2 = x2;
	tri.y2 = y2;
	tri.zw2 = zw2;
	tri.iw2 = iw2;
	tri.u2 = u2;
	tri.v2 = v2;
	tri.color = color;
	tri.atlas_rgba = atlas_rgba;
	tri.atlas_w = atlas_w;
	tri.atlas_h = atlas_h;
	out.push_back(tri);
}

/**
 * Project a clip-space polygon fan to screen tris (UVs come from ClipVert).
 */
inline void ProjectFanToScreen(
	std::vector<ScreenTri>& out,
	const ClipVert* verts,
	const int count,
	const int width,
	const int height,
	const std::uint32_t color,
	const bool cull_backfaces,
	const std::uint8_t* atlas_rgba = nullptr,
	const int atlas_w = 0,
	const int atlas_h = 0) {
	if (count < 3) {
		return;
	}
	float px[16];
	float py[16];
	float zw[16];
	float iw[16];
	float uu[16];
	float vv[16];
	for (int i = 0; i < count; ++i) {
		if (!ProjectToPixels(verts[i], width, height, px[i], py[i], zw[i], iw[i])) {
			return;
		}
		uu[i] = verts[i].u;
		vv[i] = verts[i].v;
	}
	for (int i = 1; i + 1 < count; ++i) {
		TryAppendScreenTri(
			out,
			px[0], py[0], zw[0], iw[0], uu[0], vv[0],
			px[i], py[i], zw[i], iw[i], uu[i], vv[i],
			px[i + 1], py[i + 1], zw[i + 1], iw[i + 1], uu[i + 1], vv[i + 1],
			color,
			cull_backfaces,
			atlas_rgba,
			atlas_w,
			atlas_h);
	}
}

/**
 * Transform + clip + project one world-space triangle into screen tris (0..N).
 *
 * Optional per-vertex UVs + atlas enable Layer 2.1 textured fill; defaults keep flat color.
 */
inline void EmitWorldTri(
	std::vector<ScreenTri>& out,
	const float* view_proj,
	const float x0,
	const float y0,
	const float z0,
	const float x1,
	const float y1,
	const float z1,
	const float x2,
	const float y2,
	const float z2,
	const int width,
	const int height,
	const std::uint32_t color,
	const bool cull_backfaces,
	const float u0 = 0.0f,
	const float v0 = 0.0f,
	const float u1 = 0.0f,
	const float v1 = 0.0f,
	const float u2 = 0.0f,
	const float v2 = 0.0f,
	const std::uint8_t* atlas_rgba = nullptr,
	const int atlas_w = 0,
	const int atlas_h = 0) {
	ClipVert a = MulViewProj(view_proj, x0, y0, z0);
	ClipVert b = MulViewProj(view_proj, x1, y1, z1);
	ClipVert c = MulViewProj(view_proj, x2, y2, z2);
	a.u = u0;
	a.v = v0;
	b.u = u1;
	b.v = v1;
	c.u = u2;
	c.v = v2;
	ClipVert clipped[16];
	const int n = ClipTriangleHomogeneous(a, b, c, clipped);
	ProjectFanToScreen(out, clipped, n, width, height, color, cull_backfaces, atlas_rgba, atlas_w, atlas_h);
}

/**
 * Emit one already-projected screen-space triangle (pixel xy + NDC z).
 */
inline void EmitScreenTri(
	std::vector<ScreenTri>& out,
	const float x0,
	const float y0,
	const float z_ndc0,
	const float x1,
	const float y1,
	const float z_ndc1,
	const float x2,
	const float y2,
	const float z_ndc2,
	const std::uint32_t color,
	const bool cull_backfaces) {
	TryAppendScreenTri(
		out,
		x0, y0, z_ndc0, 1.0f, 0.0f, 0.0f,
		x1, y1, z_ndc1, 1.0f, 0.0f, 0.0f,
		x2, y2, z_ndc2, 1.0f, 0.0f, 0.0f,
		color,
		cull_backfaces);
}

/**
 * Bin screen tris into 64×64 tiles and raster (OpenMP over tiles — each tile owns pixels).
 */
inline void RasterScreenTrisTiled(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const std::vector<ScreenTri>& tris) {
	if (tris.empty()) {
		return;
	}
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	constexpr int kTile = FrameBuffer::kDirtyTileSize;
	const int tiles_x = std::max(1, (width + kTile - 1) / kTile);
	const int tiles_y = std::max(1, (height + kTile - 1) / kTile);
	const int tile_count = tiles_x * tiles_y;

	std::vector<std::vector<std::uint32_t>> bins(static_cast<std::size_t>(tile_count));
	PixelBounds global_bounds{};

	for (std::uint32_t ti = 0U; ti < static_cast<std::uint32_t>(tris.size()); ++ti) {
		const ScreenTri& t = tris[ti];
		const float min_x = std::min({t.x0, t.x1, t.x2});
		const float min_y = std::min({t.y0, t.y1, t.y2});
		const float max_x = std::max({t.x0, t.x1, t.x2});
		const float max_y = std::max({t.y0, t.y1, t.y2});
		int tx0 = static_cast<int>(std::floor(min_x)) / kTile;
		int ty0 = static_cast<int>(std::floor(min_y)) / kTile;
		int tx1 = static_cast<int>(std::floor(max_x)) / kTile;
		int ty1 = static_cast<int>(std::floor(max_y)) / kTile;
		tx0 = std::max(0, tx0);
		ty0 = std::max(0, ty0);
		tx1 = std::min(tiles_x - 1, tx1);
		ty1 = std::min(tiles_y - 1, ty1);
		if (tx0 > tx1 || ty0 > ty1) {
			continue;
		}
		for (int ty = ty0; ty <= ty1; ++ty) {
			for (int tx = tx0; tx <= tx1; ++tx) {
				bins[static_cast<std::size_t>(ty * tiles_x + tx)].push_back(ti);
			}
		}
	}

	InitOpenMpOnce();
	std::vector<PixelBounds> thread_bounds;
#if defined(_OPENMP)
	const int max_threads = std::max(1, omp_get_max_threads());
	thread_bounds.resize(static_cast<std::size_t>(max_threads));
	// Parallelize over tiles: each tile owns its pixels → no depth races.
	#pragma omp parallel for schedule(dynamic)
	for (int tile = 0; tile < tile_count; ++tile) {
		const int tid = omp_get_thread_num();
		PixelBounds& local = thread_bounds[static_cast<std::size_t>(tid)];
		const auto& list = bins[static_cast<std::size_t>(tile)];
		if (list.empty()) {
			continue;
		}
		const int tx = tile % tiles_x;
		const int ty = tile / tiles_x;
		const int x0 = tx * kTile;
		const int y0 = ty * kTile;
		const int x1 = std::min(x0 + kTile, width);
		const int y1 = std::min(y0 + kTile, height);
		for (const std::uint32_t idx : list) {
			RasterScreenTriTile(dst, depth, width, height, x0, y0, x1, y1, tris[idx], local);
		}
	}
	MergeThreadBounds(global_bounds, thread_bounds);
#else
	for (int tile = 0; tile < tile_count; ++tile) {
		const auto& list = bins[static_cast<std::size_t>(tile)];
		if (list.empty()) {
			continue;
		}
		const int tx = tile % tiles_x;
		const int ty = tile / tiles_x;
		const int x0 = tx * kTile;
		const int y0 = ty * kTile;
		const int x1 = std::min(x0 + kTile, width);
		const int y1 = std::min(y0 + kTile, height);
		for (const std::uint32_t idx : list) {
			RasterScreenTriTile(dst, depth, width, height, x0, y0, x1, y1, tris[idx], global_bounds);
		}
	}
#endif

	if (global_bounds.valid) {
		framebuffer.NoteDirtyRect(
			global_bounds.min_x,
			global_bounds.min_y,
			global_bounds.max_x + 1,
			global_bounds.max_y + 1);
	}
	framebuffer.FinalizeDirtyTiles();
}

} // namespace detail3d

/**
 * Raster world-space triangles [x0,y0,z0, x1,y1,z1, x2,y2,z2, ...] with view-proj + optional depth.
 *
 * cull_backfaces defaults on for the world path (OpenGL-front kept after Y flip).
 */
inline void RasterTris3dWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const float* verts,
	const std::size_t tri_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	if (tri_count == 0U || verts == nullptr || view_proj16 == nullptr) {
		return;
	}
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	std::vector<detail3d::ScreenTri> screen;
	screen.reserve(tri_count);
	for (std::size_t i = 0U; i < tri_count; ++i) {
		const std::size_t base = i * 9U;
		detail3d::EmitWorldTri(
			screen,
			view_proj16,
			verts[base + 0U],
			verts[base + 1U],
			verts[base + 2U],
			verts[base + 3U],
			verts[base + 4U],
			verts[base + 5U],
			verts[base + 6U],
			verts[base + 7U],
			verts[base + 8U],
			width,
			height,
			tri_color,
			cull_backfaces);
	}
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

/**
 * Clear color (+ depth when provided) then raster world-space triangles.
 */
inline void ClearAndRasterTris3dWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const std::uint32_t clear_color,
	const float* verts,
	const std::size_t tri_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	framebuffer.ResetDirty();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	FillSpan(dst, framebuffer.PixelCount(), clear_color);
	framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
	if (depth != nullptr && depth->Allocated()) {
		depth->Clear(1.0f);
	}
	RasterTris3dWorld(framebuffer, depth, view_proj16, verts, tri_count, tri_color, cull_backfaces);
}

/**
 * Multiply column-major 4x4 matrices: out = a * b (used for MVP = view_proj * model).
 */
inline void MulMat4ColumnMajor(const float* a, const float* b, float* out) {
	float tmp[16];
	for (int col = 0; col < 4; ++col) {
		for (int row = 0; row < 4; ++row) {
			tmp[col * 4 + row] =
				a[0 * 4 + row] * b[col * 4 + 0] +
				a[1 * 4 + row] * b[col * 4 + 1] +
				a[2 * 4 + row] * b[col * 4 + 2] +
				a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
	for (int i = 0; i < 16; ++i) {
		out[i] = tmp[i];
	}
}

/**
 * Raster a retained mesh: positions (xyz) + optional indices through MVP into the tiled path.
 *
 * mvp16 = view_proj * model (column-major). Empty indices (index_count == 0) means a triangle
 * list over positions. Reuses EmitWorldTri + RasterScreenTrisTiled — no second rasterizer.
 */
inline void RasterMeshWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* mvp16,
	const float* positions,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	if (positions == nullptr || mvp16 == nullptr || vertex_count < 3U) {
		return;
	}
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	std::vector<detail3d::ScreenTri> screen;

	auto emit_indexed = [&](const std::uint32_t i0, const std::uint32_t i1, const std::uint32_t i2) {
		if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
			return;
		}
		const float* p0 = positions + static_cast<std::size_t>(i0) * 3U;
		const float* p1 = positions + static_cast<std::size_t>(i1) * 3U;
		const float* p2 = positions + static_cast<std::size_t>(i2) * 3U;
		detail3d::EmitWorldTri(
			screen,
			mvp16,
			p0[0], p0[1], p0[2],
			p1[0], p1[1], p1[2],
			p2[0], p2[1], p2[2],
			width,
			height,
			tri_color,
			cull_backfaces);
	};

	if (index_count > 0U && indices != nullptr) {
		const std::size_t tri_count = index_count / 3U;
		screen.reserve(tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::size_t base = t * 3U;
			emit_indexed(indices[base], indices[base + 1U], indices[base + 2U]);
		}
	} else {
		const std::size_t tri_count = vertex_count / 3U;
		screen.reserve(tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::uint32_t i0 = static_cast<std::uint32_t>(t * 3U);
			emit_indexed(i0, i0 + 1U, i0 + 2U);
		}
	}
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

/**
 * Clear color (+ depth when provided) then raster a retained mesh through MVP.
 */
inline void ClearAndRasterMeshWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* mvp16,
	const std::uint32_t clear_color,
	const float* positions,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	framebuffer.ResetDirty();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	FillSpan(dst, framebuffer.PixelCount(), clear_color);
	framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
	if (depth != nullptr && depth->Allocated()) {
		depth->Clear(1.0f);
	}
	RasterMeshWorld(
		framebuffer,
		depth,
		mvp16,
		positions,
		vertex_count,
		indices,
		index_count,
		tri_color,
		cull_backfaces);
}

/**
 * Raster a retained mesh with atlas texturing (Layer 2.1).
 *
 * uvs: 2 floats/vert (u,v). UV 0..1 covers the full atlas; nearest + clamp.
 * atlas_rgba null or non-positive size → no-op (caller should validate).
 * Reuses EmitWorldTri + RasterScreenTrisTiled — no second rasterizer.
 */
inline void RasterMeshTexturedWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* mvp16,
	const float* positions,
	const float* uvs,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h,
	const bool cull_backfaces) {
	if (positions == nullptr || uvs == nullptr || mvp16 == nullptr || vertex_count < 3U) {
		return;
	}
	if (atlas_rgba == nullptr || atlas_w <= 0 || atlas_h <= 0) {
		return;
	}
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	std::vector<detail3d::ScreenTri> screen;
	// Flat color unused when atlas is set; pass opaque white as a harmless placeholder.
	constexpr std::uint32_t kUnusedFlat = 0xFFFFFFFFU;

	auto emit_indexed = [&](const std::uint32_t i0, const std::uint32_t i1, const std::uint32_t i2) {
		if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
			return;
		}
		const float* p0 = positions + static_cast<std::size_t>(i0) * 3U;
		const float* p1 = positions + static_cast<std::size_t>(i1) * 3U;
		const float* p2 = positions + static_cast<std::size_t>(i2) * 3U;
		const float* t0 = uvs + static_cast<std::size_t>(i0) * 2U;
		const float* t1 = uvs + static_cast<std::size_t>(i1) * 2U;
		const float* t2 = uvs + static_cast<std::size_t>(i2) * 2U;
		detail3d::EmitWorldTri(
			screen,
			mvp16,
			p0[0], p0[1], p0[2],
			p1[0], p1[1], p1[2],
			p2[0], p2[1], p2[2],
			width,
			height,
			kUnusedFlat,
			cull_backfaces,
			t0[0], t0[1],
			t1[0], t1[1],
			t2[0], t2[1],
			atlas_rgba,
			atlas_w,
			atlas_h);
	};

	if (index_count > 0U && indices != nullptr) {
		const std::size_t tri_count = index_count / 3U;
		screen.reserve(tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::size_t base = t * 3U;
			emit_indexed(indices[base], indices[base + 1U], indices[base + 2U]);
		}
	} else {
		const std::size_t tri_count = vertex_count / 3U;
		screen.reserve(tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::uint32_t i0 = static_cast<std::uint32_t>(t * 3U);
			emit_indexed(i0, i0 + 1U, i0 + 2U);
		}
	}
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

/**
 * Clear color (+ depth when provided) then raster a textured retained mesh.
 */
inline void ClearAndRasterMeshTexturedWorld(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* mvp16,
	const std::uint32_t clear_color,
	const float* positions,
	const float* uvs,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h,
	const bool cull_backfaces) {
	framebuffer.ResetDirty();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	FillSpan(dst, framebuffer.PixelCount(), clear_color);
	framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
	if (depth != nullptr && depth->Allocated()) {
		depth->Clear(1.0f);
	}
	RasterMeshTexturedWorld(
		framebuffer,
		depth,
		mvp16,
		positions,
		uvs,
		vertex_count,
		indices,
		index_count,
		atlas_rgba,
		atlas_w,
		atlas_h,
		cull_backfaces);
}

/**
 * Raster screen-space triangles: pixel xy + NDC z [-1,1] per vertex (9 floats/tri).
 *
 * Skips view-proj and frustum clip. cull_backfaces is typically off for this path.
 */
inline void RasterTris3dScreen(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* verts,
	const std::size_t tri_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	if (tri_count == 0U || verts == nullptr) {
		return;
	}
	std::vector<detail3d::ScreenTri> screen;
	screen.reserve(tri_count);
	for (std::size_t i = 0U; i < tri_count; ++i) {
		const std::size_t base = i * 9U;
		detail3d::EmitScreenTri(
			screen,
			verts[base + 0U],
			verts[base + 1U],
			verts[base + 2U],
			verts[base + 3U],
			verts[base + 4U],
			verts[base + 5U],
			verts[base + 6U],
			verts[base + 7U],
			verts[base + 8U],
			tri_color,
			cull_backfaces);
	}
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

} // namespace hyperlite::raster
