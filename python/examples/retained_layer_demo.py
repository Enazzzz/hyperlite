"""Retained layer demo — record a static tile grid once, replay every frame.

Shows CommitRetainedLayer / DrawRetainedLayer plus optional tick_blits fast path.
"""

import array
import time

import hyperlite

WIDTH = 960
HEIGHT = 540
TILE = 32
COLS = WIDTH // TILE
ROWS = HEIGHT // TILE


def build_atlas() -> tuple[bytearray, int]:
	"""Build a tiny checker tile atlas."""
	atlas = bytearray(TILE * TILE * 4)
	for y in range(TILE):
		for x in range(TILE):
			i = (y * TILE + x) * 4
			shade = 180 if ((x // 8) + (y // 8)) % 2 == 0 else 90
			atlas[i + 0] = 40
			atlas[i + 1] = shade
			atlas[i + 2] = 120
			atlas[i + 3] = 255
	return atlas, TILE


def record_tile_layer(engine: hyperlite.Engine, atlas: int) -> int:
	"""Record the static tilemap once into a retained layer."""
	engine.begin_frame()
	engine.clear(10, 12, 24, 255)
	for row in range(ROWS):
		for col in range(COLS):
			engine.draw_sprite(atlas, 0, 0, TILE, TILE, col * TILE, row * TILE)
	return engine.commit_retained_layer()


def pack_sprite_descs(atlas: int, offset_x: int) -> bytes:
	"""Pack moving sprites for tick_blits (7 x int32 per sprite)."""
	descs = array.array("i")
	for i in range(8):
		x = (offset_x + i * 80) % (WIDTH - TILE)
		y = 40 + i * 50
		descs.extend([atlas, 0, 0, TILE, TILE, x, y])
	return descs.tobytes()


def main() -> None:
	"""Run retained-layer replay demo."""
	engine = hyperlite.Engine(WIDTH, HEIGHT, "gpu", "Hyperlite Retained Layer Demo")
	engine.set_blit_sort_threshold(64)
	atlas_bytes, _ = build_atlas()
	atlas = engine.load_atlas(atlas_bytes, TILE, TILE)
	layer = record_tile_layer(engine, atlas)

	last = time.perf_counter()
	frames = 0
	t = 0
	use_tick_blits = True

	print(f"retained_layer_demo layer={layer} tick_blits={use_tick_blits}  Esc=quit")

	while engine.is_running():
		if use_tick_blits:
			sprite_buf = pack_sprite_descs(atlas, t * 4)
			engine.tick_blits(sprite_buf, 20, 24, 40, 255)
		else:
			engine.begin_frame()
			engine.draw_retained_layer(layer)
			for i in range(8):
				x = (t * 4 + i * 80) % (WIDTH - TILE)
				y = 40 + i * 50
				engine.draw_sprite(atlas, 0, 0, TILE, TILE, x, y)
			engine.tick()

		if engine.key_down(hyperlite.Keys.Escape):
			break

		frames += 1
		t += 1
		now = time.perf_counter()
		if now - last >= 1.0:
			record_ms, upload_ms, kernel_ms, readback_ms, present_ms = engine.gpu_timings()
			print(
				f"fps={frames / (now - last):.1f} record={record_ms:.2f}ms "
				f"upload={upload_ms:.2f}ms kernel={kernel_ms:.2f}ms "
				f"readback={readback_ms:.2f}ms present={present_ms:.2f}ms"
			)
			frames = 0
			last = now


if __name__ == "__main__":
	main()
