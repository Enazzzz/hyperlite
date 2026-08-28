"""Native Game.run() demo — no Python while-True loop.

Python configures the runtime; C++ owns polling, timing, and frame pacing.
An optional on_frame callback is registered only for this demo's HUD; it is
not required. Headless usage: HYPERLITE_HEADLESS=1 python native_game.py
"""

import os

import hyperlite


def main() -> None:
	"""Configure and start the native loop."""
	present = "headless" if os.environ.get("HYPERLITE_HEADLESS") else "auto"
	game = hyperlite.Game(640, 360, "cpu", "Hyperlite Native Game", present=present)
	game.set_target_fps(60.0)
	game.set_clear_color(18, 20, 28, 255)
	game.map_action("quit", hyperlite.Keys.Escape)

	# Tests / CI: stop after a handful of native frames.
	if present == "headless":
		game.set_max_frames(8)

	engine = game.engine()

	def hud() -> None:
		"""Optional Python hook — not on the hot path unless registered."""
		if game.action_down("quit"):
			game.request_quit()
		engine.rect_fill(16, 16, 24, 24, 80, 200, 120, 255)

	game.on_frame(hud)
	game.run()
	print(f"frames={game.frame_index()} last_dt={game.delta_time():.4f}")


if __name__ == "__main__":
	main()
