"""Wireframe demo — animated line grid via tick_lines and a preallocated NumPy buffer."""

import math
import sys
import time

import hyperlite

try:
	import numpy as np
except ImportError:
	np = None

WIDTH = 1280
HEIGHT = 720
GRID = 48
LINE_COUNT = GRID * (GRID + 1) * 2


def make_segment_buffer() -> "np.ndarray":
	"""Preallocate one int32 segment buffer reused every frame."""
	if np is None:
		raise RuntimeError("wireframe_demo requires numpy (pip install numpy)")
	return np.empty((LINE_COUNT, 4), dtype=np.int32)


def fill_grid_segments(segments: "np.ndarray", phase: float) -> None:
	"""Project a scrolling wireframe grid into x0,y0,x1,y1 segment tuples."""
	wobble = int(math.sin(phase) * 24.0)
	step_x = WIDTH // GRID
	step_y = HEIGHT // GRID
	row = 0
	for gy in range(GRID + 1):
		y = gy * step_y + wobble
		if y < 0:
			y = 0
		if y >= HEIGHT:
			y = HEIGHT - 1
		for gx in range(GRID):
			x0 = gx * step_x
			x1 = min(WIDTH - 1, x0 + step_x)
			segments[row, 0] = x0
			segments[row, 1] = y
			segments[row, 2] = x1
			segments[row, 3] = y
			row += 1
	for gx in range(GRID + 1):
		x = gx * step_x - wobble // 2
		if x < 0:
			x = 0
		if x >= WIDTH:
			x = WIDTH - 1
		for gy in range(GRID):
			y0 = gy * step_y
			y1 = min(HEIGHT - 1, y0 + step_y)
			segments[row, 0] = x
			segments[row, 1] = y0
			segments[row, 2] = x
			segments[row, 3] = y1
			row += 1


def main() -> None:
	"""Run fused tick_lines wireframe demo."""
	if np is None:
		print("wireframe_demo requires numpy: pip install numpy", file=sys.stderr)
		sys.exit(1)

	engine = hyperlite.Engine(WIDTH, HEIGHT, "cpu", "Hyperlite Wireframe Demo")
	engine.set_vsync(True)
	segments = make_segment_buffer()
	start = time.perf_counter()
	frame = 0

	while engine.is_running():
		phase = frame * 0.035
		fill_grid_segments(segments, phase)
		engine.tick_lines(segments, 8, 12, 24, 255, 0, 255, 90, 255, width=1)
		frame += 1
		if frame % 120 == 0:
			elapsed = time.perf_counter() - start
			fps = frame / elapsed if elapsed > 0.0 else 0.0
			print(f"frames={frame} fps={fps:.1f} segments={LINE_COUNT}")


if __name__ == "__main__":
	main()
