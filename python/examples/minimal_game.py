"""Minimal game loop — move a square with WASD, quit with Escape.

Shows the core Hyperlite pattern: poll input, queue draw commands each frame,
present. No scene graph, no sprites — you draw everything explicitly every frame.
"""

import hyperlite

# Windows virtual-key codes (what key_down expects).
VK_ESCAPE = 0x1B
VK_W = 0x57
VK_A = 0x41
VK_S = 0x53
VK_D = 0x44

WIDTH = 960
HEIGHT = 540
PLAYER_SIZE = 32
PLAYER_SPEED = 4.0


def main() -> None:
	"""Run a simple top-down mover demo."""
	engine = hyperlite.Engine(WIDTH, HEIGHT, "gpu", "Hyperlite Minimal Game")

	# Center the player.
	px = (WIDTH - PLAYER_SIZE) // 2
	py = (HEIGHT - PLAYER_SIZE) // 2

	while engine.is_running():
		engine.poll_events()

		if engine.key_down(VK_ESCAPE):
			break

		# Movement (fixed step; no delta-time for simplicity).
		if engine.key_down(VK_W):
			py -= int(PLAYER_SPEED)
		if engine.key_down(VK_S):
			py += int(PLAYER_SPEED)
		if engine.key_down(VK_A):
			px -= int(PLAYER_SPEED)
		if engine.key_down(VK_D):
			px += int(PLAYER_SPEED)

		# Keep on screen.
		px = max(0, min(WIDTH - PLAYER_SIZE, px))
		py = max(0, min(HEIGHT - PLAYER_SIZE, py))

		engine.begin_frame()
		engine.clear(18, 20, 28, 255)

		# Grid hint (cheap debug background).
		for gx in range(0, WIDTH, 64):
			engine.line(gx, 0, gx, HEIGHT - 1, 28, 32, 44, 255)
		for gy in range(0, HEIGHT, 64):
			engine.line(0, gy, WIDTH - 1, gy, 28, 32, 44, 255)

		# Player.
		engine.rect_fill(px, py, PLAYER_SIZE, PLAYER_SIZE, 80, 200, 120, 255)
		engine.rect_outline(px, py, PLAYER_SIZE, PLAYER_SIZE, 200, 255, 220, 255)

		# Crosshair at mouse.
		mx, my = engine.mouse_pos()
		engine.line(mx - 8, my, mx + 8, my, 255, 220, 80, 255)
		engine.line(mx, my - 8, mx, my + 8, 255, 220, 80, 255)

		engine.end_frame()
		engine.present()


if __name__ == "__main__":
	main()
