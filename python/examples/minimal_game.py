"""Minimal Engine loop — move a square with WASD, quit with Escape.

Shows the core Hyperlite pattern: poll input, queue draw commands each frame,
present. No scene graph — you draw everything explicitly every frame.

This path keeps a Python `while` loop. For a native loop with the same demo,
see `native_game.py`.

Headless: HYPERLITE_HEADLESS=1 python minimal_game.py
"""

import ctypes
import os
import time

import hyperlite


def _enable_precise_sleep() -> None:
	"""Request 1 ms timer resolution on Windows so sub-16 ms sleeps are usable."""
	try:
		ctypes.windll.winmm.timeBeginPeriod(1)
	except (AttributeError, OSError):
		pass


def _disable_precise_sleep() -> None:
	"""Restore default timer resolution."""
	try:
		ctypes.windll.winmm.timeEndPeriod(1)
	except (AttributeError, OSError):
		pass


def _wait_until(deadline: float) -> None:
	"""Sleep until deadline; spin the last ~1 ms for an accurate cap."""
	while True:
		remaining = deadline - time.perf_counter()
		if remaining <= 0.0:
			return
		if remaining > 0.003:
			time.sleep(remaining - 0.002)


WIDTH = 960
HEIGHT = 540
PLAYER_SIZE = 32
PLAYER_SPEED = 4.0
FPS_CAP = 165.0


def main() -> None:
	"""Run a simple top-down mover demo."""
	_enable_precise_sleep()
	try:
		_run_game()
	finally:
		_disable_precise_sleep()


def _run_game() -> None:
	"""Main loop body (separated so timer cleanup always runs)."""
	present = "headless" if os.environ.get("HYPERLITE_HEADLESS") else "auto"
	engine = hyperlite.Engine(WIDTH, HEIGHT, "cpu", "Hyperlite Minimal Game", present=present)

	px = (WIDTH - PLAYER_SIZE) // 2
	py = (HEIGHT - PLAYER_SIZE) // 2

	frame_count = 0
	total_frames = 0
	last_fps_tick = time.perf_counter()

	# Tab toggles sleep-based FPS cap (off = uncapped).
	fps_limited = False
	tab_was_down = False
	target_dt = 1.0 / FPS_CAP
	max_frames = 8 if present == "headless" else 0

	while engine.is_running():
		frame_start = time.perf_counter()
		engine.poll_events()

		if engine.key_down(hyperlite.Keys.Escape):
			break

		tab_down = engine.key_down(hyperlite.Keys.Tab)
		if tab_down and not tab_was_down:
			fps_limited = not fps_limited
			state = f"ON ({FPS_CAP:.0f})" if fps_limited else "OFF"
			print(f"fps_limit={state}")
		tab_was_down = tab_down

		move_step = PLAYER_SPEED * max(engine.delta_time() * 60.0, 0.0)
		if engine.key_down(hyperlite.Keys.W):
			py -= int(move_step)
		if engine.key_down(hyperlite.Keys.S):
			py += int(move_step)
		if engine.key_down(hyperlite.Keys.A):
			px -= int(move_step)
		if engine.key_down(hyperlite.Keys.D):
			px += int(move_step)

		px = max(0, min(WIDTH - PLAYER_SIZE, px))
		py = max(0, min(HEIGHT - PLAYER_SIZE, py))

		engine.begin_frame()
		engine.clear(18, 20, 28, 255)

		for gx in range(0, WIDTH, 64):
			engine.line(gx, 0, gx, HEIGHT - 1, 28, 32, 44, 255)
		for gy in range(0, HEIGHT, 64):
			engine.line(0, gy, WIDTH - 1, gy, 28, 32, 44, 255)

		engine.rect_fill(px, py, PLAYER_SIZE, PLAYER_SIZE, 80, 200, 120, 255)
		engine.rect_outline(px, py, PLAYER_SIZE, PLAYER_SIZE, 200, 255, 220, 255)

		mx, my = engine.mouse_pos()
		engine.line(mx - 8, my, mx + 8, my, 255, 220, 80, 255)
		engine.line(mx, my - 8, mx, my + 8, 255, 220, 80, 255)
		engine.end_frame()
		engine.present()

		frame_count += 1
		total_frames += 1
		if max_frames > 0 and total_frames >= max_frames:
			break
		now = time.perf_counter()
		if now - last_fps_tick >= 1.0:
			fps = frame_count / (now - last_fps_tick)
			cap_label = f"cap={FPS_CAP:.0f}" if fps_limited else "cap=off"
			print(f"fps={fps:.1f} {cap_label} total_frames={total_frames}")
			frame_count = 0
			last_fps_tick = now

		if fps_limited:
			_wait_until(frame_start + target_dt)

	print(f"frames={total_frames}")


if __name__ == "__main__":
	main()
