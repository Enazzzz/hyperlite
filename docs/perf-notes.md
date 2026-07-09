# Hyperlite Performance Notes

## Design for Throughput

- Explicit command buffer with deferred blits, uploads, and retained layers.
- Contiguous RGBA8 framebuffer with 64px tile dirty tracking and optional partial CPU present.
- Material-sorted blit batches (default threshold 256) for large sprite counts.
- Line batch raster on CPU: same-color runs share one dirty rect; horizontal spans use AVX2.
- Put-pixel batching: consecutive same-color `kPutPixel` commands merge into one dirty rect.
- Full CPU alpha compositing on lines, rects, pixels, and RGBA blits (fast RB/AG lane blend).
- Multi-core opaque line raster (OpenMP) for batches of 384+ segments.
- `tick_lines()` fused path: poll + clear + parallel wireframe + present in one native call.
- `lines_bulk()` queues thousands of segments from one int32 buffer without per-line Python overhead.
- Line color/width sort (default threshold 64) groups wireframe draws before batch execution.
- Default command buffer reserve 65536 (~1.5 MB), grows automatically when exceeded.
- Built-in vsync: `set_vsync(True)` — DXGI sync interval (default) or DwmFlush on GDI fallback.
- **DXGI flip-model present is default** on Windows — CPU host upload via `UpdateSubresource` + swapchain `Present`. GDI BitBlt is fallback only when DXGI init fails.

## Backend Cheat Sheet

| Workload | Backend | API |
|----------|---------|-----|
| Vector / HUD games | `cpu` | `line`, `rect_fill`, `tick()` — DXGI present by default |
| Wireframe / debug draw | `cpu` | `lines_bulk`, `tick_lines` — DXGI present by default |
| Many sprites | `cpu` or `gpu` + atlas | `load_atlas`, `draw_sprite`, `tick_blits` |
| Static tilemaps | either | `commit_retained_layer`, `draw_retained_layer` |
| Full-screen software raster | `gpu` | NumPy → `upload_frame_rgba` |
| GPU without readback tax | `gpu` | `set_direct_present(True)` |

## New APIs (Tier 2/3)

```python
engine.set_blit_sort_threshold(256)   # 0 = disable material sort
engine.set_line_sort_threshold(64)    # 0 = disable line sort
engine.set_command_buffer_reserve(65536)
engine.set_vsync(True)                # default on
engine.set_dirty_present(True)        # CPU tile partial present (64px dirty tiles)
engine.line(x0, y0, x1, y1, r, g, b, a=255, width=2)  # thick / alpha lines
engine.lines_bulk(segments, r, g, b, a=255, width=1)    # int32 Nx4 buffer, one native call
engine.tick_lines(segments, cr, cg, cb, ca, r, g, b, a=255)  # fused poll + wireframe frame
engine.set_direct_present(True)       # GPU: CUDA→DXGI, no D2H readback
engine.set_dxgi_present(True)         # default on; GDI fallback if disabled/failed

layer = engine.commit_retained_layer()  # capture static commands once
engine.draw_retained_layer(layer)       # replay without Python re-record

# Fused sprite loop (7 x int32 per sprite: atlas, sx, sy, w, h, dx, dy)
engine.tick_blits(sprite_array, r, g, b, a=255)
```

## Benchmarks

```powershell
.\build\Release\reference_render_tests.exe
.\build\Release\cpu_line_bench.exe
.\build\Release\gpu_blit_bench.exe
python python\examples\wireframe_demo.py
python python\examples\retained_layer_demo.py
python python\examples\software_raster_demo.py --fill numpy
```

## Next Steps (when plateau)

- Retained layer GPU replay without command re-merge.
- DXGI flip-discard + vsync tuning.
- Material sort keyed by atlas + clip rect.
