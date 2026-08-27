# Hyperlite 3D plan

NULLLIGHT-style wireframe FPS needs project + clip + depth-tested lines first — not a scene graph.
This document locks the four-layer roadmap. **Layers 0–2.1 are shipped.**

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
- Portable CPU (scalar fallback; AVX2/SSE4.2 pixel blocks when the arch enables them — see [simd-tri-fill.md](simd-tri-fill.md))

## Layer 2 — Retained meshes (shipped)

- `load_mesh` / `draw_mesh` / `tick_mesh` — CPU-resident MeshStore (AtlasStore pattern)
- Vertex layout v1: **6 float32/vert** — `x, y, z, u, v, _pad`; indices `uint32` (optional triangle list)
- Draw: `MVP = view_proj * model`, then existing Layer 1 clip + tiled half-space raster (honors depth + `set_cull_backfaces`)
- Immediate `tris_3d` unchanged for dynamic geometry
- See [3d-meshes.md](3d-meshes.md) and [3d-mesh-bench.md](3d-mesh-bench.md)

## Layer 2.1 — Textured retained meshes (shipped)

- `draw_mesh_textured(mesh, model, atlas)` / optional `tick_mesh_textured` — same mesh layout, samples `load_atlas` RGBA
- Perspective-correct UV during the **existing** tiled half-space fill (`u/w`, `v/w`, `1/w`); no second rasterizer
- UV **0..1 over the full atlas** (not a sprite subrect); **clamp**; **nearest** sample (v1 fast path)
- Alpha: `a==255` opaque + depth write; `a<255` src-over + no depth write; `a==0` skip pixel (same Layer 1 rule)
- Flat `draw_mesh` unchanged; invalid mesh/atlas handles are no-ops
- See [3d-meshes.md](3d-meshes.md)

## Further speed (not a second renderer)

Hyperlite’s renderer **owns all pixels**. Present is a **copy** of our RGBA8 (+ depth) into the window via the existing blit path (DXGI / X11 / GDI) — not a second rasterizer.

Further speedups stay inside that ownership model:

- **(a)** More CPU SIMD / tiling in *our* raster
- **(b)** Optional CUDA (or similar **compute**) kernels that write the **same** RGBA8 + depth framebuffer Hyperlite already owns, then present those bytes through the existing window blit

**Out of scope forever as a graphics backend:** Vulkan, OpenGL, D3D, Metal, or any API that takes over rasterization / owns the swapchain as the renderer. Compute that fills Hyperlite’s buffers is fine; a second GPU graphics pipeline is not.

## Non-goals (v1)

- Scene graph
- glTF loader
- PBR / material system
- Custom shader language
- Skeletal animation
- Vulkan / OpenGL / D3D / Metal (or any graphics API that replaces our raster)

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

# Textured retained mesh (Layer 2.1)
atlas = engine.load_atlas(rgba, w, h)
engine.draw_mesh_textured(mesh, model16, atlas)
engine.tick_mesh_textured(mesh, model16, atlas, cr, cg, cb, ca)

# Fused world-space wireframe (Layer 0)
engine.tick_lines_3d(world_segs, cr, cg, cb, ca, r, g, b, a=255, width=1)

# Mixed frame
engine.begin_frame()
engine.clear(...)
engine.tris_3d(world_tris, r, g, b, a)
engine.draw_mesh(mesh, model16, r, g, b, a)
engine.draw_mesh_textured(mesh, model16, atlas)
engine.rect_fill(...)  # HUD, no depth
engine.tick()
```

See [guide.md](guide.md) for call details, [3d-meshes.md](3d-meshes.md) for retained meshes, [3d-wireframe-bench.md](3d-wireframe-bench.md) for line throughput, [3d-tri-bench.md](3d-tri-bench.md) for immediate triangles, [3d-mesh-bench.md](3d-mesh-bench.md) for retained mesh throughput, and [proc-world.md](proc-world.md) for the integrated procedural stress game.
