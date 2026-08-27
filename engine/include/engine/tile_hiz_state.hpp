#pragma once

#include <cstdint>

namespace hyperlite::raster::detail3d {

/**
 * Monotonic epoch bumped whenever the depth buffer is cleared or resized.
 *
 * RasterScreenTrisTiled resets per-tile Hi-Z only when this epoch advances.
 */
inline std::uint32_t& TileHiZEpoch() {
	static std::uint32_t epoch = 0U;
	return epoch;
}

/**
 * Invalidate persisted per-tile Hi-Z (call from every depth clear / resize path).
 */
inline void InvalidateTileHiZ() {
	++TileHiZEpoch();
}

} // namespace hyperlite::raster::detail3d
