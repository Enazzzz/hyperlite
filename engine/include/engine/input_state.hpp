#pragma once

#include <array>
#include <cstdint>

#include "engine/types.hpp"

namespace hyperlite {

/** Mouse button indices used by mouse_buttons and Python MouseButtons. */
enum class MouseButton : int {
	Left = 0,
	Right = 1,
	Middle = 2,
	X1 = 3,
	X2 = 4,
};

inline constexpr std::size_t kMouseButtonCount = 5;

/**
 * Input state snapshot updated by the platform layer.
 */
struct InputState {
	std::array<bool, 256> key_down{};
	std::array<bool, kMouseButtonCount> mouse_buttons{};
	Int2 mouse_pos{0, 0};
	Int2 mouse_delta{0, 0};
	bool mouse_captured = false;
	bool quit_requested = false;
};

} // namespace hyperlite
