#pragma once

#include "engine/tile_hiz_state.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Float32 depth plane matching the framebuffer size (GL-style [0,1], clear = 1.0).
 *
 * Stored in 128×128 panel (tile) order matching kTriRasterTileSize bins so a raster
 * tile's z samples are contiguous in memory. Color stays row-major for present blit.
 *
 * Allocated only when depth testing is enabled. 2D draws never read or write this buffer.
 */
class DepthBuffer {
public:
	/** Panel edge length; must match raster::detail3d::kTriRasterTileSize (128). */
	static constexpr int kPanelSize = 128;
	static constexpr int kPanelSamples = kPanelSize * kPanelSize;

	/**
	 * Construct an empty or sized depth buffer (values cleared to 1.0).
	 */
	explicit DepthBuffer(int width = 0, int height = 0) {
		Resize(width, height);
	}

	/**
	 * Resize storage to match the framebuffer and clear to far depth.
	 */
	void Resize(const int width, const int height) {
		width_ = std::max(0, width);
		height_ = std::max(0, height);
		tiles_x_ = width_ > 0 ? (width_ + kPanelSize - 1) / kPanelSize : 0;
		tiles_y_ = height_ > 0 ? (height_ + kPanelSize - 1) / kPanelSize : 0;
		depth_.assign(
			static_cast<std::size_t>(tiles_x_) * static_cast<std::size_t>(tiles_y_) * kPanelSamples,
			1.0f);
		raster::detail3d::InvalidateTileHiZ();
	}

	/**
	 * Fill every sample (including panel padding) with far depth (1.0).
	 */
	void Clear(const float value = 1.0f) {
		std::fill(depth_.begin(), depth_.end(), value);
		raster::detail3d::InvalidateTileHiZ();
	}

	/**
	 * Release storage (used when depth is disabled).
	 */
	void Reset() {
		width_ = 0;
		height_ = 0;
		tiles_x_ = 0;
		tiles_y_ = 0;
		depth_.clear();
		depth_.shrink_to_fit();
		raster::detail3d::InvalidateTileHiZ();
	}

	/**
	 * Whether storage is allocated.
	 */
	bool Allocated() const {
		return !depth_.empty();
	}

	/**
	 * Width in pixels.
	 */
	int Width() const {
		return width_;
	}

	/**
	 * Height in pixels.
	 */
	int Height() const {
		return height_;
	}

	/**
	 * Panel columns in the depth grid.
	 */
	int TilesX() const {
		return tiles_x_;
	}

	/**
	 * Panel rows in the depth grid.
	 */
	int TilesY() const {
		return tiles_y_;
	}

	/**
	 * Mutable depth samples (panel-major).
	 */
	float* Data() {
		return depth_.data();
	}

	/**
	 * Immutable depth samples (panel-major).
	 */
	const float* Data() const {
		return depth_.data();
	}

	/**
	 * Sample count (includes panel padding at framebuffer edges).
	 */
	std::size_t SampleCount() const {
		return depth_.size();
	}

	/**
	 * Panel-major index for one in-bounds pixel.
	 */
	std::size_t Index(const int x, const int y) const {
		const int tile_x = x / kPanelSize;
		const int tile_y = y / kPanelSize;
		const int local_x = x - tile_x * kPanelSize;
		const int local_y = y - tile_y * kPanelSize;
		return PanelOffset(tile_x, tile_y) +
			static_cast<std::size_t>(local_y) * static_cast<std::size_t>(kPanelSize) +
			static_cast<std::size_t>(local_x);
	}

	/**
	 * Mutable pointer to one pixel's depth (caller guarantees in-bounds).
	 */
	float* Ptr(const int x, const int y) {
		return depth_.data() + Index(x, y);
	}

	/**
	 * Immutable pointer to one pixel's depth (caller guarantees in-bounds).
	 */
	const float* Ptr(const int x, const int y) const {
		return depth_.data() + Index(x, y);
	}

	/**
	 * Start of one 128×128 panel (tile_x, tile_y in panel coordinates).
	 */
	float* PanelBase(const int tile_x, const int tile_y) {
		return depth_.data() + PanelOffset(tile_x, tile_y);
	}

	/**
	 * Start of one 128×128 panel (tile_x, tile_y in panel coordinates).
	 */
	const float* PanelBase(const int tile_x, const int tile_y) const {
		return depth_.data() + PanelOffset(tile_x, tile_y);
	}

	/**
	 * GL-style less-equal depth test and conditional write at one pixel.
	 *
	 * Returns true when the color write should proceed.
	 */
	bool TestAndWrite(const int x, const int y, const float z) {
		if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width_) ||
			static_cast<unsigned int>(y) >= static_cast<unsigned int>(height_)) {
			return false;
		}
		return TestAndWriteIndex(Index(x, y), z);
	}

	/**
	 * Depth test/write at a precomputed panel index (caller guarantees in-bounds).
	 *
	 * Hot path for clipped 3D line/tri rasters that already validated x,y.
	 */
	bool TestAndWriteIndex(const std::size_t index, const float z) {
		float& slot = depth_[index];
		if (z <= slot) {
			slot = z;
			return true;
		}
		return false;
	}

	/**
	 * Read depth at one pixel (1.0 if out of bounds).
	 */
	float At(const int x, const int y) const {
		if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width_) ||
			static_cast<unsigned int>(y) >= static_cast<unsigned int>(height_)) {
			return 1.0f;
		}
		return depth_[Index(x, y)];
	}

private:
	/**
	 * Byte offset of panel (tile_x, tile_y) in depth_.
	 */
	std::size_t PanelOffset(const int tile_x, const int tile_y) const {
		return (static_cast<std::size_t>(tile_y) * static_cast<std::size_t>(tiles_x_) +
				static_cast<std::size_t>(tile_x)) *
			static_cast<std::size_t>(kPanelSamples);
	}

	int width_ = 0;
	int height_ = 0;
	int tiles_x_ = 0;
	int tiles_y_ = 0;
	std::vector<float> depth_{};
};

} // namespace hyperlite
