"""Native Game.run() starter — WASD square, Escape to quit.

Python configures. C++ owns poll / clear / present / frame pacing.
The optional on_frame callback is only the mover + HUD; it is not required
for Run() itself. Headless: HYPERLITE_HEADLESS=1 python native_game.py
"""

import os

import hyperlite

WIDTH = 960
HEIGHT = 540
PLAYER_SIZE = 32
PLAYER_SPEED = 240.0  # pixels per second


def main() -> None:
	"""Configure a small game and start the native loop."""
	present = "headless" if os.environ.get("HYPERLITE_HEADLESS") else "auto"
	game = hyperlite.Game(WIDTH, HEIGHT, "cpu", "Hyperlite Native Game", present=present)
	game.set_target_fps(60.0)
	game.set_clear_color(18, 20, 28, 255)
	game.map_action("quit", hyperlite.Keys.Escape)
	game.map_action("up", hyperlite.Keys.W)
	game.map_action("down", hyperlite.Keys.S)
	game.map_action("left", hyperlite.Keys.A)
	game.map_action("right", hyperlite.Keys.D)

	if present == "headless":
		game.set_max_frames(8)

	engine = game.engine()
	# Player top-left in pixels; mutated only from on_frame.
	px = float((WIDTH - PLAYER_SIZE) // 2)
	py = float((HEIGHT - PLAYER_SIZE) // 2)

	def on_frame() -> None:
		"""Optional Python hook — skip this entirely for a C++-only loop."""
		if game.action_down("quit") or game.key_pressed(hyperlite.Keys.Escape):
			game.request_quit()
			return
		dt = max(game.delta_time(), 0.0)
		step = PLAYER_SPEED * dt
		nonlocal px, py
		if game.action_down("up"):
			py -= step
		if game.action_down("down"):
			py += step
		if game.action_down("left"):
			px -= step
		if game.action_down("right"):
			px += step
		px = max(0.0, min(float(WIDTH - PLAYER_SIZE), px))
		py = max(0.0, min(float(HEIGHT - PLAYER_SIZE), py))

		x = int(px)
		y = int(py)
		engine.rect_fill(x, y, PLAYER_SIZE, PLAYER_SIZE, 80, 200, 120, 255)
		engine.rect_outline(x, y, PLAYER_SIZE, PLAYER_SIZE, 200, 255, 220, 255)
		mx, my = engine.mouse_pos()
		engine.line(mx - 8, my, mx + 8, my, 255, 220, 80, 255)
		engine.line(mx, my - 8, mx, my + 8, 255, 220, 80, 255)

	game.on_frame(on_frame)
	game.run()
	print(f"frames={game.frame_index()} last_dt={game.delta_time():.4f}")


if __name__ == "__main__":
	main()
