#pragma once

#include <cstdint>
#include <cstring>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace hyperlite::raster {

/**
 * Extract one 8-bit channel from a packed RGBA pixel.
 */
inline std::uint8_t Channel(const std::uint32_t packed, const int shift) {
	return static_cast<std::uint8_t>((packed >> shift) & 0xFFU);
}

/**
 * Fast straight-alpha composite (src over dst) using parallel RB/AG lanes.
 */
inline std::uint32_t BlendOver(const std::uint32_t dst, const std::uint32_t src) {
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
 * Write one pixel with optional alpha blending.
 */
inline void StorePixel(std::uint32_t* dst, const std::uint32_t packed_color) {
	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
		*dst = packed_color;
	} else if (sa != 0U) {
		*dst = BlendOver(*dst, packed_color);
	}
}

/**
 * Fill a span with one constant-alpha color (scalar, 4x unrolled).
 */
inline void FillConstantAlphaSpan(std::uint32_t* dst, std::size_t count, const std::uint32_t packed_color) {
	std::size_t i = 0U;
	for (; i + 4U <= count; i += 4U) {
		dst[i + 0U] = BlendOver(dst[i + 0U], packed_color);
		dst[i + 1U] = BlendOver(dst[i + 1U], packed_color);
		dst[i + 2U] = BlendOver(dst[i + 2U], packed_color);
		dst[i + 3U] = BlendOver(dst[i + 3U], packed_color);
	}
	for (; i < count; ++i) {
		dst[i] = BlendOver(dst[i], packed_color);
	}
}

/**
 * Fill a contiguous run with one packed color (opaque uses AVX2 when available).
 */
inline void FillSpan(std::uint32_t* dst, std::size_t count, const std::uint32_t packed_color) {
	if (count == 0U) {
		return;
	}
	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U) {
#if defined(__AVX2__)
		const __m256i value = _mm256_set1_epi32(static_cast<int>(packed_color));
		std::size_t i = 0U;
		for (; i + 8U <= count; i += 8U) {
			_mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), value);
		}
		for (; i < count; ++i) {
			dst[i] = packed_color;
		}
#else
		for (std::size_t i = 0U; i < count; ++i) {
			dst[i] = packed_color;
		}
#endif
		return;
	}
	if (sa == 0U) {
		return;
	}
	FillConstantAlphaSpan(dst, count, packed_color);
}

/**
 * Fill an opaque rectangle by writing one row then memcpy-replicating it.
 */
inline void FillRectOpaque(
	std::uint32_t* dst,
	const int width,
	const int x0,
	const int y0,
	const int x1,
	const int y1,
	const std::uint32_t packed_color) {
	const std::size_t row_pixels = static_cast<std::size_t>(x1 - x0);
	if (row_pixels == 0U) {
		return;
	}
	std::uint32_t* row0 = dst + (static_cast<std::size_t>(y0) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x0);
	FillSpan(row0, row_pixels, packed_color);
	const std::size_t row_bytes = row_pixels * sizeof(std::uint32_t);
	for (int row = y0 + 1; row < y1; ++row) {
		std::uint32_t* row_ptr = dst + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x0);
		std::memcpy(row_ptr, row0, row_bytes);
	}
}

/**
 * Alpha-composite one RGBA8 source row over a destination row.
 */
inline void BlendRowRgba8(std::uint32_t* dst, const std::uint8_t* src, const int width) {
	for (int x = 0; x < width; ++x) {
		const std::size_t idx = static_cast<std::size_t>(x);
		const std::uint8_t* px = src + (idx * 4U);
		const std::uint32_t src_px =
			static_cast<std::uint32_t>(px[0]) |
			(static_cast<std::uint32_t>(px[1]) << 8U) |
			(static_cast<std::uint32_t>(px[2]) << 16U) |
			(static_cast<std::uint32_t>(px[3]) << 24U);
		StorePixel(dst + idx, src_px);
	}
}

/**
 * Copy one fully-opaque RGBA8 row into packed uint32 destination pixels.
 */
inline void CopyOpaqueRowRgba8(std::uint32_t* dst, const std::uint8_t* src, const int width) {
	const auto* src32 = reinterpret_cast<const std::uint32_t*>(src);
#if defined(__AVX2__)
	int x = 0;
	for (; x + 8 <= width; x += 8) {
		_mm256_storeu_si256(
			reinterpret_cast<__m256i*>(dst + x),
			_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src32 + x)));
	}
	for (; x < width; ++x) {
		dst[x] = src32[x];
	}
#else
	for (int x = 0; x < width; ++x) {
		dst[x] = src32[x];
	}
#endif
}

/**
 * Copy or blend one RGBA8 source row in a single pass.
 */
inline void CopyRowRgba8(std::uint32_t* dst, const std::uint8_t* src, const int width) {
	int x = 0;
	for (; x < width; ++x) {
		const std::uint8_t* px = src + (static_cast<std::size_t>(x) * 4U);
		const std::uint8_t alpha = px[3U];
		if (alpha == 0U) {
			continue;
		}
		const std::uint32_t src_px =
			static_cast<std::uint32_t>(px[0]) |
			(static_cast<std::uint32_t>(px[1]) << 8U) |
			(static_cast<std::uint32_t>(px[2]) << 16U) |
			(static_cast<std::uint32_t>(alpha) << 24U);
		if (alpha == 255U) {
			dst[x] = src_px;
		} else {
			dst[x] = BlendOver(dst[x], src_px);
		}
	}
}

} // namespace hyperlite::raster
