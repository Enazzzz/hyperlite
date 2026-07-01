#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/types.hpp"

namespace hyperlite {

/**
 * Contiguous RGBA framebuffer with deterministic ownership semantics.
 */
class FrameBuffer {
public:
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

private:
	int width_ = 0;
	int height_ = 0;
	std::vector<std::uint8_t> pixels_{};
};

} // namespace hyperlite
