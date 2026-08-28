# Retained meshes (Layer 2 / 2.1)

Load a mesh once, draw it many times with a model matrix — the 3D analogue of `load_atlas` + `draw_sprite`. Uses the Layer 1 tiled triangle raster + depth + view-proj.

**Immediate `tris_3d` still exists** for dynamic geometry. Prefer meshes when the same topology is redrawn every frame.

## API

```python
import numpy as np
import hyperlite

engine = hyperlite.Engine(1280, 720, "cpu", present="headless")
engine.enable_depth(True)
engine.set_view_proj(view_proj16)  # column-major float32[16]
engine.set_cull_backfaces(True)    # honored by draw_mesh (same as Layer 1 world path)

# verts: float32, 6 floats/vert = x, y, z, u, v, _pad
# indices: uint32 triangles (optional; None/empty = triangle list)
mesh = engine.load_mesh(verts, indices)

engine.begin_frame()
engine.clear(8, 12, 20, 255)
engine.draw_mesh(mesh, model16, 0, 200, 255, 255)  # model: column-major 4x4
engine.tick()

# Fused helper (optional)
engine.tick_mesh(mesh, model16, 8, 12, 20, 255, 0, 200, 255, 255)

# Layer 2.1 textured draw (UV 0..1 = full atlas)
atlas = engine.load_atlas(rgba8, width, height)
engine.draw_mesh_textured(mesh, model16, atlas)
engine.tick_mesh_textured(mesh, model16, atlas, 8, 12, 20, 255)
```

Invalid mesh / atlas handles are a **no-op** (no crash). Bad uploads raise / return failure from `load_mesh` / `load_atlas`.

## Texturing (Layer 2.1)

| Rule | Behavior |
|------|----------|
| UV space | `u,v` in **0..1 over the full atlas** (not a sprite subrect) |
| Filter | **Nearest** neighbor (pixel-art / immediate fast path) |
| Wrap | **Clamp** to [0,1] |
| Perspective | Interpolate `u/w`, `v/w`, `1/w` in screen space, then divide |
| Alpha | `a==255` opaque + depth write; `a<255` src-over + no depth write; `a==0` skip |

Flat-color `draw_mesh` ignores UVs and remains unchanged.

## What ships

| Feature | Status |
|---------|--------|
| `load_mesh` / `draw_mesh` / `tick_mesh` | Shipped |
| Flat color per draw | Shipped |
| UV storage | Shipped |
| `draw_mesh_textured` / `tick_mesh_textured` | **Shipped (Layer 2.1)** |
| `draw_mesh_many` / `draw_mesh_textured_many` | **Shipped** — see [mesh-instances.md](mesh-instances.md) |
| glTF / scene graph / PBR | Out of scope |

## Mental model

| 2D | 3D Layer 2 |
|----|------------|
| `load_atlas` | `load_mesh` |
| `draw_sprite(atlas, …)` | `draw_mesh(mesh, model, color)` |
| atlas blit | `draw_mesh_textured(mesh, model, atlas)` |
| Retained 2D command layer | **Not** the same — meshes are vertex/index buffers |

## See also

- [3d-plan.md](3d-plan.md) — roadmap
- [mesh-instances.md](mesh-instances.md) — `draw_mesh_many` / `draw_mesh_textured_many`
- [3d-mesh-bench.md](3d-mesh-bench.md) — throughput vs immediate `tris_3d` / textured
- [3d-tri-bench.md](3d-tri-bench.md) — Layer 1 immediate triangles
