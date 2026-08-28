#pragma once

#include "engine/cpu_blend.hpp"
#include "engine/cpu_line_raster.hpp"
#include "engine/cpu_line_raster_3d.hpp"
#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"

#include <algorithm>
#include <bit>
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
 *
 * Uses outcodes for trivial accept (copy 3 verts, skip Sutherland–Hodgman) and
 * trivial reject (all three outside the same plane). Partial clips still run SH.
 */
inline int ClipTriangleHomogeneous(const ClipVert& a, const ClipVert& b, const ClipVert& c, ClipVert* out_verts) {
	const int ca = ComputeClipOutcode(a);
	const int cb = ComputeClipOutcode(b);
	const int cc = ComputeClipOutcode(c);
	if ((ca & cb & cc) != 0) {
		return 0;
	}
	if ((ca | cb | cc) == 0) {
		out_verts[0] = a;
		out_verts[1] = b;
		out_verts[2] = c;
		return 3;
	}
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
 * Minimum window depth over a screen triangle (linear z/w → extrema at vertices).
 */
inline float TriMinWindowDepthVertices(ScreenTri tri) {
	float area2 = ScreenSignedArea2(tri.x0, tri.y0, tri.x1, tri.y1, tri.x2, tri.y2);
	if (area2 < 0.0f) {
		std::swap(tri.zw1, tri.zw2);
	}
	const float z0 = NdcToWindowDepth(tri.zw0);
	const float z1 = NdcToWindowDepth(tri.zw1);
	const float z2 = NdcToWindowDepth(tri.zw2);
	return std::min(z0, std::min(z1, z2));
}

/**
 * Farthest stored window depth (max sample) in a tile region — conservative Hi-Z occluder.
 *
 * Retained for tests / fallback; the tiled raster tracks max depth on write instead of
 * rescanning the tile after every triangle (see RasterScreenTrisTiled).
 */
inline float ScanTileMaxDepth(
	const float* depth_base,
	const int width,
	const int x0,
	const int y0,
	const int x1,
	const int y1) {
	float max_d = 0.0f;
#if defined(__AVX2__)
	const __m256 vzero = _mm256_setzero_ps();
	__m256 vmax = vzero;
	for (int y = y0; y < y1; ++y) {
		const float* row = depth_base + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
		int x = x0;
		for (; x + 8 <= x1; x += 8) {
			const __m256 v = _mm256_loadu_ps(row + x);
			vmax = _mm256_max_ps(vmax, v);
		}
		for (; x < x1; ++x) {
			max_d = std::max(max_d, row[x]);
		}
	}
	alignas(32) float tmp[8];
	_mm256_store_ps(tmp, vmax);
	for (int i = 0; i < 8; ++i) {
		max_d = std::max(max_d, tmp[i]);
	}
#else
	for (int y = y0; y < y1; ++y) {
		const float* row = depth_base + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
		for (int x = x0; x < x1; ++x) {
			max_d = std::max(max_d, row[x]);
		}
	}
#endif
	return max_d;
}

/**
 * True when every pixel center in a horizontal depth span fails vs tile Hi-Z.
 *
 * Screen depth is linear in x; span minimum is at one endpoint. Conservative for
 * partial edge coverage (subset of covered pixels cannot be nearer than span min).
 */
inline bool DepthSpanBehindOccluder(
	const float z_start,
	const float dz_dx,
	const int span_len,
	const float tile_occluder_max) {
	if (span_len <= 0) {
		return false;
	}
	if (span_len == 1) {
		return z_start > tile_occluder_max;
	}
	const float z_end = z_start + dz_dx * static_cast<float>(span_len - 1);
	const float z_min = (dz_dx >= 0.0f) ? z_start : z_end;
	return z_min > tile_occluder_max;
}

/**
 * Merge one written depth sample into the per-triangle Hi-Z write accumulator.
 */
inline void AccumulateDepthWriteMax(float& write_max, const float z) {
	write_max = (write_max < 0.0f) ? z : std::max(write_max, z);
}

/**
 * Max window depth among SIMD lanes that were actually written (bitmask from fill).
 */
#if defined(__AVX2__)
inline void AccumulateMaskedDepthMax8(float& write_max, const __m256 zv, const int mask) {
	if (mask == 0) {
		return;
	}
	alignas(32) float tmp[8];
	_mm256_storeu_ps(tmp, zv);
	unsigned m = static_cast<unsigned>(mask);
	while (m != 0U) {
		const int i = static_cast<int>(std::countr_zero(m));
		AccumulateDepthWriteMax(write_max, tmp[i]);
		m &= m - 1U;
	}
}
#endif

#if defined(__SSE4_2__)
inline void AccumulateMaskedDepthMax4(float& write_max, const __m128 zv, const int mask) {
	if (mask == 0) {
		return;
	}
	alignas(16) float tmp[4];
	_mm_storeu_ps(tmp, zv);
	unsigned m = static_cast<unsigned>(mask);
	while (m != 0U) {
		const int i = static_cast<int>(std::countr_zero(m));
		AccumulateDepthWriteMax(write_max, tmp[i]);
		m &= m - 1U;
	}
}
#endif

/**
 * True when the triangle's nearest window depth is behind every stored sample in the tile.
 *
 * Uses the triangle-wide vertex minimum (linear z/w → exact min on the whole tri).
 * tile_max_depth must be the max depth currently in the tile (1.0 = cleared / no occluder).
 */
inline bool TriTileDepthReject(const ScreenTri& tri, const float tile_max_depth) {
	if (tile_max_depth >= 1.0f) {
		return false;
	}
	return TriMinWindowDepthVertices(tri) > tile_max_depth;
}

/**
 * Tile reject when tri_min (cached vertex minimum) is behind tile_max.
 */
inline bool TriTileDepthRejectMin(const float tri_min_depth, const float tile_max_depth) {
	if (tile_max_depth >= 1.0f) {
		return false;
	}
	return tri_min_depth > tile_max_depth;
}

/**
 * Fold farthest depth written by one triangle into the tile Hi-Z occluder.
 */
inline void MergeTileHiZFromWrite(
	float& tile_max,
	bool& tile_hiz_active,
	const float tri_write_max) {
	if (tri_write_max < 0.0f) {
		return;
	}
	if (!tile_hiz_active) {
		tile_max = tri_write_max;
		tile_hiz_active = true;
	} else {
		tile_max = std::max(tile_max, tri_write_max);
	}
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

/**
 * AVX-512VL: 8-wide opaque depth-only block (coverage + depth test/write, no color).
 *
 * When old_depth_out is non-null, stores pre-write depth values for alpha fixup on
 * the scalar atlas sample path (a==0 / translucent texels revert speculative writes).
 */
inline int FillOpaqueDepthBlock8Vl(
	float* row_depth,
	const __m256 w0,
	const __m256 w1,
	const __m256 w2,
	const __m256 z_win,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth,
	float* old_depth_out) {
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
		if (old_depth_out != nullptr) {
			_mm256_storeu_ps(old_depth_out, d);
		}
		const __mmask8 pass = _mm256_cmp_ps_mask(z_win, d, _CMP_LE_OQ);
		cov = static_cast<__mmask8>(cov & pass);
		if (cov == 0) {
			return 0;
		}
		_mm256_mask_storeu_ps(row_depth, cov, z_win);
	}
	return static_cast<int>(cov);
}

/**
 * AVX-512VL: 8-wide interior depth block (fully covered span; no edge compares).
 */
inline int FillInteriorDepthBlock8Vl(
	float* row_depth,
	const __m256 z_win,
	const bool has_depth,
	float* old_depth_out) {
	if (!has_depth || row_depth == nullptr) {
		return 0xFF;
	}
	const __m256 d = _mm256_loadu_ps(row_depth);
	if (old_depth_out != nullptr) {
		_mm256_storeu_ps(old_depth_out, d);
	}
	const __mmask8 pass = _mm256_cmp_ps_mask(z_win, d, _CMP_LE_OQ);
	if (pass == 0) {
		return 0;
	}
	_mm256_mask_storeu_ps(row_depth, pass, z_win);
	return static_cast<int>(pass);
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

/**
 * AVX2: 8-wide opaque depth-only block (coverage + depth test/write, no color).
 */
inline int FillOpaqueDepthBlock8(
	float* row_depth,
	const __m256 w0,
	const __m256 w1,
	const __m256 w2,
	const __m256 z_win,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth,
	float* old_depth_out) {
	const __m256 zero = _mm256_setzero_ps();
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
		if (old_depth_out != nullptr) {
			_mm256_storeu_ps(old_depth_out, d);
		}
		const __m256 pass = _mm256_cmp_ps(z_win, d, _CMP_LE_OQ);
		const __m256 write = _mm256_and_ps(cov, pass);
		store_mask = _mm256_castps_si256(write);
		const int write_bits = _mm256_movemask_ps(write);
		if (write_bits == 0) {
			return 0;
		}
		_mm256_maskstore_ps(row_depth, store_mask, z_win);
	}
	return _mm256_movemask_ps(_mm256_castsi256_ps(store_mask));
}

/**
 * AVX2: 8-wide interior depth block (fully covered span; no edge compares).
 */
inline int FillInteriorDepthBlock8(
	float* row_depth,
	const __m256 z_win,
	const bool has_depth,
	float* old_depth_out) {
	if (!has_depth || row_depth == nullptr) {
		return 0xFF;
	}
	const __m256 d = _mm256_loadu_ps(row_depth);
	if (old_depth_out != nullptr) {
		_mm256_storeu_ps(old_depth_out, d);
	}
	const __m256 pass = _mm256_cmp_ps(z_win, d, _CMP_LE_OQ);
	const __m256i store_mask = _mm256_castps_si256(pass);
	const int write_bits = _mm256_movemask_ps(pass);
	if (write_bits == 0) {
		return 0;
	}
	_mm256_maskstore_ps(row_depth, store_mask, z_win);
	return write_bits;
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

/**
 * SSE4.2: 4-wide opaque depth-only block (coverage + depth test/write, no color).
 */
inline int FillOpaqueDepthBlock4(
	float* row_depth,
	const __m128 w0,
	const __m128 w1,
	const __m128 w2,
	const __m128 z_win,
	const bool tl0,
	const bool tl1,
	const bool tl2,
	const bool has_depth,
	float* old_depth_out) {
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
		if (old_depth_out != nullptr) {
			_mm_storeu_ps(old_depth_out, d);
		}
		const __m128 pass = _mm_cmple_ps(z_win, d);
		write = _mm_and_ps(cov, pass);
		const int write_bits = _mm_movemask_ps(write);
		if (write_bits == 0) {
			return 0;
		}
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
	return _mm_movemask_ps(write);
}

/**
 * SSE4.2: 4-wide interior depth block (fully covered span; no edge compares).
 */
inline int FillInteriorDepthBlock4(
	float* row_depth,
	const __m128 z_win,
	const bool has_depth,
	float* old_depth_out) {
	if (!has_depth || row_depth == nullptr) {
		return 0xF;
	}
	const __m128 d = _mm_loadu_ps(row_depth);
	if (old_depth_out != nullptr) {
		_mm_storeu_ps(old_depth_out, d);
	}
	const __m128 pass = _mm_cmple_ps(z_win, d);
	const int write_bits = _mm_movemask_ps(pass);
	if (write_bits == 0) {
		return 0;
	}
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
	return write_bits;
}
#endif

/**
 * Scalar atlas sample + color store for SIMD depth-passed textured lanes.
 *
 * Depth is written speculatively by the SIMD block; a==0 / translucent texels revert
 * and follow the same depth/color rules as the all-scalar textured path.
 */
inline void ProcessTexturedPassedLanes(
	std::uint32_t* row_dst,
	float* row_depth,
	const int x0,
	const int y,
	const int pass_mask,
	const int lane_count,
	const float* old_depth,
	float iw,
	const float diw_dx,
	float uw,
	const float duw_dx,
	float vw,
	const float dvw_dx,
	float z_win,
	const float dz_win_dx,
	const ScreenTri& tri,
	const bool has_depth,
	bool& touched,
	int& dirty_x0,
	int& dirty_y0,
	int& dirty_x1,
	int& dirty_y1,
	float& write_max) {
	if (pass_mask == 0) {
		return;
	}
	float liw = iw;
	float luw = uw;
	float lvw = vw;
	float lz = z_win;
	for (int lane = 0; lane < lane_count; ++lane) {
		if ((pass_mask & (1 << lane)) != 0) {
			const int x = x0 + lane;
			if (std::fabs(liw) >= 1e-20f) {
				const std::uint32_t packed = SampleAtlasNearest(
					tri.atlas_rgba, tri.atlas_w, tri.atlas_h, luw / liw, lvw / liw);
				const std::uint32_t sa = packed >> 24U;
				if (sa != 0U) {
					const bool opaque = sa == 255U;
					if (opaque) {
						row_dst[x] = packed;
						if (has_depth) {
							AccumulateDepthWriteMax(write_max, lz);
						}
						touched = true;
						dirty_x0 = std::min(dirty_x0, x);
						dirty_y0 = std::min(dirty_y0, y);
						dirty_x1 = std::max(dirty_x1, x);
						dirty_y1 = std::max(dirty_y1, y);
					} else {
						if (has_depth && old_depth != nullptr) {
							row_depth[x] = old_depth[lane];
						}
						bool pass = true;
						if (has_depth && lz > row_depth[x]) {
							pass = false;
						}
						if (pass) {
							StorePixel(row_dst + x, packed);
							touched = true;
							dirty_x0 = std::min(dirty_x0, x);
							dirty_y0 = std::min(dirty_y0, y);
							dirty_x1 = std::max(dirty_x1, x);
							dirty_y1 = std::max(dirty_y1, y);
						}
					}
				} else if (has_depth && old_depth != nullptr) {
					row_depth[x] = old_depth[lane];
				}
			} else if (has_depth && old_depth != nullptr) {
				row_depth[x] = old_depth[lane];
			}
		}
		liw += diw_dx;
		luw += duw_dx;
		lvw += dvw_dx;
		lz += dz_win_dx;
	}
}

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
	// std::countr_zero is portable (MSVC has no __builtin_ctz).
	unsigned m = static_cast<unsigned>(mask);
	while (m != 0U) {
		const int i = static_cast<int>(std::countr_zero(m));
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
 * SSE4.2, with scalar remainder. Textured opaque: same SIMD coverage + depth blocks,
 * scalar nearest atlas sample per passed lane (gather SIMD still not used).
 *
 * Returns true when at least one pixel was written.
 *
 * tile_occluder_max: farthest stored depth in this tile (1.0 = no occluder). When less than
 * far-plane, rows and SIMD blocks whose minimum interpolated z exceeds it skip fill work
 * (partial coverage included); scalar pixels use the same Hi-Z rule before loading depth.
 * depth_write_max: when non-null, receives the farthest window depth actually written.
 */
inline bool RasterScreenTriTile(
	std::uint32_t* dst,
	DepthBuffer* depth,
	const int width,
	const int height,
	const int tile_x0,
	const int tile_y0,
	const int tile_x1,
	const int tile_y1,
	ScreenTri tri,
	PixelBounds& bounds,
	const float tile_occluder_max = 1.0f,
	float* depth_write_max = nullptr) {
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
		return false;
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
		return false;
	}

	// Edge 0 = v1→v2 (bary w0), edge 1 = v2→v0, edge 2 = v0→v1.
	const HalfEdgeCoef e0 = MakeHalfEdge(tri.x1, tri.y1, tri.x2, tri.y2);
	const HalfEdgeCoef e1 = MakeHalfEdge(tri.x2, tri.y2, tri.x0, tri.y0);
	const HalfEdgeCoef e2 = MakeHalfEdge(tri.x0, tri.y0, tri.x1, tri.y1);

	const float px_lo = static_cast<float>(ix0) + 0.5f;
	const float px_hi = static_cast<float>(ix1 - 1) + 0.5f;
	const float py_lo = static_cast<float>(iy0) + 0.5f;
	const float py_hi = static_cast<float>(iy1 - 1) + 0.5f;
	// Trivial reject: tile AABB vs half-plane signs.
	if (HalfEdgeBoxTrivialOut(e0, px_lo, px_hi, py_lo, py_hi) ||
		HalfEdgeBoxTrivialOut(e1, px_lo, px_hi, py_lo, py_hi) ||
		HalfEdgeBoxTrivialOut(e2, px_lo, px_hi, py_lo, py_hi)) {
		return false;
	}

	const float inv_area = 1.0f / area2;
	const bool textured = tri.atlas_rgba != nullptr && tri.atlas_w > 0 && tri.atlas_h > 0;
	const std::uint32_t flat_packed = tri.color;
	const std::uint32_t flat_a = flat_packed >> 24U;
	const bool opaque_flat = !textured && flat_a == 255U;
	const bool has_depth = depth != nullptr && depth->Allocated();
	float* depth_base = has_depth ? depth->Data() : nullptr;
	const std::size_t stride = static_cast<std::size_t>(width);
	constexpr float kHiZFarDepth = 1.0f - 1e-6f;
	const bool tile_depth_reject =
		has_depth && tile_occluder_max < kHiZFarDepth;
	float write_max = -1.0f;

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
			if (tile_depth_reject &&
				DepthSpanBehindOccluder(z_win_row, dz_win_dx, ix1 - ix0, tile_occluder_max)) {
				w0_row += e0.b;
				w1_row += e1.b;
				w2_row += e2.b;
				z_win_row += dz_win_dy;
				continue;
			}
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
					if (tile_depth_reject &&
						DepthSpanBehindOccluder(z_win, dz_win_dx, kBlock, tile_occluder_max)) {
						z_win += dz_win_dx * static_cast<float>(kBlock);
						continue;
					}
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
							AccumulateMaskedDepthMax8(write_max, zv, written);
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
							AccumulateMaskedDepthMax4(write_max, zv, written);
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
					if (tile_depth_reject &&
						DepthSpanBehindOccluder(z_win, dz_win_dx, kBlock, tile_occluder_max)) {
						w0 += e0.a * static_cast<float>(kBlock);
						w1 += e1.a * static_cast<float>(kBlock);
						w2 += e2.a * static_cast<float>(kBlock);
						z_win += dz_win_dx * static_cast<float>(kBlock);
						continue;
					}
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
					if (has_depth && written != 0) {
						AccumulateMaskedDepthMax8(write_max, zv, written);
					}
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
					if (has_depth && written != 0) {
						AccumulateMaskedDepthMax4(write_max, zv, written);
					}
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
						if (!(tile_depth_reject && z_win > tile_occluder_max)) {
							float& slot = row_depth[x];
							if (z_win <= slot) {
								slot = z_win;
								row_dst[x] = flat_packed;
								AccumulateDepthWriteMax(write_max, z_win);
								touched = true;
								dirty_x0 = std::min(dirty_x0, x);
								dirty_y0 = std::min(dirty_y0, y);
								dirty_x1 = std::max(dirty_x1, x);
								dirty_y1 = std::max(dirty_y1, y);
							}
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
		// Textured: SIMD coverage + depth (same blocks as opaque flat), scalar nearest atlas sample.
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

#if defined(__AVX2__)
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
			if (tile_depth_reject &&
				DepthSpanBehindOccluder(z_win_row, dz_win_dx, ix1 - ix0, tile_occluder_max)) {
				w0_row += e0.b;
				w1_row += e1.b;
				w2_row += e2.b;
				z_win_row += dz_win_dy;
				iw_row += diw_dy;
				uw_row += duw_dy;
				vw_row += dvw_dy;
				continue;
			}
			float w0 = w0_row;
			float w1 = w1_row;
			float w2 = w2_row;
			float z_win = z_win_row;
			float iw = iw_row;
			float uw = uw_row;
			float vw = vw_row;
			std::uint32_t* row_dst = dst + static_cast<std::size_t>(y) * stride;
			float* row_depth = has_depth ? (depth_base + static_cast<std::size_t>(y) * stride) : nullptr;

			int x = ix0;
#if defined(__AVX2__) || defined(__SSE4_2__)
			if (box_fully_covered) {
				for (; x + kBlock <= ix1; x += kBlock) {
					if (tile_depth_reject &&
						DepthSpanBehindOccluder(z_win, dz_win_dx, kBlock, tile_occluder_max)) {
						z_win += dz_win_dx * static_cast<float>(kBlock);
						iw += diw_dx * static_cast<float>(kBlock);
						uw += duw_dx * static_cast<float>(kBlock);
						vw += dvw_dx * static_cast<float>(kBlock);
						continue;
					}
#if defined(__AVX2__)
					alignas(32) float old_depth[8];
					const __m256 zv = _mm256_add_ps(_mm256_set1_ps(z_win), dz_lane);
					const int passed = FillInteriorDepthBlock8(
						has_depth ? (row_depth + x) : nullptr, zv, has_depth, has_depth ? old_depth : nullptr);
					if (passed != 0) {
						ProcessTexturedPassedLanes(
							row_dst,
							row_depth,
							x,
							y,
							passed,
							kBlock,
							has_depth ? old_depth : nullptr,
							iw,
							diw_dx,
							uw,
							duw_dx,
							vw,
							dvw_dx,
							z_win,
							dz_win_dx,
							tri,
							has_depth,
							touched,
							dirty_x0,
							dirty_y0,
							dirty_x1,
							dirty_y1,
							write_max);
					}
#elif defined(__SSE4_2__)
					alignas(16) float old_depth[4];
					const __m128 zv = _mm_add_ps(_mm_set1_ps(z_win), dz_lane);
					const int passed = FillInteriorDepthBlock4(
						has_depth ? (row_depth + x) : nullptr, zv, has_depth, has_depth ? old_depth : nullptr);
					if (passed != 0) {
						ProcessTexturedPassedLanes(
							row_dst,
							row_depth,
							x,
							y,
							passed,
							kBlock,
							has_depth ? old_depth : nullptr,
							iw,
							diw_dx,
							uw,
							duw_dx,
							vw,
							dvw_dx,
							z_win,
							dz_win_dx,
							tri,
							has_depth,
							touched,
							dirty_x0,
							dirty_y0,
							dirty_x1,
							dirty_y1,
							write_max);
					}
#endif
					z_win += dz_win_dx * static_cast<float>(kBlock);
					iw += diw_dx * static_cast<float>(kBlock);
					uw += duw_dx * static_cast<float>(kBlock);
					vw += dvw_dx * static_cast<float>(kBlock);
				}
			} else {
				for (; x + kBlock <= ix1; x += kBlock) {
					if (tile_depth_reject &&
						DepthSpanBehindOccluder(z_win, dz_win_dx, kBlock, tile_occluder_max)) {
						w0 += e0.a * static_cast<float>(kBlock);
						w1 += e1.a * static_cast<float>(kBlock);
						w2 += e2.a * static_cast<float>(kBlock);
						z_win += dz_win_dx * static_cast<float>(kBlock);
						iw += diw_dx * static_cast<float>(kBlock);
						uw += duw_dx * static_cast<float>(kBlock);
						vw += dvw_dx * static_cast<float>(kBlock);
						continue;
					}
#if defined(__AVX2__)
					alignas(32) float old_depth[8];
					const __m256 w0v = _mm256_add_ps(_mm256_set1_ps(w0), a0_lane);
					const __m256 w1v = _mm256_add_ps(_mm256_set1_ps(w1), a1_lane);
					const __m256 w2v = _mm256_add_ps(_mm256_set1_ps(w2), a2_lane);
					const __m256 zv = _mm256_add_ps(_mm256_set1_ps(z_win), dz_lane);
#if defined(__AVX512F__) && defined(__AVX512VL__)
					const int passed = FillOpaqueDepthBlock8Vl(
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth,
						has_depth ? old_depth : nullptr);
#else
					const int passed = FillOpaqueDepthBlock8(
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth,
						has_depth ? old_depth : nullptr);
#endif
					if (passed != 0) {
						ProcessTexturedPassedLanes(
							row_dst,
							row_depth,
							x,
							y,
							passed,
							kBlock,
							has_depth ? old_depth : nullptr,
							iw,
							diw_dx,
							uw,
							duw_dx,
							vw,
							dvw_dx,
							z_win,
							dz_win_dx,
							tri,
							has_depth,
							touched,
							dirty_x0,
							dirty_y0,
							dirty_x1,
							dirty_y1,
							write_max);
					}
#elif defined(__SSE4_2__)
					alignas(16) float old_depth[4];
					const __m128 w0v = _mm_add_ps(_mm_set1_ps(w0), a0_lane);
					const __m128 w1v = _mm_add_ps(_mm_set1_ps(w1), a1_lane);
					const __m128 w2v = _mm_add_ps(_mm_set1_ps(w2), a2_lane);
					const __m128 zv = _mm_add_ps(_mm_set1_ps(z_win), dz_lane);
					const int passed = FillOpaqueDepthBlock4(
						has_depth ? (row_depth + x) : nullptr,
						w0v,
						w1v,
						w2v,
						zv,
						e0.top_left,
						e1.top_left,
						e2.top_left,
						has_depth,
						has_depth ? old_depth : nullptr);
					if (passed != 0) {
						ProcessTexturedPassedLanes(
							row_dst,
							row_depth,
							x,
							y,
							passed,
							kBlock,
							has_depth ? old_depth : nullptr,
							iw,
							diw_dx,
							uw,
							duw_dx,
							vw,
							dvw_dx,
							z_win,
							dz_win_dx,
							tri,
							has_depth,
							touched,
							dirty_x0,
							dirty_y0,
							dirty_x1,
							dirty_y1,
							write_max);
					}
#endif
					w0 += e0.a * static_cast<float>(kBlock);
					w1 += e1.a * static_cast<float>(kBlock);
					w2 += e2.a * static_cast<float>(kBlock);
					z_win += dz_win_dx * static_cast<float>(kBlock);
					iw += diw_dx * static_cast<float>(kBlock);
					uw += duw_dx * static_cast<float>(kBlock);
					vw += dvw_dx * static_cast<float>(kBlock);
				}
			}
#endif
			for (; x < ix1; ++x) {
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
							if (tile_depth_reject && z_win > tile_occluder_max) {
								pass = false;
							} else if (opaque) {
								float& slot = row_depth[x];
								if (z_win <= slot) {
									slot = z_win;
									AccumulateDepthWriteMax(write_max, z_win);
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
	if (depth_write_max != nullptr && write_max >= 0.0f) {
		*depth_write_max = write_max;
	}
	return touched;
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
	tri.u0 = u0;
	tri.v0 = v0;
	tri.x1 = x1;
	tri.y1 = y1;
	tri.zw1 = zw1;
	tri.u1 = u1;
	tri.v1 = v1;
	tri.x2 = x2;
	tri.y2 = y2;
	tri.zw2 = zw2;
	tri.u2 = u2;
	tri.v2 = v2;
	tri.color = color;
	tri.atlas_rgba = atlas_rgba;
	tri.atlas_w = atlas_w;
	tri.atlas_h = atlas_h;
	const bool textured = atlas_rgba != nullptr && atlas_w > 0 && atlas_h > 0;
	if (textured) {
		tri.iw0 = iw0;
		tri.iw1 = iw1;
		tri.iw2 = iw2;
	}
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
	const bool textured = atlas_rgba != nullptr && atlas_w > 0 && atlas_h > 0;
	for (int i = 0; i < count; ++i) {
		if (textured) {
			if (!ProjectToPixels(verts[i], width, height, px[i], py[i], zw[i], iw[i])) {
				return;
			}
		} else if (!ProjectToPixelsNoIw(verts[i], width, height, px[i], py[i], zw[i])) {
			return;
		}
		uu[i] = verts[i].u;
		vv[i] = verts[i].v;
	}
	for (int i = 1; i + 1 < count; ++i) {
		TryAppendScreenTri(
			out,
			px[0], py[0], zw[0], textured ? iw[0] : 1.0f, uu[0], vv[0],
			px[i], py[i], zw[i], textured ? iw[i] : 1.0f, uu[i], vv[i],
			px[i + 1], py[i + 1], zw[i + 1], textured ? iw[i + 1] : 1.0f, uu[i + 1], vv[i + 1],
			color,
			cull_backfaces,
			atlas_rgba,
			atlas_w,
			atlas_h);
	}
}

/**
 * Per-draw scratch for mesh transform / emit / tile bins (reused across draws).
 *
 * Process-static via GetMeshDrawScratch() so DrawMesh / TickMesh avoid per-frame heap
 * churn without TLS (which breaks non-PIC static → Python .so links). Transform may use
 * OpenMP over disjoint vertex chunks; emit + bin setup run on one thread; OpenMP fill
 * only reads bins afterward.
 */
struct MeshDrawScratch {
	/** Homogeneous clip-space verts after MVP (one per mesh vertex). */
	std::vector<ClipVert> clip_verts{};
	/** Per-vert frustum outcodes matching clip_verts. */
	std::vector<int> outcodes{};
	/** Screen attrs valid when outcodes[i] == 0 (trivial-in). */
	std::vector<float> px{};
	std::vector<float> py{};
	std::vector<float> zw{};
	std::vector<float> iw{};
	/** Emitted screen triangles for the current draw. */
	std::vector<ScreenTri> screen{};
	/** 64×64 tile → triangle index lists (capacity retained across frames). */
	std::vector<std::vector<std::uint32_t>> bins{};
	/** Per-tile max stored window depth for Hi-Z reject (persisted across draws until depth clear). */
	std::vector<float> tile_max_depth{};
	/** TileHiZEpoch() value when tile_max_depth was last reset to far (1.0). */
	std::uint32_t tile_hiz_epoch = 0U;
	/** Front-to-back permutation into `screen` / draw tri list (reused each RasterScreenTrisTiled). */
	std::vector<std::uint32_t> tri_order{};
	/** Cached min window depth per tri (parallel to tri_order / draw list). */
	std::vector<float> tri_min_depth{};
};

/**
 * Process-wide mesh scratch (emit + bin setup are single-threaded before OpenMP fill).
 *
 * Not thread_local: TLS in a non-PIC static archive fails when linking the Python .so
 * (R_X86_64_TPOFF32). Transform writes disjoint vertex ranges in parallel; emit/bin
 * and OpenMP tile fill follow on the calling thread / tile workers respectively.
 */
inline MeshDrawScratch& GetMeshDrawScratch() {
	static MeshDrawScratch scratch;
	return scratch;
}

/** Minimum mesh vertex count before OpenMP transform amortizes fork overhead. */
constexpr std::size_t kMinVertsForOmpTransform = 512U;
/** Vertices per OpenMP chunk (disjoint writes; AVX2 inner loop per chunk). */
constexpr std::size_t kTransformChunkVerts = 64U;

/**
 * Transform vertices [begin, end) through MVP; write clip + outcodes (+ project when trivial-in).
 *
 * When iw_out is non-null, also stores 1/w for textured emit; flat mesh passes null and skips iw.
 * AVX2 8-wide AoS gather when __AVX2__ && __FMA__; scalar remainder / portable build.
 */
inline void TransformMeshPositionsRange(
	const float* mvp,
	const float* positions,
	const std::size_t begin,
	const std::size_t end,
	ClipVert* clip_out,
	int* codes_out,
	float* px_out,
	float* py_out,
	float* zw_out,
	float* iw_out,
	const int width,
	const int height) {
	const bool store_iw = iw_out != nullptr;
	std::size_t i = begin;
#if defined(__AVX2__) && defined(__FMA__)
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
	alignas(32) float ox[8];
	alignas(32) float oy[8];
	alignas(32) float oz[8];
	alignas(32) float ow[8];
	for (; i + 8U <= end; i += 8U) {
		const int base = static_cast<int>(i * 3U);
		const __m256i off = _mm256_setr_epi32(base, base + 3, base + 6, base + 9, base + 12, base + 15, base + 18, base + 21);
		const __m256 vx = _mm256_i32gather_ps(positions, off, 4);
		const __m256 vy = _mm256_i32gather_ps(positions, _mm256_add_epi32(off, _mm256_set1_epi32(1)), 4);
		const __m256 vz = _mm256_i32gather_ps(positions, _mm256_add_epi32(off, _mm256_set1_epi32(2)), 4);
		const __m256 cx = _mm256_fmadd_ps(m0, vx, _mm256_fmadd_ps(m4, vy, _mm256_fmadd_ps(m8, vz, m12)));
		const __m256 cy = _mm256_fmadd_ps(m1, vx, _mm256_fmadd_ps(m5, vy, _mm256_fmadd_ps(m9, vz, m13)));
		const __m256 cz = _mm256_fmadd_ps(m2, vx, _mm256_fmadd_ps(m6, vy, _mm256_fmadd_ps(m10, vz, m14)));
		const __m256 cw = _mm256_fmadd_ps(m3, vx, _mm256_fmadd_ps(m7, vy, _mm256_fmadd_ps(m11, vz, m15)));
		_mm256_store_ps(ox, cx);
		_mm256_store_ps(oy, cy);
		_mm256_store_ps(oz, cz);
		_mm256_store_ps(ow, cw);
		for (int lane = 0; lane < 8; ++lane) {
			const std::size_t vi = i + static_cast<std::size_t>(lane);
			ClipVert& c = clip_out[vi];
			c.x = ox[lane];
			c.y = oy[lane];
			c.z = oz[lane];
			c.w = ow[lane];
			c.u = 0.0f;
			c.v = 0.0f;
			const int code = ComputeClipOutcode(c);
			codes_out[vi] = code;
			if (code == 0) {
				const bool projected = store_iw
					? ProjectToPixels(c, width, height, px_out[vi], py_out[vi], zw_out[vi], iw_out[vi])
					: ProjectToPixelsNoIw(c, width, height, px_out[vi], py_out[vi], zw_out[vi]);
				if (!projected) {
					codes_out[vi] = kNear;
				}
			}
		}
	}
#endif
	for (; i < end; ++i) {
		const float* p = positions + i * 3U;
		ClipVert& c = clip_out[i];
		c = MulViewProj(mvp, p[0], p[1], p[2]);
		c.u = 0.0f;
		c.v = 0.0f;
		const int code = ComputeClipOutcode(c);
		codes_out[i] = code;
		if (code == 0) {
			const bool projected = store_iw
				? ProjectToPixels(c, width, height, px_out[i], py_out[i], zw_out[i], iw_out[i])
				: ProjectToPixelsNoIw(c, width, height, px_out[i], py_out[i], zw_out[i]);
			if (!projected) {
				codes_out[i] = kNear;
			}
		}
	}
}

/**
 * Transform mesh positions through column-major MVP; write clip + outcodes.
 *
 * When outcode == 0, also perspective-divides into px/py/zw (project once per vert).
 * When iw_out is non-null, also stores 1/w for textured emit; flat mesh passes null and skips iw.
 * Large meshes use OpenMP over 64-vert chunks (disjoint scratch writes); small meshes stay serial.
 */
inline void TransformMeshPositions(
	const float* mvp,
	const float* positions,
	const std::size_t vertex_count,
	ClipVert* clip_out,
	int* codes_out,
	float* px_out,
	float* py_out,
	float* zw_out,
	float* iw_out,
	const int width,
	const int height) {
#if defined(_OPENMP)
	if (vertex_count >= kMinVertsForOmpTransform) {
		InitOpenMpOnce();
		const int num_chunks =
			static_cast<int>((vertex_count + kTransformChunkVerts - 1U) / kTransformChunkVerts);
		#pragma omp parallel for schedule(static)
		for (int c = 0; c < num_chunks; ++c) {
			const std::size_t begin = static_cast<std::size_t>(c) * kTransformChunkVerts;
			const std::size_t end = std::min(begin + kTransformChunkVerts, vertex_count);
			TransformMeshPositionsRange(
				mvp,
				positions,
				begin,
				end,
				clip_out,
				codes_out,
				px_out,
				py_out,
				zw_out,
				iw_out,
				width,
				height);
		}
		return;
	}
#endif
	TransformMeshPositionsRange(
		mvp,
		positions,
		0U,
		vertex_count,
		clip_out,
		codes_out,
		px_out,
		py_out,
		zw_out,
		iw_out,
		width,
		height);
}

/**
 * Sutherland–Hodgman clip without re-running outcode accept/reject (caller already knows).
 */
inline int ClipTriangleHomogeneousPartial(
	const ClipVert& a,
	const ClipVert& b,
	const ClipVert& c,
	ClipVert* out_verts) {
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
 * Trivial-accept flat tri append (outcodes already verified zero; no atlas/UV work).
 */
inline bool TryAppendFlatScreenTriFast(
	std::vector<ScreenTri>& out,
	const float x0,
	const float y0,
	const float zw0,
	const float x1,
	const float y1,
	const float zw1,
	const float x2,
	const float y2,
	const float zw2,
	const std::uint32_t color,
	const bool cull_backfaces) {
	const float area = ScreenSignedArea2(x0, y0, x1, y1, x2, y2);
	if (cull_backfaces) {
		if (area >= 0.0f) {
			return false;
		}
	} else if (std::fabs(area) < 1e-8f) {
		return false;
	}
	ScreenTri& tri = out.emplace_back();
	tri.x0 = x0;
	tri.y0 = y0;
	tri.zw0 = zw0;
	tri.x1 = x1;
	tri.y1 = y1;
	tri.zw1 = zw1;
	tri.x2 = x2;
	tri.y2 = y2;
	tri.zw2 = zw2;
	tri.color = color;
	return true;
}

/**
 * Trivial-accept textured tri append (outcodes already verified zero).
 */
inline bool TryAppendTexturedScreenTriFast(
	std::vector<ScreenTri>& out,
	const float x0,
	const float y0,
	const float zw0,
	const float iw0,
	const float u0,
	const float v0,
	const float x1,
	const float y1,
	const float zw1,
	const float iw1,
	const float u1,
	const float v1,
	const float x2,
	const float y2,
	const float zw2,
	const float iw2,
	const float u2,
	const float v2,
	const std::uint32_t color,
	const bool cull_backfaces,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h) {
	const float area = ScreenSignedArea2(x0, y0, x1, y1, x2, y2);
	if (cull_backfaces) {
		if (area >= 0.0f) {
			return false;
		}
	} else if (std::fabs(area) < 1e-8f) {
		return false;
	}
	ScreenTri& tri = out.emplace_back();
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
	return true;
}

/**
 * Emit one indexed triangle from pre-transformed clip verts (flat or textured).
 *
 * Trivial-accept uses project-once screen attrs; partial clips run Sutherland–Hodgman.
 */
inline void EmitIndexedClipTri(
	std::vector<ScreenTri>& out,
	const ClipVert* clip_verts,
	const int* outcodes,
	const float* px,
	const float* py,
	const float* zw,
	const float* iw,
	const std::uint32_t i0,
	const std::uint32_t i1,
	const std::uint32_t i2,
	const int width,
	const int height,
	const std::uint32_t color,
	const bool cull_backfaces,
	const float u0,
	const float v0,
	const float u1,
	const float v1,
	const float u2,
	const float v2,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h) {
	const bool textured = atlas_rgba != nullptr && atlas_w > 0 && atlas_h > 0;
	if (textured && iw == nullptr) {
		return;
	}
	const int ca = outcodes[i0];
	const int cb = outcodes[i1];
	const int cc = outcodes[i2];
	if ((ca & cb & cc) != 0) {
		return;
	}
	if ((ca | cb | cc) == 0) {
		if (atlas_rgba == nullptr) {
			TryAppendFlatScreenTriFast(
				out,
				px[i0], py[i0], zw[i0],
				px[i1], py[i1], zw[i1],
				px[i2], py[i2], zw[i2],
				color,
				cull_backfaces);
		} else {
			TryAppendTexturedScreenTriFast(
				out,
				px[i0], py[i0], zw[i0], iw[i0], u0, v0,
				px[i1], py[i1], zw[i1], iw[i1], u1, v1,
				px[i2], py[i2], zw[i2], iw[i2], u2, v2,
				color,
				cull_backfaces,
				atlas_rgba,
				atlas_w,
				atlas_h);
		}
		return;
	}
	ClipVert a = clip_verts[i0];
	ClipVert b = clip_verts[i1];
	ClipVert c = clip_verts[i2];
	a.u = u0;
	a.v = v0;
	b.u = u1;
	b.v = v1;
	c.u = u2;
	c.v = v2;
	ClipVert clipped[16];
	const int n = ClipTriangleHomogeneousPartial(a, b, c, clipped);
	ProjectFanToScreen(out, clipped, n, width, height, color, cull_backfaces, atlas_rgba, atlas_w, atlas_h);
}

/**
 * Emit indexed flat tris in [tri_begin, tri_end) from pre-transformed clip attrs.
 */
inline void EmitIndexedFlatRange(
	std::vector<ScreenTri>& out,
	const ClipVert* clip_verts,
	const int* outcodes,
	const float* px,
	const float* py,
	const float* zw,
	const std::uint32_t* indices,
	const std::size_t tri_begin,
	const std::size_t tri_end,
	const int width,
	const int height,
	const std::uint32_t color,
	const bool cull_backfaces) {
	for (std::size_t t = tri_begin; t < tri_end; ++t) {
		const std::size_t base = t * 3U;
		const std::uint32_t i0 = indices[base];
		const std::uint32_t i1 = indices[base + 1U];
		const std::uint32_t i2 = indices[base + 2U];
#if defined(__GNUC__) || defined(__clang__)
		if (t + 2U < tri_end) {
			const std::size_t pf_base = (t + 2U) * 3U;
			__builtin_prefetch(&indices[pf_base], 0, 1);
			__builtin_prefetch(&outcodes[indices[pf_base]], 0, 1);
			__builtin_prefetch(&px[indices[pf_base]], 0, 1);
		}
#endif
		const int ca = outcodes[i0];
		const int cb = outcodes[i1];
		const int cc = outcodes[i2];
		if ((ca & cb & cc) != 0) {
			continue;
		}
		if ((ca | cb | cc) == 0) {
			TryAppendFlatScreenTriFast(
				out,
				px[i0], py[i0], zw[i0],
				px[i1], py[i1], zw[i1],
				px[i2], py[i2], zw[i2],
				color,
				cull_backfaces);
			continue;
		}
		EmitIndexedClipTri(
			out,
			clip_verts,
			outcodes,
			px,
			py,
			zw,
			nullptr,
			i0,
			i1,
			i2,
			width,
			height,
			color,
			cull_backfaces,
			0.0f,
			0.0f,
			0.0f,
			0.0f,
			0.0f,
			0.0f,
			nullptr,
			0,
			0);
	}
}

/**
 * Emit indexed textured tris in [tri_begin, tri_end) from pre-transformed clip attrs.
 */
inline void EmitIndexedTexturedRange(
	std::vector<ScreenTri>& out,
	const ClipVert* clip_verts,
	const int* outcodes,
	const float* px,
	const float* py,
	const float* zw,
	const float* iw,
	const float* uvs,
	const std::uint32_t* indices,
	const std::size_t tri_begin,
	const std::size_t tri_end,
	const int width,
	const int height,
	const std::uint32_t color,
	const bool cull_backfaces,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h) {
	for (std::size_t t = tri_begin; t < tri_end; ++t) {
		const std::size_t base = t * 3U;
		const std::uint32_t i0 = indices[base];
		const std::uint32_t i1 = indices[base + 1U];
		const std::uint32_t i2 = indices[base + 2U];
#if defined(__GNUC__) || defined(__clang__)
		if (t + 2U < tri_end) {
			const std::size_t pf_base = (t + 2U) * 3U;
			__builtin_prefetch(&indices[pf_base], 0, 1);
			__builtin_prefetch(&outcodes[indices[pf_base]], 0, 1);
			__builtin_prefetch(&uvs[static_cast<std::size_t>(indices[pf_base]) * 2U], 0, 1);
		}
#endif
		const int ca = outcodes[i0];
		const int cb = outcodes[i1];
		const int cc = outcodes[i2];
		if ((ca & cb & cc) != 0) {
			continue;
		}
		const float* t0 = uvs + static_cast<std::size_t>(i0) * 2U;
		const float* t1 = uvs + static_cast<std::size_t>(i1) * 2U;
		const float* t2 = uvs + static_cast<std::size_t>(i2) * 2U;
		if ((ca | cb | cc) == 0) {
			TryAppendTexturedScreenTriFast(
				out,
				px[i0], py[i0], zw[i0], iw[i0], t0[0], t0[1],
				px[i1], py[i1], zw[i1], iw[i1], t1[0], t1[1],
				px[i2], py[i2], zw[i2], iw[i2], t2[0], t2[1],
				color,
				cull_backfaces,
				atlas_rgba,
				atlas_w,
				atlas_h);
			continue;
		}
		EmitIndexedClipTri(
			out,
			clip_verts,
			outcodes,
			px,
			py,
			zw,
			iw,
			i0,
			i1,
			i2,
			width,
			height,
			color,
			cull_backfaces,
			t0[0],
			t0[1],
			t1[0],
			t1[1],
			t2[0],
			t2[1],
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
 * True when front-to-back reorder is worth the sort cost before bin/fill.
 *
 * Skips uniform-depth draws (flat mesh) and draws with a tiny near-depth prefix
 * (fullscreen occluder + back field — already submission-ordered nearest-first).
 */
inline bool ShouldSortTrisFrontToBack(
	const std::size_t tri_count,
	const std::vector<float>& tri_min_depth) {
	if (tri_count <= 1U || tri_min_depth.size() != tri_count) {
		return false;
	}
	float depth_min = tri_min_depth[0];
	float depth_max = tri_min_depth[0];
	for (std::size_t i = 1U; i < tri_count; ++i) {
		const float d = tri_min_depth[i];
		depth_min = std::min(depth_min, d);
		depth_max = std::max(depth_max, d);
	}
	constexpr float kUniformDepthEps = 1e-5f;
	if (depth_max - depth_min <= kUniformDepthEps) {
		return false;
	}
	constexpr float kNearMinDepthEps = 1e-5f;
	constexpr std::size_t kOccluderNearMinMax = 4U;
	int near_min_count = 0;
	for (std::size_t i = 0U; i < tri_count; ++i) {
		if (tri_min_depth[i] <= depth_min + kNearMinDepthEps) {
			++near_min_count;
		}
	}
	if (near_min_count <= static_cast<int>(kOccluderNearMinMax) && tri_count >= 64U) {
		return false;
	}
	// Coplanar layers (flat mesh): most tris share the same min depth — sort cannot help Hi-Z.
	if (static_cast<std::size_t>(near_min_count) * 4U > tri_count) {
		return false;
	}
	return true;
}

/**
 * Cheap uniform-depth probe on a few tris (flat mesh skips full min-depth pass).
 */
inline bool TrisLookUniformDepthSample(const std::vector<ScreenTri>& tris) {
	const std::size_t tri_count = tris.size();
	if (tri_count <= 1U) {
		return true;
	}
	constexpr std::size_t kSamples = 8U;
	const std::size_t sample_count = std::min(tri_count, kSamples);
	float depth_min = TriMinWindowDepthVertices(tris[0]);
	float depth_max = depth_min;
	for (std::size_t s = 1U; s < sample_count; ++s) {
		const std::size_t idx = (s * (tri_count - 1U)) / (sample_count - 1U);
		const float d = TriMinWindowDepthVertices(tris[idx]);
		depth_min = std::min(depth_min, d);
		depth_max = std::max(depth_max, d);
	}
	constexpr float kUniformDepthEps = 1e-5f;
	return depth_max - depth_min <= kUniformDepthEps;
}

/**
 *
 * Reuses thread-local bin vectors across calls (clear, keep capacity). AABB uses nested
 * min/max (no initializer_list). No per-pixel work in the bin pass.
 *
 * Depth-on: per-tile Hi-Z tracks max stored depth; triangles whose nearest tile z is
 * behind that occluder skip the pixel loop. tile_max advances from write tracking (no
 * per-triangle 64×64 depth rescan). tile_max_depth persists across RasterScreenTrisTiled
 * calls until depth is cleared (TileHiZEpoch bump). When depth is on and the draw has
 * meaningful overlap depth variation (not uniform-depth mesh, not occluder-prefix), tris
 * are sorted front-to-back before binning so nearer surfaces update tile_max first.
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

	MeshDrawScratch& scratch = GetMeshDrawScratch();
	auto& bins = scratch.bins;
	if (static_cast<int>(bins.size()) != tile_count) {
		bins.assign(static_cast<std::size_t>(tile_count), {});
	} else {
		for (auto& bin : bins) {
			bin.clear();
		}
	}

	auto& tile_max_depth = scratch.tile_max_depth;
	const std::uint32_t hiz_epoch = TileHiZEpoch();
	if (scratch.tile_hiz_epoch != hiz_epoch || static_cast<int>(tile_max_depth.size()) != tile_count) {
		if (static_cast<int>(tile_max_depth.size()) != tile_count) {
			tile_max_depth.assign(static_cast<std::size_t>(tile_count), 1.0f);
		} else {
			std::fill(tile_max_depth.begin(), tile_max_depth.end(), 1.0f);
		}
		scratch.tile_hiz_epoch = hiz_epoch;
	}

	const bool depth_on = depth != nullptr && depth->Allocated();
	constexpr float kHiZFarDepth = 1.0f - 1e-6f;
	const std::size_t tri_count = tris.size();

	// Front-to-back bin order when Hi-Z can reject overlapping farther tris in-draw.
	auto& tri_order = scratch.tri_order;
	auto& tri_min_depth = scratch.tri_min_depth;
	if (tri_order.size() != tri_count) {
		tri_order.resize(tri_count);
	}
	if (tri_min_depth.size() != tri_count) {
		tri_min_depth.resize(tri_count);
	}
	for (std::size_t i = 0U; i < tri_count; ++i) {
		tri_order[i] = static_cast<std::uint32_t>(i);
	}
	bool sort_front_to_back = false;
	if (depth_on && tri_count > 1U && !TrisLookUniformDepthSample(tris)) {
		for (std::size_t i = 0U; i < tri_count; ++i) {
			tri_min_depth[i] = TriMinWindowDepthVertices(tris[i]);
		}
		sort_front_to_back = ShouldSortTrisFrontToBack(tri_count, tri_min_depth);
	}
	if (sort_front_to_back) {
		std::sort(tri_order.begin(), tri_order.end(), [&tri_min_depth](const std::uint32_t a, const std::uint32_t b) {
			return tri_min_depth[a] < tri_min_depth[b];
		});
	}

	PixelBounds global_bounds{};

	for (const std::uint32_t ti : tri_order) {
		const ScreenTri& t = tris[ti];
		const float min_x = std::min(t.x0, std::min(t.x1, t.x2));
		const float min_y = std::min(t.y0, std::min(t.y1, t.y2));
		const float max_x = std::max(t.x0, std::max(t.x1, t.x2));
		const float max_y = std::max(t.y0, std::max(t.y1, t.y2));
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
		if (tx0 == tx1 && ty0 == ty1) {
			bins[static_cast<std::size_t>(ty0 * tiles_x + tx0)].push_back(ti);
			continue;
		}
		for (int ty = ty0; ty <= ty1; ++ty) {
			const int row = ty * tiles_x;
			for (int tx = tx0; tx <= tx1; ++tx) {
				bins[static_cast<std::size_t>(row + tx)].push_back(ti);
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
		float& tile_max = tile_max_depth[static_cast<std::size_t>(tile)];
		bool tile_hiz_scanned = depth_on && tile_max < kHiZFarDepth;
		for (const std::uint32_t idx : list) {
			if (tile_hiz_scanned && tile_max < kHiZFarDepth) {
				const bool reject = sort_front_to_back
					? TriTileDepthRejectMin(tri_min_depth[idx], tile_max)
					: TriTileDepthReject(tris[idx], tile_max);
				if (reject) {
					continue;
				}
			}
			const float tile_oc =
				(tile_hiz_scanned && tile_max < kHiZFarDepth) ? tile_max : 1.0f;
			float tri_write_max = -1.0f;
			if (RasterScreenTriTile(
					dst,
					depth,
					width,
					height,
					x0,
					y0,
					x1,
					y1,
					tris[idx],
					local,
					tile_oc,
					&tri_write_max)) {
				if (depth_on) {
					MergeTileHiZFromWrite(tile_max, tile_hiz_scanned, tri_write_max);
				}
			}
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
		float& tile_max = tile_max_depth[static_cast<std::size_t>(tile)];
		bool tile_hiz_scanned = depth_on && tile_max < kHiZFarDepth;
		for (const std::uint32_t idx : list) {
			if (tile_hiz_scanned && tile_max < kHiZFarDepth) {
				const bool reject = sort_front_to_back
					? TriTileDepthRejectMin(tri_min_depth[idx], tile_max)
					: TriTileDepthReject(tris[idx], tile_max);
				if (reject) {
					continue;
				}
			}
			const float tile_oc =
				(tile_hiz_scanned && tile_max < kHiZFarDepth) ? tile_max : 1.0f;
			float tri_write_max = -1.0f;
			if (RasterScreenTriTile(
					dst,
					depth,
					width,
					height,
					x0,
					y0,
					x1,
					y1,
					tris[idx],
					global_bounds,
					tile_oc,
					&tri_write_max)) {
				if (depth_on) {
					MergeTileHiZFromWrite(tile_max, tile_hiz_scanned, tri_write_max);
				}
			}
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
 * Reuses thread-local screen scratch (no shared verts — each corner transformed once per tri).
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
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
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
 * Transform + clip-emit flat mesh tris into an existing screen list (append, no tile fill).
 *
 * Reuses MeshDrawScratch clip/project buffers; caller must bin/fill after all instances.
 */
inline void AppendFlatMeshTris(
	std::vector<detail3d::ScreenTri>& screen,
	detail3d::MeshDrawScratch& scratch,
	const float* mvp16,
	const float* positions,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces,
	const int width,
	const int height) {
	if (positions == nullptr || mvp16 == nullptr || vertex_count < 3U) {
		return;
	}
	scratch.clip_verts.resize(vertex_count);
	scratch.outcodes.resize(vertex_count);
	scratch.px.resize(vertex_count);
	scratch.py.resize(vertex_count);
	scratch.zw.resize(vertex_count);
	detail3d::TransformMeshPositions(
		mvp16,
		positions,
		vertex_count,
		scratch.clip_verts.data(),
		scratch.outcodes.data(),
		scratch.px.data(),
		scratch.py.data(),
		scratch.zw.data(),
		nullptr,
		width,
		height);

	const detail3d::ClipVert* clip_verts = scratch.clip_verts.data();
	const int* codes = scratch.outcodes.data();
	const float* px = scratch.px.data();
	const float* py = scratch.py.data();
	const float* zw = scratch.zw.data();

	if (index_count > 0U && indices != nullptr) {
		const std::size_t tri_count = index_count / 3U;
		screen.reserve(screen.size() + tri_count);
		detail3d::EmitIndexedFlatRange(
			screen,
			clip_verts,
			codes,
			px,
			py,
			zw,
			indices,
			0U,
			tri_count,
			width,
			height,
			tri_color,
			cull_backfaces);
	} else {
		const std::size_t tri_count = vertex_count / 3U;
		screen.reserve(screen.size() + tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::uint32_t i0 = static_cast<std::uint32_t>(t * 3U);
			const std::uint32_t i1 = i0 + 1U;
			const std::uint32_t i2 = i0 + 2U;
			const int ca = codes[i0];
			const int cb = codes[i1];
			const int cc = codes[i2];
			if ((ca & cb & cc) != 0) {
				continue;
			}
			if ((ca | cb | cc) == 0) {
				detail3d::TryAppendFlatScreenTriFast(
					screen,
					px[i0], py[i0], zw[i0],
					px[i1], py[i1], zw[i1],
					px[i2], py[i2], zw[i2],
					tri_color,
					cull_backfaces);
				continue;
			}
			detail3d::EmitIndexedClipTri(
				screen,
				clip_verts,
				codes,
				px,
				py,
				zw,
				nullptr,
				i0,
				i1,
				i2,
				width,
				height,
				tri_color,
				cull_backfaces,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				nullptr,
				0,
				0);
		}
	}
}

/**
 * Transform + clip-emit textured mesh tris into an existing screen list (append, no tile fill).
 */
inline void AppendTexturedMeshTris(
	std::vector<detail3d::ScreenTri>& screen,
	detail3d::MeshDrawScratch& scratch,
	const float* mvp16,
	const float* positions,
	const float* uvs,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h,
	const bool cull_backfaces,
	const int width,
	const int height) {
	if (positions == nullptr || uvs == nullptr || mvp16 == nullptr || vertex_count < 3U) {
		return;
	}
	if (atlas_rgba == nullptr || atlas_w <= 0 || atlas_h <= 0) {
		return;
	}
	scratch.clip_verts.resize(vertex_count);
	scratch.outcodes.resize(vertex_count);
	scratch.px.resize(vertex_count);
	scratch.py.resize(vertex_count);
	scratch.zw.resize(vertex_count);
	scratch.iw.resize(vertex_count);
	detail3d::TransformMeshPositions(
		mvp16,
		positions,
		vertex_count,
		scratch.clip_verts.data(),
		scratch.outcodes.data(),
		scratch.px.data(),
		scratch.py.data(),
		scratch.zw.data(),
		scratch.iw.data(),
		width,
		height);

	constexpr std::uint32_t kUnusedFlat = 0xFFFFFFFFU;
	const detail3d::ClipVert* clip_verts = scratch.clip_verts.data();
	const int* codes = scratch.outcodes.data();
	const float* px = scratch.px.data();
	const float* py = scratch.py.data();
	const float* zw = scratch.zw.data();
	const float* iw = scratch.iw.data();

	if (index_count > 0U && indices != nullptr) {
		const std::size_t tri_count = index_count / 3U;
		screen.reserve(screen.size() + tri_count);
		detail3d::EmitIndexedTexturedRange(
			screen,
			clip_verts,
			codes,
			px,
			py,
			zw,
			iw,
			uvs,
			indices,
			0U,
			tri_count,
			width,
			height,
			kUnusedFlat,
			cull_backfaces,
			atlas_rgba,
			atlas_w,
			atlas_h);
	} else {
		const std::size_t tri_count = vertex_count / 3U;
		screen.reserve(screen.size() + tri_count);
		for (std::size_t t = 0U; t < tri_count; ++t) {
			const std::uint32_t i0 = static_cast<std::uint32_t>(t * 3U);
			const std::uint32_t i1 = i0 + 1U;
			const std::uint32_t i2 = i0 + 2U;
			const int ca = codes[i0];
			const int cb = codes[i1];
			const int cc = codes[i2];
			if ((ca & cb & cc) != 0) {
				continue;
			}
			const float* t0 = uvs + static_cast<std::size_t>(i0) * 2U;
			const float* t1 = uvs + static_cast<std::size_t>(i1) * 2U;
			const float* t2 = uvs + static_cast<std::size_t>(i2) * 2U;
			if ((ca | cb | cc) == 0) {
				detail3d::TryAppendTexturedScreenTriFast(
					screen,
					px[i0], py[i0], zw[i0], iw[i0], t0[0], t0[1],
					px[i1], py[i1], zw[i1], iw[i1], t1[0], t1[1],
					px[i2], py[i2], zw[i2], iw[i2], t2[0], t2[1],
					kUnusedFlat,
					cull_backfaces,
					atlas_rgba,
					atlas_w,
					atlas_h);
				continue;
			}
			detail3d::EmitIndexedClipTri(
				screen,
				clip_verts,
				codes,
				px,
				py,
				zw,
				iw,
				i0,
				i1,
				i2,
				width,
				height,
				kUnusedFlat,
				cull_backfaces,
				t0[0],
				t0[1],
				t1[0],
				t1[1],
				t2[0],
				t2[1],
				atlas_rgba,
				atlas_w,
				atlas_h);
		}
	}
}

/**
 * Triangles emitted by one mesh instance (indexed or triangle-list).
 */
inline std::size_t MeshInstanceTriCount(
	const std::size_t vertex_count,
	const std::size_t index_count) {
	if (index_count > 0U) {
		return index_count / 3U;
	}
	return vertex_count / 3U;
}

/**
 * Raster a retained mesh: positions (xyz) + optional indices through MVP into the tiled path.
 *
 * mvp16 = view_proj * model (column-major). Empty indices (index_count == 0) means a triangle
 * list over positions. Transforms each unique vertex once (AVX2 gather when available), then
 * outcode accept/reject + project-once for fully-visible verts. Reuses RasterScreenTrisTiled.
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
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
	AppendFlatMeshTris(
		screen,
		scratch,
		mvp16,
		positions,
		vertex_count,
		indices,
		index_count,
		tri_color,
		cull_backfaces,
		width,
		height);
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

/**
 * Raster one mesh with many model matrices: transform/clip/emit per instance, bin/fill once.
 *
 * models16: instance_count × 16 float32 column-major model matrices. view_proj16 is the
 * current world→clip matrix. N=1 uses the same emit path as RasterMeshWorld.
 */
inline void RasterMeshWorldMany(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const float* models16,
	const std::size_t instance_count,
	const float* positions,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint32_t tri_color,
	const bool cull_backfaces) {
	if (positions == nullptr || view_proj16 == nullptr || models16 == nullptr || vertex_count < 3U ||
		instance_count == 0U) {
		return;
	}
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
	const std::size_t tris_per_instance = MeshInstanceTriCount(vertex_count, index_count);
	screen.reserve(tris_per_instance * instance_count);
	float mvp[16];
	for (std::size_t inst = 0U; inst < instance_count; ++inst) {
		MulMat4ColumnMajor(view_proj16, models16 + inst * 16U, mvp);
		AppendFlatMeshTris(
			screen,
			scratch,
			mvp,
			positions,
			vertex_count,
			indices,
			index_count,
			tri_color,
			cull_backfaces,
			width,
			height);
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
 * Same transform-once / outcode / scratch path as RasterMeshWorld.
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
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
	AppendTexturedMeshTris(
		screen,
		scratch,
		mvp16,
		positions,
		uvs,
		vertex_count,
		indices,
		index_count,
		atlas_rgba,
		atlas_w,
		atlas_h,
		cull_backfaces,
		width,
		height);
	detail3d::RasterScreenTrisTiled(framebuffer, depth, screen);
}

/**
 * Textured mesh with many model matrices: transform/clip/emit per instance, bin/fill once.
 */
inline void RasterMeshTexturedWorldMany(
	FrameBuffer& framebuffer,
	DepthBuffer* depth,
	const float* view_proj16,
	const float* models16,
	const std::size_t instance_count,
	const float* positions,
	const float* uvs,
	const std::size_t vertex_count,
	const std::uint32_t* indices,
	const std::size_t index_count,
	const std::uint8_t* atlas_rgba,
	const int atlas_w,
	const int atlas_h,
	const bool cull_backfaces) {
	if (positions == nullptr || uvs == nullptr || view_proj16 == nullptr || models16 == nullptr ||
		vertex_count < 3U || instance_count == 0U) {
		return;
	}
	if (atlas_rgba == nullptr || atlas_w <= 0 || atlas_h <= 0) {
		return;
	}
	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
	const std::size_t tris_per_instance = MeshInstanceTriCount(vertex_count, index_count);
	screen.reserve(tris_per_instance * instance_count);
	float mvp[16];
	for (std::size_t inst = 0U; inst < instance_count; ++inst) {
		MulMat4ColumnMajor(view_proj16, models16 + inst * 16U, mvp);
		AppendTexturedMeshTris(
			screen,
			scratch,
			mvp,
			positions,
			uvs,
			vertex_count,
			indices,
			index_count,
			atlas_rgba,
			atlas_w,
			atlas_h,
			cull_backfaces,
			width,
			height);
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
	detail3d::MeshDrawScratch& scratch = detail3d::GetMeshDrawScratch();
	std::vector<detail3d::ScreenTri>& screen = scratch.screen;
	screen.clear();
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
