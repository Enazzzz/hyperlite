"""Software raster demo using upload_frame_rgba fast path.

Renders a procedural RGB pattern into a CPU buffer, uploads once per frame, then
draws a tiny line HUD overlay through the command queue.

Uses NumPy for the hot fill when available; pass --fill python to compare baseline.
"""

import argparse
import sys
import time

import hyperlite

try:
	import numpy as np
except ImportError:
	np = None

WIDTH = 640
HEIGHT = 360


def parse_args() -> argparse.Namespace:
	"""Parse demo options."""
	parser = argparse.ArgumentParser(description="Software raster upload demo")
	parser.add_argument("--width", type=int, default=WIDTH, help="Framebuffer width")
	parser.add_argument("--height", type=int, default=HEIGHT, help="Framebuffer height")
	parser.add_argument("--backend", type=str, default="gpu", choices=["cpu", "gpu"], help="Rendering backend")
	parser.add_argument(
		"--fill",
		type=str,
		default="numpy" if np is not None else "python",
		choices=["numpy", "python"],
		help="Frame fill implementation (numpy needs the numpy package)",
	)
	parser.add_argument("--seconds", type=float, default=0.0, help="Auto-exit after N seconds (0 = infinite)")
	return parser.parse_args()


def fill_frame_python(frame_u32: memoryview, width: int, height: int, t: int) -> None:
	"""Fill RGBA8 pixels with a pure-Python procedural pattern (slow baseline)."""
	for y in range(height):
		row = y * width
		g = (y + t) & 255
		for x in range(width):
			r = (x + t) & 255
			b = (x ^ y ^ t) & 255
			frame_u32[row + x] = r | (g << 8) | (b << 16) | (255 << 24)


def make_numpy_raster(width: int, height: int) -> tuple["np.ndarray", "np.ndarray", "np.ndarray"]:
	"""Preallocate a uint32 frame and coordinate grids for vectorized fills."""
	xs = np.arange(width, dtype=np.uint32)
	ys = np.arange(height, dtype=np.uint32)
	y_grid, x_grid = np.meshgrid(ys, xs, indexing="ij")
	frame_u32 = np.empty(width * height, dtype=np.uint32)
	return frame_u32, x_grid, y_grid


def fill_frame_numpy(
	frame_u32: "np.ndarray",
	x_grid: "np.ndarray",
	y_grid: "np.ndarray",
	t: int,
) -> None:
	"""Fill the frame buffer with the same pattern as fill_frame_python, vectorized."""
	t32 = np.uint32(t)
	mask = np.uint32(255)
	alpha = np.uint32(0xFF000000)
	r = (x_grid + t32) & mask
	g = (y_grid + t32) & mask
	b = (x_grid ^ y_grid ^ t32) & mask
	np.copyto(frame_u32, (r | (g << np.uint32(8)) | (b << np.uint32(16)) | alpha).reshape(-1))


def main() -> None:
	"""Run software raster upload demo."""
	args = parse_args()
	if args.fill == "numpy" and np is None:
		print("NumPy is not installed; use: pip install numpy  (or pass --fill python)", file=sys.stderr)
		sys.exit(1)

	engine = hyperlite.Engine(args.width, args.height, args.backend, "Hyperlite Software Raster Demo")

	frame_bytes = bytearray(args.width * args.height * 4)
	frame_u32_py = memoryview(frame_bytes).cast("I")
	np_frame: np.ndarray | None = None
	np_x: np.ndarray | None = None
	np_y: np.ndarray | None = None
	if args.fill == "numpy":
		np_frame, np_x, np_y = make_numpy_raster(args.width, args.height)

	last_report = time.perf_counter()
	run_start = last_report
	frames = 0
	t = 0

	print(
		f"software_raster_demo backend={engine.backend_name()} fill={args.fill} "
		f"size={args.width}x{args.height}  Esc=quit"
	)

	while engine.is_running():
		engine.begin_frame()

		if args.fill == "numpy" and np_frame is not None and np_x is not None and np_y is not None:
			fill_frame_numpy(np_frame, np_x, np_y, t)
			engine.upload_frame_rgba(np_frame)
		else:
			fill_frame_python(frame_u32_py, args.width, args.height, t)
			engine.upload_frame_rgba(frame_bytes)

		mx = (t * 3) % args.width
		engine.line(mx, 0, mx, args.height - 1, 255, 255, 255, 255)
		engine.line(0, args.height // 2, args.width - 1, args.height // 2, 255, 255, 255, 255)

		engine.tick()
		if engine.key_down(hyperlite.Keys.Escape):
			break

		frames += 1
		t = (t + 1) & 255

		now = time.perf_counter()
		elapsed = now - last_report
		if elapsed >= 1.0:
			record_ms, upload_ms, kernel_ms, readback_ms, present_ms = engine.gpu_timings()
			fps = frames / elapsed
			print(
				f"fps={fps:.1f} fill={args.fill} record={record_ms:.2f}ms upload={upload_ms:.2f}ms "
				f"kernel={kernel_ms:.2f}ms readback={readback_ms:.2f}ms present={present_ms:.2f}ms"
			)
			frames = 0
			last_report = now

		if args.seconds > 0.0 and (now - run_start) >= args.seconds:
			break


if __name__ == "__main__":
	main()
