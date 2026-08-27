# Hyperlite 3D plan

NULLLIGHT-style wireframe FPS needs project + clip + depth-tested lines first — not a scene graph.
This document locks the four-layer roadmap. **Layers 0–2 are shipped.**

## Layer 0 — Depth-tested wireframe (shipped)

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

## Layer 1 — Screen-space triangles (shipped)

- Immediate / bulk filled triangles (`tris_3d` / `tris_screen` / `tick_tris_3d`); no retained mesh yet
- Half-space / barycentric coverage with **top-left** fill rule (shared edges: no double-write, no holes)
- Bin into **64×64** tiles (same as framebuffer dirty tiles); OpenMP over tiles when available (each tile owns pixels → no depth races)
- Flat color per triangle; opaque (`a=255`) is the fast path (depth test + write)
- Translucent: src-over blend, **depth test only — no depth write**
- World path: Sutherland–Hodgman clip in homogeneous clip space (±w, near mandatory); clipped ngon fans back to tris
- `set_cull_backfaces(True)` default on for world path (keeps OpenGL-front after viewport Y flip); screen path culls off
- Perspective-correct depth: interpolate `z/w` (and `1/w`) in screen space
- Portable CPU (scalar; optional AVX2 only via existing blend helpers when the arch enables it)

## Layer 2 — Retained meshes (shipped here)

- `load_mesh` / `draw_mesh` / `tick_mesh` — CPU-resident MeshStore (AtlasStore pattern)
- Vertex layout v1: **6 float32/vert** — `x, y, z, u, v, _pad`; indices `uint32` (optional triangle list)
- Draw: `MVP = view_proj * model`, then existing Layer 1 clip + tiled half-space raster (honors depth + `set_cull_backfaces`)
- UVs stored for a future textured path; **flat-color only in this PR** (`draw_mesh_textured` deferred to Layer 2.1)
- Immediate `tris_3d` unchanged for dynamic geometry
- See [3d-meshes.md](3d-meshes.md) and [3d-mesh-bench.md](3d-mesh-bench.md)

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
engine.set_cull_backfaces(True)  # world tris; default on

# Fused world-space filled triangles (9 floats/tri)
engine.tick_tris_3d(world_tris, cr, cg, cb, ca, r, g, b, a=255)

# Screen-space escape hatch (pixel xy + NDC z)
engine.tris_screen(screen_tris, r, g, b, a=255)

# Retained mesh (Layer 2): load once, draw with model matrix
mesh = engine.load_mesh(verts_xyz_uv_pad, indices_uint32)
engine.draw_mesh(mesh, model16, r, g, b, a=255)
engine.tick_mesh(mesh, model16, cr, cg, cb, ca, r, g, b, a=255)

# Fused world-space wireframe (Layer 0)
engine.tick_lines_3d(world_segs, cr, cg, cb, ca, r, g, b, a=255, width=1)

# Mixed frame
engine.begin_frame()
engine.clear(...)
engine.tris_3d(world_tris, r, g, b, a)
engine.draw_mesh(mesh, model16, r, g, b, a)
engine.rect_fill(...)  # HUD, no depth
engine.tick()
```

See [guide.md](guide.md) for call details, [3d-meshes.md](3d-meshes.md) for retained meshes, [3d-wireframe-bench.md](3d-wireframe-bench.md) for line throughput, [3d-tri-bench.md](3d-tri-bench.md) for immediate triangles, and [3d-mesh-bench.md](3d-mesh-bench.md) for retained mesh throughput.
