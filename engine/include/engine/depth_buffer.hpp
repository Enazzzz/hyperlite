#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Float32 depth plane matching the framebuffer size (GL-style [0,1], clear = 1.0).
 *
 * Allocated only when depth testing is enabled. 2D draws never read or write this buffer.
 */
class DepthBuffer {
public:
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
		depth_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 1.0f);
	}

	/**
	 * Fill every sample with far depth (1.0).
	 */
	void Clear(const float value = 1.0f) {
		std::fill(depth_.begin(), depth_.end(), value);
	}

	/**
	 * Release storage (used when depth is disabled).
	 */
	void Reset() {
		width_ = 0;
		height_ = 0;
		depth_.clear();
		depth_.shrink_to_fit();
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
	 * Mutable depth samples (row-major).
	 */
	float* Data() {
		return depth_.data();
	}

	/**
	 * Immutable depth samples (row-major).
	 */
	const float* Data() const {
		return depth_.data();
	}

	/**
	 * Sample count.
	 */
	std::size_t SampleCount() const {
		return depth_.size();
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
		float& slot = depth_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
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
		return depth_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
	}

private:
	int width_ = 0;
	int height_ = 0;
	std::vector<float> depth_{};
};

} // namespace hyperlite
