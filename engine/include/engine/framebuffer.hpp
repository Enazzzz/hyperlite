#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/types.hpp"

namespace hyperlite {

/**
 * Contiguous RGBA framebuffer with tile-based dirty tracking.
 */
class FrameBuffer {
public:
	static constexpr int kDirtyTileSize = 64;

	/**
	 * Construct an empty or sized framebuffer.
	 */
	explicit FrameBuffer(int width = 0, int height = 0) {
		Resize(width, height);
	}

	/**
	 * Resize and reinitialize pixel storage.
	 */
	void Resize(int width, int height) {
		width_ = width;
		height_ = height;
		pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U, 0U);
		RebuildDirtyTiles();
	}

	/**
	 * Fill the framebuffer with one color.
	 */
	void Clear(const Color color) {
		const std::uint32_t packed = hyperlite::PackColor(color);
		const std::size_t pixels_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
		auto* dst = reinterpret_cast<std::uint32_t*>(pixels_.data());
		std::fill_n(dst, pixels_count, packed);
	}

	/**
	 * Return mutable pixel pointer (RGBA8).
	 */
	std::uint8_t* Data() {
		return pixels_.data();
	}

	/**
	 * Return immutable pixel pointer (RGBA8).
	 */
	const std::uint8_t* Data() const {
		return pixels_.data();
	}

	/**
	 * Return pixel buffer byte-size.
	 */
	std::size_t SizeBytes() const {
		return pixels_.size();
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
	 * Pixel count in the active framebuffer.
	 */
	std::size_t PixelCount() const {
		return static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
	}

	/**
	 * Reset dirty tracking at frame start.
	 */
	void ResetDirty() {
		dirty_active_ = false;
		if (!dirty_tiles_.empty()) {
			std::fill(dirty_tiles_.begin(), dirty_tiles_.end(), 0U);
		}
	}

	/**
	 * Expand the dirty rectangle to include a clipped region.
	 */
	void NoteDirtyRect(const int x0, const int y0, const int x1, const int y1) {
		if (x1 <= x0 || y1 <= y0) {
			return;
		}
		if (!dirty_active_) {
			dirty_x0_ = x0;
			dirty_y0_ = y0;
			dirty_x1_ = x1;
			dirty_y1_ = y1;
			dirty_active_ = true;
		} else {
			dirty_x0_ = std::min(dirty_x0_, x0);
			dirty_y0_ = std::min(dirty_y0_, y0);
			dirty_x1_ = std::max(dirty_x1_, x1);
			dirty_y1_ = std::max(dirty_y1_, y1);
		}
		MarkDirtyTiles(x0, y0, x1, y1);
	}

	/**
	 * Whether any pixels were touched this frame.
	 */
	bool DirtyActive() const {
		return dirty_active_;
	}

	/**
	 * Return the inclusive-exclusive dirty bounds in pixel coordinates.
	 */
	void DirtyBounds(int& x0, int& y0, int& x1, int& y1) const {
		x0 = dirty_x0_;
		y0 = dirty_y0_;
		x1 = dirty_x1_;
		y1 = dirty_y1_;
	}

	/**
	 * Return dirty 64px tile indices for partial present iteration.
	 */
	const std::vector<std::uint32_t>& DirtyTiles() const {
		return dirty_tile_indices_;
	}

	/**
	 * Rebuild the list of dirty tile indices after rasterization completes.
	 */
	void FinalizeDirtyTiles() {
		dirty_tile_indices_.clear();
		if (dirty_tiles_.empty()) {
			return;
		}
		for (std::size_t tile = 0U; tile < dirty_tiles_.size(); ++tile) {
			if (dirty_tiles_[tile] != 0U) {
				dirty_tile_indices_.push_back(static_cast<std::uint32_t>(tile));
			}
		}
	}

	/**
	 * Convert a tile index to pixel bounds (inclusive-exclusive).
	 */
	void TileBounds(const std::uint32_t tile_index, int& x0, int& y0, int& x1, int& y1) const {
		const int tiles_x = std::max(1, (width_ + kDirtyTileSize - 1) / kDirtyTileSize);
		const int tx = static_cast<int>(tile_index) % tiles_x;
		const int ty = static_cast<int>(tile_index) / tiles_x;
		x0 = tx * kDirtyTileSize;
		y0 = ty * kDirtyTileSize;
		x1 = std::min(x0 + kDirtyTileSize, width_);
		y1 = std::min(y0 + kDirtyTileSize, height_);
	}

private:
	/**
	 * Allocate dirty-tile storage for the current framebuffer size.
	 */
	void RebuildDirtyTiles() {
		const int tiles_x = std::max(1, (width_ + kDirtyTileSize - 1) / kDirtyTileSize);
		const int tiles_y = std::max(1, (height_ + kDirtyTileSize - 1) / kDirtyTileSize);
		dirty_tiles_.assign(static_cast<std::size_t>(tiles_x) * static_cast<std::size_t>(tiles_y), 0U);
		dirty_tile_indices_.clear();
	}

	/**
	 * Mark all tiles overlapped by one dirty rectangle.
	 */
	void MarkDirtyTiles(const int x0, const int y0, const int x1, const int y1) {
		if (dirty_tiles_.empty()) {
			return;
		}
		const int tiles_x = std::max(1, (width_ + kDirtyTileSize - 1) / kDirtyTileSize);
		const int tx0 = std::max(0, x0 / kDirtyTileSize);
		const int ty0 = std::max(0, y0 / kDirtyTileSize);
		const int tx1 = std::min(tiles_x - 1, (x1 - 1) / kDirtyTileSize);
		const int ty1 = std::min(std::max(1, (height_ + kDirtyTileSize - 1) / kDirtyTileSize) - 1, (y1 - 1) / kDirtyTileSize);
		for (int ty = ty0; ty <= ty1; ++ty) {
			for (int tx = tx0; tx <= tx1; ++tx) {
				dirty_tiles_[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tiles_x) + static_cast<std::size_t>(tx)] = 1U;
			}
		}
	}

	int width_ = 0;
	int height_ = 0;
	std::vector<std::uint8_t> pixels_{};
	bool dirty_active_ = false;
	int dirty_x0_ = 0;
	int dirty_y0_ = 0;
	int dirty_x1_ = 0;
	int dirty_y1_ = 0;
	std::vector<std::uint8_t> dirty_tiles_{};
	std::vector<std::uint32_t> dirty_tile_indices_{};
};

} // namespace hyperlite
