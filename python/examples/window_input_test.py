"""Window input test — mouse lock, fullscreen, and resize.

Controls:
  Enter              Capture mouse (FPS-style relative look)
  Left click         Capture mouse (when not captured)
  Esc                  Release mouse if captured, else quit
  F11                  Toggle borderless fullscreen
  1 / 2 / 3            Preset window sizes (640x360, 960x540, 1280x720)
  WASD                 Move the green box (frame-rate independent)
  Tab                  Print current state to console

While captured, use mouse movement to rotate the white look line from center.
Drag window borders to test live resize (framebuffer follows client area).
"""

import math
import time

import hyperlite

PRESETS = (
	(640, 360),
	(960, 540),
	(1280, 720),
)


def _edge_trigger(down: bool, was_down: bool) -> tuple[bool, bool]:
	"""Return (pressed_this_frame, new_was_down)."""
	return down and not was_down, down


def _clamp(value: int, low: int, high: int) -> int:
	"""Clamp integer to inclusive bounds."""
	return max(low, min(high, value))


def _draw_hud(
	engine: hyperlite.Engine,
	width: int,
	height: int,
	captured: bool,
	fullscreen: bool,
	yaw_deg: float,
	px: int,
	py: int,
	box: int,
) -> None:
	"""Draw visual state without a font renderer."""
	# Border — amber when captured, cyan when free.
	if captured:
		r, g, b = 255, 180, 60
	else:
		r, g, b = 60, 200, 255
	engine.rect_outline(2, 2, width - 4, height - 4, r, g, b, 255)

	# Corner ticks show live client size (scale with resize).
	tick = 16
	engine.line(0, 0, tick, 0, r, g, b, 255)
	engine.line(0, 0, 0, tick, r, g, b, 255)
	engine.line(width - 1, 0, width - 1 - tick, 0, r, g, b, 255)
	engine.line(width - 1, 0, width - 1, tick, r, g, b, 255)
	engine.line(0, height - 1, tick, height - 1, r, g, b, 255)
	engine.line(0, height - 1, 0, height - 1 - tick, r, g, b, 255)
	engine.line(width - 1, height - 1, width - 1 - tick, height - 1, r, g, b, 255)
	engine.line(width - 1, height - 1, width - 1, height - 1 - tick, r, g, b, 255)

	# Grid resizes with window.
	step = max(32, min(width, height) // 12)
	for gx in range(0, width, step):
		engine.line(gx, 0, gx, height - 1, 24, 28, 36, 255)
	for gy in range(0, height, step):
		engine.line(0, gy, width - 1, gy, 24, 28, 36, 255)

	# Movable box (WASD).
	engine.rect_fill(px, py, box, box, 80, 200, 120, 255)
	engine.rect_outline(px, py, box, box, 200, 255, 220, 255)

	cx = width // 2
	cy = height // 2

	# Look direction from accumulated yaw (mouse delta while captured).
	rad = math.radians(yaw_deg)
	look_len = min(width, height) // 3
	lx = cx + int(math.cos(rad) * look_len)
	ly = cy + int(math.sin(rad) * look_len)
	engine.line(cx, cy, lx, ly, 255, 255, 255, 255)
	engine.put_pixel(cx, cy, 255, 255, 255, 255)

	if captured:
		# Center reticle when mouse is locked.
		engine.line(cx - 10, cy, cx + 10, cy, 255, 220, 80, 255)
		engine.line(cx, cy - 10, cx, cy + 10, 255, 220, 80, 255)
	else:
		# Free cursor crosshair.
		mx, my = engine.mouse_pos()
		engine.line(mx - 8, my, mx + 8, my, 255, 220, 80, 255)
		engine.line(mx, my - 8, mx, my + 8, 255, 220, 80, 255)

	# Fullscreen indicator stripe at top.
	if fullscreen:
		engine.rect_fill(0, 0, width, 6, 180, 80, 255, 255)


def _print_banner() -> None:
	"""Print controls once at startup."""
	print("Hyperlite window input test")
	print("  Enter             capture mouse")
	print("  Left click        capture mouse")
	print("  Esc               release mouse, or quit")
	print("  F11               toggle fullscreen")
	print("  1 / 2 / 3         preset window sizes")
	print("  WASD              move green box")
	print("  Tab               dump state to console")
	print()


def _print_state(
	engine: hyperlite.Engine,
	yaw_deg: float,
	px: int,
	py: int,
) -> None:
	"""Log current window/input state."""
	w, h = engine.window_size()
	captured = engine.mouse_captured()
	fullscreen = engine.is_fullscreen()
	dx, dy = engine.mouse_delta()
	mx, my = engine.mouse_pos()
	lmb = engine.mouse_button_down(hyperlite.MouseButtons.Left)
	rmb = engine.mouse_button_down(hyperlite.MouseButtons.Right)
	print(
		f"size={w}x{h} fullscreen={fullscreen} captured={captured} "
		f"mouse=({mx},{my}) delta=({dx},{dy}) lmb={lmb} rmb={rmb} "
		f"yaw={yaw_deg:.1f} box=({px},{py})"
	)


def main() -> None:
	"""Run interactive window/input feature test."""
	_print_banner()

	engine = hyperlite.Engine(960, 540, "cpu", "Hyperlite Window Input Test")

	yaw_deg = -90.0
	look_sensitivity = 0.25
	move_speed = 4.0
	box = 28
	px = (960 - box) // 2
	py = (540 - box) // 2

	f11_was = False
	tab_was = False
	enter_was = False
	lmb_was = False
	preset_was = [False, False, False]

	last_status = time.perf_counter()
	frame_count = 0

	while engine.is_running():
		engine.poll_events()

		width, height = engine.window_size()
		captured = engine.mouse_captured()
		fullscreen = engine.is_fullscreen()

		# Esc: unlock first, then quit.
		if engine.key_down(hyperlite.Keys.Escape):
			if captured:
				engine.set_mouse_captured(False)
				print("mouse released (Esc)")
			else:
				break

		# Enter captures mouse.
		enter_down = engine.key_down(hyperlite.Keys.Return)
		enter_pressed, enter_was = _edge_trigger(enter_down, enter_was)
		if enter_pressed and not captured:
			engine.set_mouse_captured(True)
			print("mouse captured (Enter)")

		# F11 fullscreen toggle.
		f11_down = engine.key_down(hyperlite.Keys.F11)
		f11_pressed, f11_was = _edge_trigger(f11_down, f11_was)
		if f11_pressed:
			engine.set_fullscreen(not fullscreen)
			w, h = engine.window_size()
			print(f"fullscreen={engine.is_fullscreen()} size={w}x{h}")

		# Preset sizes (only when not fullscreen).
		for idx, (pw, ph) in enumerate(PRESETS):
			key_down = engine.key_down(ord("1") + idx)
			pressed, preset_was[idx] = _edge_trigger(key_down, preset_was[idx])
			if pressed and not engine.is_fullscreen():
				engine.set_window_size(pw, ph)
				width, height = engine.window_size()
				px = _clamp(px, 0, max(0, width - box))
				py = _clamp(py, 0, max(0, height - box))
				print(f"preset {idx + 1} -> {width}x{height}")

		# Tab dumps state.
		tab_down = engine.key_down(hyperlite.Keys.Tab)
		tab_pressed, tab_was = _edge_trigger(tab_down, tab_was)
		if tab_pressed:
			_print_state(engine, yaw_deg, px, py)

		if not captured:
			lmb_down = engine.mouse_button_down(hyperlite.MouseButtons.Left)
			lmb_pressed, lmb_was = _edge_trigger(lmb_down, lmb_was)
			if lmb_pressed:
				engine.set_mouse_captured(True)
				print("mouse captured (click)")
		else:
			lmb_was = engine.mouse_button_down(hyperlite.MouseButtons.Left)

		if captured:
			dx, dy = engine.mouse_delta()
			yaw_deg += dx * look_sensitivity
			# Pitch could be tracked here for a full FPS test; yaw is enough to verify delta.

		move_step = move_speed * max(engine.delta_time() * 60.0, 0.0)
		if engine.key_down(hyperlite.Keys.W):
			py -= int(move_step)
		if engine.key_down(hyperlite.Keys.S):
			py += int(move_step)
		if engine.key_down(hyperlite.Keys.A):
			px -= int(move_step)
		if engine.key_down(hyperlite.Keys.D):
			px += int(move_step)
		px = _clamp(px, 0, max(0, width - box))
		py = _clamp(py, 0, max(0, height - box))

		engine.begin_frame()
		engine.clear(14, 16, 22, 255)
		_draw_hud(engine, width, height, captured, fullscreen, yaw_deg, px, py, box)
		engine.tick()

		frame_count += 1
		now = time.perf_counter()
		if now - last_status >= 2.0:
			fps = frame_count / (now - last_status)
			print(
				f"fps={fps:.0f} size={width}x{height} "
				f"fullscreen={fullscreen} captured={captured} yaw={yaw_deg:.0f}"
			)
			frame_count = 0
			last_status = now


if __name__ == "__main__":
	main()
