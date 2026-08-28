#include "engine/runtime/input_runtime.hpp"

#include <cstdio>

#ifdef __linux__
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace hyperlite {

void InputRuntime::BeginFrame(const InputState& snapshot, EventQueue* events) {
	previous_ = current_;
	current_ = snapshot;
	if (!has_previous_) {
		previous_ = current_;
		has_previous_ = true;
	}
	if (events == nullptr) {
		return;
	}
	if (current_.quit_requested && !previous_.quit_requested) {
		Event e{};
		e.kind = EventKind::WindowClose;
		events->Push(e);
	}
	for (int i = 0; i < 256; ++i) {
		if (current_.key_down[static_cast<std::size_t>(i)] != previous_.key_down[static_cast<std::size_t>(i)]) {
			Event e{};
			e.kind = EventKind::Key;
			e.a = i;
			e.b = current_.key_down[static_cast<std::size_t>(i)] ? 1 : 0;
			events->Push(e);
		}
	}
	if (current_.mouse_pos.x != previous_.mouse_pos.x || current_.mouse_pos.y != previous_.mouse_pos.y) {
		Event e{};
		e.kind = EventKind::MouseMove;
		e.a = current_.mouse_pos.x;
		e.b = current_.mouse_pos.y;
		events->Push(e);
	}
	for (int i = 0; i < static_cast<int>(kMouseButtonCount); ++i) {
		if (current_.mouse_buttons[static_cast<std::size_t>(i)] != previous_.mouse_buttons[static_cast<std::size_t>(i)]) {
			Event e{};
			e.kind = EventKind::MouseButton;
			e.a = i;
			e.b = current_.mouse_buttons[static_cast<std::size_t>(i)] ? 1 : 0;
			events->Push(e);
		}
	}
}

bool InputRuntime::KeyDown(const int vk) const {
	if (vk < 0 || vk >= 256) {
		return false;
	}
	return current_.key_down[static_cast<std::size_t>(vk)];
}

bool InputRuntime::KeyPressed(const int vk) const {
	if (vk < 0 || vk >= 256) {
		return false;
	}
	return current_.key_down[static_cast<std::size_t>(vk)] && !previous_.key_down[static_cast<std::size_t>(vk)];
}

bool InputRuntime::KeyReleased(const int vk) const {
	if (vk < 0 || vk >= 256) {
		return false;
	}
	return !current_.key_down[static_cast<std::size_t>(vk)] && previous_.key_down[static_cast<std::size_t>(vk)];
}

bool InputRuntime::MouseDown(const int button) const {
	if (button < 0 || button >= static_cast<int>(kMouseButtonCount)) {
		return false;
	}
	return current_.mouse_buttons[static_cast<std::size_t>(button)];
}

bool InputRuntime::MousePressed(const int button) const {
	if (button < 0 || button >= static_cast<int>(kMouseButtonCount)) {
		return false;
	}
	return current_.mouse_buttons[static_cast<std::size_t>(button)] &&
		!previous_.mouse_buttons[static_cast<std::size_t>(button)];
}

bool InputRuntime::MouseReleased(const int button) const {
	if (button < 0 || button >= static_cast<int>(kMouseButtonCount)) {
		return false;
	}
	return !current_.mouse_buttons[static_cast<std::size_t>(button)] &&
		previous_.mouse_buttons[static_cast<std::size_t>(button)];
}

int InputRuntime::MapAction(const InputAction& action) {
	for (std::size_t i = 0; i < actions_.size(); ++i) {
		if (actions_[i].name == action.name) {
			actions_[i] = action;
			return static_cast<int>(i);
		}
	}
	actions_.push_back(action);
	return static_cast<int>(actions_.size()) - 1;
}

int InputRuntime::FindAction(const char* name) const {
	if (name == nullptr) {
		return -1;
	}
	for (std::size_t i = 0; i < actions_.size(); ++i) {
		if (actions_[i].name == name) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

bool InputRuntime::ActionHeld(const InputAction& action) const {
	for (const int k : action.keys) {
		if (KeyDown(k)) {
			return true;
		}
	}
	for (const int b : action.mouse_buttons) {
		if (MouseDown(b)) {
			return true;
		}
	}
	if (action.gamepad_index >= 0 && action.gamepad_index < kMaxGamepads) {
		const GamepadState& pad = gamepads_[static_cast<std::size_t>(action.gamepad_index)];
		if (pad.connected) {
			for (const int b : action.gamepad_buttons) {
				if (b >= 0 && b < kGamepadButtonCount && pad.buttons[static_cast<std::size_t>(b)]) {
					return true;
				}
			}
		}
	}
	return false;
}

bool InputRuntime::ActionDown(const char* name) const {
	const int i = FindAction(name);
	return i >= 0 && ActionHeld(actions_[static_cast<std::size_t>(i)]);
}

bool InputRuntime::ActionPressed(const char* name) const {
	const int i = FindAction(name);
	if (i < 0) {
		return false;
	}
	// Edge: currently held, and was not held last frame (approximate via keys).
	const InputAction& a = actions_[static_cast<std::size_t>(i)];
	bool now = ActionHeld(a);
	bool was = false;
	for (const int k : a.keys) {
		if (k >= 0 && k < 256 && previous_.key_down[static_cast<std::size_t>(k)]) {
			was = true;
		}
	}
	return now && !was;
}

bool InputRuntime::ActionReleased(const char* name) const {
	const int i = FindAction(name);
	if (i < 0) {
		return false;
	}
	const InputAction& a = actions_[static_cast<std::size_t>(i)];
	bool now = ActionHeld(a);
	bool was = false;
	for (const int k : a.keys) {
		if (k >= 0 && k < 256 && previous_.key_down[static_cast<std::size_t>(k)]) {
			was = true;
		}
	}
	return !now && was;
}

const GamepadState& InputRuntime::Gamepad(const int index) const {
	static GamepadState empty{};
	if (index < 0 || index >= kMaxGamepads) {
		return empty;
	}
	return gamepads_[static_cast<std::size_t>(index)];
}

void InputRuntime::PollGamepads() {
#ifdef __linux__
	for (int i = 0; i < kMaxGamepads; ++i) {
		char path[64];
		std::snprintf(path, sizeof(path), "/dev/input/js%d", i);
		const int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			gamepads_[static_cast<std::size_t>(i)].connected = false;
			continue;
		}
		gamepads_[static_cast<std::size_t>(i)].connected = true;
		js_event ev{};
		while (read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
			if (ev.type & JS_EVENT_BUTTON) {
				if (ev.number < kGamepadButtonCount) {
					gamepads_[static_cast<std::size_t>(i)].buttons[ev.number] = ev.value != 0;
				}
			} else if (ev.type & JS_EVENT_AXIS) {
				if (ev.number < kGamepadAxisCount) {
					gamepads_[static_cast<std::size_t>(i)].axes[ev.number] = static_cast<float>(ev.value) / 32767.0f;
				}
			}
		}
		close(fd);
	}
#else
	(void)0;
#endif
}

} // namespace hyperlite
