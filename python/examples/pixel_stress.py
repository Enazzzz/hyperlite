"""Extreme draw-call stress test for Hyperlite.

Pushes the command buffer and raster backends with configurable floods of
put_pixel, line, and rect_fill work. Defaults target near-worst-case per-frame
load at 1280x720.

Modes:
  pixels  — random put_pixel storm (default count = full framebuffer)
  lines   — dense grid of long lines
  rects   — tiled rect_fill covering the screen
  kitchen — all of the above (maximum engine punishment)

Esc to quit. Tab toggles a ramp that increases pixel count by 10%% each second.
"""

import argparse
import random
import time

import hyperlite

WIDTH = 1280
HEIGHT = 720
# 0 = use full width * height in pixels mode.
PIXELS_PER_FRAME = 0
LINE_STEP = 6
RECT_STEP = 24


def parse_args() -> argparse.Namespace:
	"""Parse stress-test options."""
	parser = argparse.ArgumentParser(description="Hyperlite extreme draw stress test")
	parser.add_argument("--width", type=int, default=WIDTH, help="Framebuffer width")
	parser.add_argument("--height", type=int, default=HEIGHT, help="Framebuffer height")
	parser.add_argument(
		"--mode",
		type=str,
		default="kitchen",
		choices=["pixels", "lines", "rects", "kitchen"],
		help="Workload shape (kitchen = everything)",
	)
	parser.add_argument(
		"--pixels",
		type=int,
		default=PIXELS_PER_FRAME,
		help="put_pixel count per frame (0 = entire framebuffer)",
	)
	parser.add_argument("--line-step", type=int, default=LINE_STEP, help="Pixel spacing for line grid")
	parser.add_argument("--rect-step", type=int, default=RECT_STEP, help="Tile size for rect grid")
	parser.add_argument("--backend", type=str, default="cpu", choices=["cpu", "gpu"], help="Rendering backend")
	parser.add_argument("--path", type=str, default="command", choices=["command", "upload"], help="Render path: command queue or upload_frame_rgba")
	parser.add_argument("--seed", type=int, default=1, help="RNG seed for pixel coordinates")
	parser.add_argument("--ramp", action="store_true", help="Increase pixel load by 10%% every second (Tab toggles live)")
	parser.add_argument("--seconds", type=float, default=0.0, help="Auto-exit after N seconds (0 = infinite)")
	return parser.parse_args()


def resolve_pixel_count(requested: int, width: int, height: int) -> int:
	"""Map 0 to a full-frame pixel flood."""
	fb_pixels = width * height
	if requested <= 0:
		return fb_pixels
	return min(requested, fb_pixels)


def build_pixel_batch(count: int, width: int, height: int, seed: int) -> list[tuple[int, int]]:
	"""Pre-generate shuffled coordinates — one put_pixel command each."""
	rng = random.Random(seed)
	if count >= width * height:
		coords = [(x, y) for y in range(height) for x in range(width)]
		rng.shuffle(coords)
		return coords
	return [(rng.randint(0, width - 1), rng.randint(0, height - 1)) for _ in range(count)]


def build_line_batch(width: int, height: int, step: int) -> list[tuple[int, int, int, int]]:
	"""Horizontal + vertical grid of lines across the framebuffer."""
	lines: list[tuple[int, int, int, int]] = []
	for y in range(0, height, step):
		lines.append((0, y, width - 1, y))
	for x in range(0, width, step):
		lines.append((x, 0, x, height - 1))
	# Diagonal stressors.
	lines.append((0, 0, width - 1, height - 1))
	lines.append((width - 1, 0, 0, height - 1))
	return lines


def build_rect_batch(width: int, height: int, step: int) -> list[tuple[int, int, int, int]]:
	"""Tiled rect_fill commands covering the screen."""
	rects: list[tuple[int, int, int, int]] = []
	for y in range(0, height, step):
		for x in range(0, width, step):
			w = min(step, width - x)
			h = min(step, height - y)
			rects.append((x, y, w, h))
	return rects


def count_draws(mode: str, pixel_count: int, line_count: int, rect_count: int) -> int:
	"""Return total queued draw commands for one frame (excludes clear)."""
	total = 0
	if mode in ("pixels", "kitchen"):
		total += pixel_count
	if mode in ("lines", "kitchen"):
		total += line_count
	if mode in ("rects", "kitchen"):
		total += rect_count
	return total


def queue_frame(
	engine: hyperlite.Engine,
	mode: str,
	path: str,
	width: int,
	height: int,
	pixel_xs: list[int],
	pixel_ys: list[int],
	line_batch: list[tuple[int, int, int, int]],
	rect_batch: list[tuple[int, int, int, int]],
	frame_index: int,
	frame_u32: memoryview | None = None,
	frame_rgba: bytearray | None = None,
) -> None:
	"""Record one frame of stress commands."""
	engine.begin_frame()
	# Slight per-frame color shift so the GPU/CPU cannot trivially skip identical work.
	t = frame_index & 255
	clear_r = 8 + t // 8
	clear_g = 10
	clear_b = 18 + t // 4
	clear_packed = clear_r | (clear_g << 8) | (clear_b << 16) | (255 << 24)
	if path == "upload":
		if frame_u32 is None or frame_rgba is None:
			raise RuntimeError("upload path requires frame staging buffers")
		for i in range(width * height):
			frame_u32[i] = clear_packed
	else:
		engine.clear(clear_r, clear_g, clear_b, 255)

	if mode in ("pixels", "kitchen"):
		packed = 255 | ((128 + (t % 64)) << 8) | (20 << 16) | (255 << 24)
		if path == "upload":
			for x, y in zip(pixel_xs, pixel_ys):
				frame_u32[y * width + x] = packed
		else:
			engine.put_pixels(pixel_xs, pixel_ys, 255, 128 + (t % 64), 20, 255)

	if path == "upload":
		engine.upload_frame_rgba(frame_rgba)

	if mode in ("lines", "kitchen"):
		for x0, y0, x1, y1 in line_batch:
			engine.line(x0, y0, x1, y1, 64, 200 + (t % 55), 64, 255)

	if mode in ("rects", "kitchen"):
		for x, y, w, h in rect_batch:
			engine.rect_fill(x, y, w, h, 40 + (t % 40), 60, 120 + (t % 80), 255)

	engine.end_frame()
	engine.present()


def main() -> None:
	"""Run the stress loop and stream FPS / draw stats to the terminal."""
	args = parse_args()
	needs_pixels = args.mode in ("pixels", "kitchen")
	needs_lines = args.mode in ("lines", "kitchen")
	needs_rects = args.mode in ("rects", "kitchen")

	pixel_count = resolve_pixel_count(args.pixels, args.width, args.height) if needs_pixels else 0
	line_batch = build_line_batch(args.width, args.height, max(2, args.line_step)) if needs_lines else []
	rect_batch = build_rect_batch(args.width, args.height, max(4, args.rect_step)) if needs_rects else []
	pixel_batch = build_pixel_batch(pixel_count, args.width, args.height, args.seed) if needs_pixels else []
	pixel_xs = [p[0] for p in pixel_batch]
	pixel_ys = [p[1] for p in pixel_batch]

	draws_per_frame = count_draws(args.mode, len(pixel_batch), len(line_batch), len(rect_batch))
	engine = hyperlite.Engine(args.width, args.height, args.backend, "Hyperlite Pixel Stress")
	frame_rgba = bytearray(args.width * args.height * 4) if args.path == "upload" else None
	frame_u32 = memoryview(frame_rgba).cast("I") if frame_rgba is not None else None

	frame_count = 0
	total_frames = 0
	accum_draws = 0
	total_draws = 0
	last_report = time.perf_counter()
	run_start = last_report
	frame_index = 0

	ramp_enabled = args.ramp
	tab_was_down = False
	ramp_factor = 1.0

	print(
		f"pixel_stress mode={args.mode} backend={engine.backend_name()} "
		f"path={args.path} "
		f"size={args.width}x{args.height} draws/frame={draws_per_frame:,} "
		f"(pixels={len(pixel_batch):,} lines={len(line_batch):,} rects={len(rect_batch):,})"
	)
	print("Esc=quit  Tab=toggle ramp (+10%/s pixel load)")

	while engine.is_running():
		engine.poll_events()
		if engine.key_down(hyperlite.Keys.Escape):
			break

		tab_down = engine.key_down(hyperlite.Keys.Tab)
		if tab_down and not tab_was_down:
			ramp_enabled = not ramp_enabled
			print(f"ramp={'ON' if ramp_enabled else 'OFF'}")
		tab_was_down = tab_down

		# Apply ramp by truncating the pre-generated pixel batch (lines/rects stay max).
		active_count = len(pixel_batch)
		if ramp_enabled and ramp_factor > 1.0:
			active_count = min(len(pixel_batch), int(len(pixel_batch) * min(ramp_factor, 3.0)))
		active_xs = pixel_xs[:active_count]
		active_ys = pixel_ys[:active_count]

		active_draws = count_draws(args.mode, active_count, len(line_batch), len(rect_batch))

		queue_frame(
			engine,
			args.mode,
			args.path,
			args.width,
			args.height,
			active_xs,
			active_ys,
			line_batch,
			rect_batch,
			frame_index,
			frame_u32,
			frame_rgba,
		)
		frame_index += 1

		frame_count += 1
		total_frames += 1
		accum_draws += active_draws
		total_draws += active_draws

		now = time.perf_counter()
		elapsed = now - last_report
		if elapsed >= 1.0:
			fps = frame_count / elapsed
			frame_ms = (elapsed / frame_count) * 1000.0 if frame_count else 0.0
			draws_per_sec = accum_draws / elapsed
			record_ms, upload_ms, kernel_ms, readback_ms, present_ms = engine.gpu_timings()
			ramp_label = f" ramp={ramp_factor:.2f}x" if ramp_enabled else ""
			print(
				f"fps={fps:.1f} frame_ms={frame_ms:.2f} "
				f"draws/s={draws_per_sec:,.0f} draws/frame={active_draws:,} "
				f"total_draws={total_draws:,} frames={total_frames:,}{ramp_label} "
				f"backend={engine.backend_name()} "
				f"record={record_ms:.2f}ms upload={upload_ms:.2f}ms "
				f"kernel={kernel_ms:.2f}ms readback={readback_ms:.2f}ms "
				f"present={present_ms:.2f}ms dt={engine.delta_time() * 1000.0:.2f}ms"
			)
			if ramp_enabled:
				ramp_factor *= 1.10
			frame_count = 0
			accum_draws = 0
			last_report = now

		if args.seconds > 0.0 and (now - run_start) >= args.seconds:
			print(
				f"summary duration={args.seconds:.1f}s total_draws={total_draws:,} "
				f"frames={total_frames:,} backend={engine.backend_name()}"
			)
			break


if __name__ == "__main__":
	main()
