#include "engine/runtime/macos_gamepad.hpp"

#import <GameController/GameController.h>

#include <algorithm>

namespace hyperlite {
namespace {

/**
 * Map a GameController button (nil-safe) onto a 0/1 slot.
 */
void SetButton(GamepadState& pad, const int index, GCControllerButtonInput* button) {
	if (index < 0 || index >= kGamepadButtonCount) {
		return;
	}
	pad.buttons[static_cast<std::size_t>(index)] = button != nil && button.isPressed;
}

/**
 * Axis in [-1, 1]; nil reads as 0.
 */
float AxisValue(GCControllerAxisInput* axis) {
	return axis != nil ? static_cast<float>(axis.value) : 0.0f;
}

} // namespace

void PollMacosGamepads(GamepadState* pads, const int count) {
	if (pads == nullptr || count <= 0) {
		return;
	}
	static bool discovery_started = false;
	if (!discovery_started) {
		[GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
		discovery_started = true;
	}

	NSArray<GCController*>* controllers = [GCController controllers];
	const int n = std::min(count, static_cast<int>(controllers.count));
	for (int i = 0; i < count; ++i) {
		pads[i] = GamepadState{};
	}
	for (int i = 0; i < n; ++i) {
		GCController* gc = controllers[static_cast<NSUInteger>(i)];
		GamepadState& pad = pads[i];
		pad.connected = true;
		GCExtendedGamepad* ext = gc.extendedGamepad;
		if (ext == nil) {
			GCMicroGamepad* micro = gc.microGamepad;
			if (micro != nil) {
				SetButton(pad, 0, micro.buttonA);
				SetButton(pad, 1, micro.buttonX);
				pad.axes[0] = AxisValue(micro.dpad.xAxis);
				pad.axes[1] = AxisValue(micro.dpad.yAxis);
			}
			continue;
		}
		// Xbox-style layout matching common Linux js* numbering.
		SetButton(pad, 0, ext.buttonA);
		SetButton(pad, 1, ext.buttonB);
		SetButton(pad, 2, ext.buttonX);
		SetButton(pad, 3, ext.buttonY);
		SetButton(pad, 4, ext.leftShoulder);
		SetButton(pad, 5, ext.rightShoulder);
		SetButton(pad, 6, ext.buttonOptions);
		SetButton(pad, 7, ext.buttonMenu);
		SetButton(pad, 8, ext.leftThumbstickButton);
		SetButton(pad, 9, ext.rightThumbstickButton);
		SetButton(pad, 10, ext.dpad.up);
		SetButton(pad, 11, ext.dpad.down);
		SetButton(pad, 12, ext.dpad.left);
		SetButton(pad, 13, ext.dpad.right);
		pad.axes[0] = AxisValue(ext.leftThumbstick.xAxis);
		pad.axes[1] = -AxisValue(ext.leftThumbstick.yAxis); // match typical Y-down gamepads
		pad.axes[2] = AxisValue(ext.rightThumbstick.xAxis);
		pad.axes[3] = -AxisValue(ext.rightThumbstick.yAxis);
		pad.axes[4] = ext.leftTrigger != nil ? static_cast<float>(ext.leftTrigger.value) : 0.0f;
		pad.axes[5] = ext.rightTrigger != nil ? static_cast<float>(ext.rightTrigger.value) : 0.0f;
	}
}

} // namespace hyperlite
