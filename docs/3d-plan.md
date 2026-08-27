# Hyperlite 3D plan

NULLLIGHT-style wireframe FPS needs project + clip + depth-tested lines first — not a scene graph.
This document locks the four-layer roadmap. **Layer 0 is this PR.**

## Layer 0 — Depth-tested wireframe (shipped here)

- Separate float32 world-space segment buffers (`x0,y0,z0,x1,y1,z1`), not Z on 2D `DrawCommand`
- Column-major `set_view_proj` (world → clip); identity = world is clip
- Homogeneous Cohen-Sutherland clip **before** perspective divide (±w planes, near mandatory)
- Float32 depth plane (clear = 1.0, GL-style less-equal); allocate only via `enable_depth(True)`
- Perspective-correct depth along lines: interpolate `z/w` and `1/w` in screen space
- Fused `tick_lines_3d` (clear color + depth + raster + present) and queued `lines_3d`
- Escape hatch: `lines_3d_screen` — pixel `xy` + NDC `z` in `[-1,1]` (skips view-proj / frustum)
- 2D draws ignore depth; `enable_depth(False)` restores exact prior 2D behavior
- CPU path is default and works headless; no OpenMP over depth writes (no races)
- CUDA 3D deferred

## Layer 1 — Screen-space triangles

- `tris_bulk` tiled raster (reuse ~64px dirty tiles)
- Still immediate / bulk buffers; no retained mesh API yet

## Layer 2 — Retained meshes

- `load_mesh` / `draw_mesh`
- Atlas UVs for textured wireframe-adjacent fills later

## Layer 3 — Portable GPU

- CPU remains the default and correctness path
- CUDA fast path where available
- Vulkan (or similar) for cross-vendor GPU later

## Non-goals (v1)

- Scene graph
- glTF loader
- PBR / material system
- Custom shader language
- Skeletal animation

## API sketch (Python)

```python
engine = hyperlite.Engine(1280, 720, "cpu", present="headless")
engine.enable_depth(True)
engine.set_view_proj(matrix16)  # float32[16], column-major

# Fused world-space path
engine.tick_lines_3d(world_segs, cr, cg, cb, ca, r, g, b, a=255, width=1)

# Mixed frame
engine.begin_frame()
engine.clear(...)
engine.lines_3d(world_segs, r, g, b, a, width=1)
engine.rect_fill(...)  # HUD, no depth
engine.tick()
```

See [guide.md](guide.md) for call details and [3d-wireframe-bench.md](3d-wireframe-bench.md) for measured throughput.
