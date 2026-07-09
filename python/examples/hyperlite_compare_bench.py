"""Quick Hyperlite vector + sprite benches matching pygame_compare_bench.py."""

import argparse
import time

import hyperlite


def bench_vector(width: int, height: int, backend: str, seconds: float) -> None:
	"""Grid + rect + crosshair like minimal_game."""
	engine = hyperlite.Engine(width, height, backend, "Hyperlite vector bench")
	player_size = 32
	px = (width - player_size) // 2
	py = (height - player_size) // 2
	frames = 0
	last_report = time.perf_counter()
	run_start = last_report

	while engine.is_running():
		engine.poll_events()
		engine.begin_frame()
		engine.clear(18, 20, 28, 255)
		for gx in range(0, width, 64):
			engine.line(gx, 0, gx, height - 1, 28, 32, 44, 255)
		for gy in range(0, height, 64):
			engine.line(0, gy, width - 1, gy, 28, 32, 44, 255)
		engine.rect_fill(px, py, player_size, player_size, 80, 200, 120, 255)
		engine.rect_outline(px, py, player_size, player_size, 200, 255, 220, 255)
		mx, my = engine.mouse_pos()
		engine.line(mx - 8, my, mx + 8, my, 255, 220, 80, 255)
		engine.line(mx, my - 8, mx, my + 8, 255, 220, 80, 255)
		engine.tick()
		frames += 1
		now = time.perf_counter()
		if now - last_report >= 1.0:
			print(f"backend={engine.backend_name()} fps={frames / (now - last_report):.1f}")
			frames = 0
			last_report = now
		if now - run_start >= seconds:
			break


def bench_sprites(width: int, height: int, backend: str, seconds: float, sprite_count: int) -> None:
	"""Many native blit_rgba calls per frame."""
	import random

	engine = hyperlite.Engine(width, height, backend, "Hyperlite sprites bench")
	rng = random.Random(1)
	sprite = bytearray(32 * 32 * 4)
	for i in range(0, len(sprite), 4):
		sprite[i] = 80
		sprite[i + 1] = 200
		sprite[i + 2] = 120
		sprite[i + 3] = 255
	positions = [(rng.randint(0, width - 32), rng.randint(0, height - 32)) for _ in range(sprite_count)]
	frames = 0
	last_report = time.perf_counter()
	run_start = last_report

	while engine.is_running():
		engine.poll_events()
		engine.begin_frame()
		engine.clear(18, 20, 28, 255)
		for x, y in positions:
			engine.blit_rgba(sprite, x, y, 32, 32)
		engine.tick()
		frames += 1
		now = time.perf_counter()
		if now - last_report >= 1.0:
			print(f"backend={engine.backend_name()} fps={frames / (now - last_report):.1f} blits/frame={sprite_count}")
			frames = 0
			last_report = now
		if now - run_start >= seconds:
			break


def main() -> None:
	parser = argparse.ArgumentParser()
	parser.add_argument("--mode", choices=["vector", "sprites"], required=True)
	parser.add_argument("--backend", default="cpu", choices=["cpu", "gpu"])
	parser.add_argument("--width", type=int, default=960)
	parser.add_argument("--height", type=int, default=540)
	parser.add_argument("--seconds", type=float, default=6.0)
	parser.add_argument("--sprite-count", type=int, default=200)
	args = parser.parse_args()
	if args.mode == "vector":
		bench_vector(args.width, args.height, args.backend, args.seconds)
	else:
		bench_sprites(args.width, args.height, args.backend, args.seconds, args.sprite_count)


if __name__ == "__main__":
	main()
