"""Stress test for explicit pixel rendering throughput."""

import random
import time

import hyperlite


def main() -> None:
	"""Run a random-pixel workload against the native renderer."""
	engine = hyperlite.Engine(800, 600, "cpu", "Hyperlite Pixel Stress")
	last_tick = time.perf_counter()
	frame_count = 0

	while engine.is_running():
		engine.poll_events()
		engine.begin_frame()
		engine.clear(12, 12, 18, 255)

		for _ in range(25_000):
			x = random.randint(0, 799)
			y = random.randint(0, 599)
			engine.put_pixel(x, y, 255, 128, 20, 255)

		engine.rect_outline(100, 100, 300, 180, 64, 255, 64, 255)
		engine.line(0, 0, 799, 599, 255, 64, 64, 255)

		engine.end_frame()
		engine.present()

		frame_count += 1
		now = time.perf_counter()
		if now - last_tick >= 1.0:
			print(f"fps={frame_count}")
			frame_count = 0
			last_tick = now


if __name__ == "__main__":
	main()
