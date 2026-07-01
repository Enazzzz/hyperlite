#pragma once

#include <cstdint>

namespace hyperlite {

/**
 * Packed RGBA color utility used by all draw commands.
 */
struct Color {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
	std::uint8_t a;
};

/**
 * Pack RGBA bytes into a single 32-bit value.
 */
constexpr std::uint32_t PackColor(const Color color) {
	return static_cast<std::uint32_t>(color.r) |
		(static_cast<std::uint32_t>(color.g) << 8U) |
		(static_cast<std::uint32_t>(color.b) << 16U) |
		(static_cast<std::uint32_t>(color.a) << 24U);
}

/**
 * 2D integer coordinate helper for input and drawing APIs.
 */
struct Int2 {
	int x;
	int y;
};

} // namespace hyperlite
