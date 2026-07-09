"""Headless-ish pygame benchmarks mirroring Hyperlite demos (timed window, auto-exit)."""

import argparse
import sys
import time

import numpy as np
import pygame


def parse_args() -> argparse.Namespace:
	"""Parse benchmark mode and duration."""
	parser = argparse.ArgumentParser(description="Pygame comparison benchmarks")
	parser.add_argument(
		"--mode",
		type=str,
		required=True,
		choices=["software_raster", "vector", "sprites"],
		help="Workload to run",
	)
	parser.add_argument("--width", type=int, default=640, help="Window width")
	parser.add_argument("--height", type=int, default=360, help="Window height")
	parser.add_argument("--seconds", type=float, default=6.0, help="Run duration")
	parser.add_argument("--sprite-count", type=int, default=200, help="Sprites mode: blits per frame")
	return parser.parse_args()


def bench_software_raster(width: int, height: int, seconds: float) -> None:
	"""NumPy fill + frombuffer blit + two lines — mirrors software_raster_demo numpy path."""
	pygame.init()
	screen = pygame.display.set_mode((width, height))
	pygame.display.set_caption("pygame software_raster bench")

	xs = np.arange(width, dtype=np.uint32)
	ys = np.arange(height, dtype=np.uint32)
	y_grid, x_grid = np.meshgrid(ys, xs, indexing="ij")
	frame_u32 = np.empty(width * height, dtype=np.uint32)

	frames = 0
	t = 0
	last_report = time.perf_counter()
	run_start = last_report
	fill_ms = 0.0
	blit_ms = 0.0
	draw_ms = 0.0
	flip_ms = 0.0

	while True:
		for event in pygame.event.get():
			if event.type == pygame.QUIT:
				pygame.quit()
				return

		t0 = time.perf_counter()
		t32 = np.uint32(t)
		mask = np.uint32(255)
		alpha = np.uint32(0xFF000000)
		r = (x_grid + t32) & mask
		g = (y_grid + t32) & mask
		b = (x_grid ^ y_grid ^ t32) & mask
		np.copyto(frame_u32, (r | (g << np.uint32(8)) | (b << np.uint32(16)) | alpha).reshape(-1))
		t1 = time.perf_counter()

		rgba = frame_u32.view(np.uint8).reshape(height, width, 4)
		surf = pygame.image.frombuffer(rgba.tobytes(), (width, height), "RGBA")
		t2 = time.perf_counter()

		screen.blit(surf, (0, 0))
		mx = (t * 3) % width
		pygame.draw.line(screen, (255, 255, 255), (mx, 0), (mx, height - 1))
		pygame.draw.line(screen, (255, 255, 255), (0, height // 2), (width - 1, height // 2))
		t3 = time.perf_counter()

		pygame.display.flip()
		t4 = time.perf_counter()

		fill_ms += (t1 - t0) * 1000.0
		blit_ms += (t2 - t1) * 1000.0
		draw_ms += (t3 - t2) * 1000.0
		flip_ms += (t4 - t3) * 1000.0

		frames += 1
		t = (t + 1) & 255

		now = time.perf_counter()
		if now - last_report >= 1.0:
			n = max(frames, 1)
			print(
				f"fps={frames / (now - last_report):.1f} fill={fill_ms / n:.2f}ms "
				f"frombuffer={blit_ms / n:.2f}ms draw={draw_ms / n:.2f}ms flip={flip_ms / n:.2f}ms"
			)
			frames = 0
			fill_ms = blit_ms = draw_ms = flip_ms = 0.0
			last_report = now

		if now - run_start >= seconds:
			break

	pygame.quit()


def bench_vector(width: int, height: int, seconds: float) -> None:
	"""Grid + rect + crosshair — mirrors minimal_game draw load."""
	pygame.init()
	screen = pygame.display.set_mode((width, height))
	pygame.display.set_caption("pygame vector bench")

	player_size = 32
	px = (width - player_size) // 2
	py = (height - player_size) // 2

	frames = 0
	last_report = time.perf_counter()
	run_start = last_report

	while True:
		for event in pygame.event.get():
			if event.type == pygame.QUIT:
				pygame.quit()
				return

		screen.fill((18, 20, 28))
		for gx in range(0, width, 64):
			pygame.draw.line(screen, (28, 32, 44), (gx, 0), (gx, height - 1))
		for gy in range(0, height, 64):
			pygame.draw.line(screen, (28, 32, 44), (0, gy), (width - 1, gy))

		pygame.draw.rect(screen, (80, 200, 120), (px, py, player_size, player_size))
		pygame.draw.rect(screen, (200, 255, 220), (px, py, player_size, player_size), 1)

		mx, my = pygame.mouse.get_pos()
		pygame.draw.line(screen, (255, 220, 80), (mx - 8, my), (mx + 8, my))
		pygame.draw.line(screen, (255, 220, 80), (mx, my - 8), (mx, my + 8))

		pygame.display.flip()
		frames += 1

		now = time.perf_counter()
		if now - last_report >= 1.0:
			print(f"fps={frames / (now - last_report):.1f}")
			frames = 0
			last_report = now

		if now - run_start >= seconds:
			break

	pygame.quit()


def bench_sprites(width: int, height: int, seconds: float, sprite_count: int) -> None:
	"""Many small sprite blits per frame."""
	pygame.init()
	screen = pygame.display.set_mode((width, height))
	pygame.display.set_caption("pygame sprites bench")

	rng = np.random.default_rng(1)
	sprite = pygame.Surface((32, 32), pygame.SRCALPHA)
	sprite.fill((80, 200, 120, 255))
	positions = [(int(rng.integers(0, width - 32)), int(rng.integers(0, height - 32))) for _ in range(sprite_count)]

	frames = 0
	last_report = time.perf_counter()
	run_start = last_report

	while True:
		for event in pygame.event.get():
			if event.type == pygame.QUIT:
				pygame.quit()
				return

		screen.fill((18, 20, 28))
		for x, y in positions:
			screen.blit(sprite, (x, y))
		pygame.display.flip()
		frames += 1

		now = time.perf_counter()
		if now - last_report >= 1.0:
			print(f"fps={frames / (now - last_report):.1f} blits/frame={sprite_count}")
			frames = 0
			last_report = now

		if now - run_start >= seconds:
			break

	pygame.quit()


def main() -> None:
	"""Dispatch selected benchmark."""
	args = parse_args()
	if args.mode == "software_raster":
		bench_software_raster(args.width, args.height, args.seconds)
	elif args.mode == "vector":
		bench_vector(args.width, args.height, args.seconds)
	elif args.mode == "sprites":
		bench_sprites(args.width, args.height, args.seconds, args.sprite_count)
	else:
		print(f"unknown mode: {args.mode}", file=sys.stderr)
		sys.exit(1)


if __name__ == "__main__":
	main()
