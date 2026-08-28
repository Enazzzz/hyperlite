#pragma once

#include "engine/input_state.hpp"
#include "engine/runtime/events.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

inline constexpr int kMaxGamepads = 4;
inline constexpr int kGamepadButtonCount = 16;
inline constexpr int kGamepadAxisCount = 8;

/**
 * Snapshot of one gamepad (axes in [-1,1], buttons as 0/1).
 */
struct GamepadState {
	bool connected = false;
	std::array<float, kGamepadAxisCount> axes{};
	std::array<bool, kGamepadButtonCount> buttons{};
};

/**
 * Named action bound to keys, mouse buttons, and/or gamepad buttons.
 */
struct InputAction {
	std::string name{};
	std::vector<int> keys{};
	std::vector<int> mouse_buttons{};
	std::vector<int> gamepad_buttons{};
	int gamepad_index = 0;
};

/**
 * Native input: pressed/released edges, mappings, gamepad poll.
 *
 * Wraps Engine::GetInputState() after PollEvents. C++ owns polling.
 */
class InputRuntime {
public:
	/**
	 * Capture previous keys, then record the new snapshot and emit events.
	 */
	void BeginFrame(const InputState& snapshot, EventQueue* events);

	/** True while the virtual-key is held. */
	bool KeyDown(const int vk) const;

	/** True on the frame the virtual-key transitioned down. */
	bool KeyPressed(const int vk) const;

	/** True on the frame the virtual-key transitioned up. */
	bool KeyReleased(const int vk) const;

	bool MouseDown(const int button) const;
	bool MousePressed(const int button) const;
	bool MouseReleased(const int button) const;

	Int2 MousePos() const { return current_.mouse_pos; }
	Int2 MouseDelta() const { return current_.mouse_delta; }
	bool QuitRequested() const { return current_.quit_requested; }

	/** Bind or replace an action by name. Returns action index. */
	int MapAction(const InputAction& action);

	bool ActionDown(const char* name) const;
	bool ActionPressed(const char* name) const;
	bool ActionReleased(const char* name) const;

	const GamepadState& Gamepad(const int index) const;

	/**
	 * Poll Linux /dev/input/js* when present. No-op on other platforms or when absent.
	 */
	void PollGamepads();

	const InputState& Current() const { return current_; }

private:
	int FindAction(const char* name) const;
	bool ActionHeld(const InputAction& action) const;

	InputState previous_{};
	InputState current_{};
	std::array<GamepadState, kMaxGamepads> gamepads_{};
	std::vector<InputAction> actions_{};
	bool has_previous_ = false;
};

} // namespace hyperlite
