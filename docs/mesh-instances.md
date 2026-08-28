# Retained mesh instancing (`draw_mesh_many`)

Layer 2 batch draw: **one mesh, many model matrices**, one native call. Hyperlite still owns every pixel (CPU tiled raster + Hi-Z); this removes per-prop Python/C++ dispatch and repeats tile bin/fill only once per batch.

## API

| Python | C++ | Notes |
|--------|-----|-------|
| `draw_mesh_many(mesh, models, r, g, b, a=255)` | `DrawMeshMany(mesh_id, models16, count, tri_packed)` | Flat color shared by all instances |
| `draw_mesh_textured_many(mesh, models, atlas)` | `DrawMeshTexturedMany(...)` | Same atlas/UV rules as `draw_mesh_textured` |

- `models`: contiguous **float32** buffer, **N × 16** column-major model matrices (NumPy `(N, 16)` works).
- `draw_mesh` / `draw_mesh_textured` / `tick_*` are unchanged.
- `instance_count == 0` or invalid handles → no-op.
- `instance_count == 1` → delegates to the single-draw path (pixel parity with `draw_mesh`).

## Pipeline

Per batch:

1. `FlushPending2d()` once (same as `draw_mesh`).
2. For each instance: `view_proj × model` → transform-once clip/emit (PR #10 scratch).
3. Append all `ScreenTri`s into shared `MeshDrawScratch::screen`.
4. **One** `RasterScreenTrisTiled` (128×128 raster bins, OpenMP over tiles, Hi-Z per tile). Dirty present tiles remain 64px.

Transform/clip cost still scales with instances × vertices; bin + fill + Python call overhead drops from **O(draws)** to **O(batches)**.

## `proc_world` wiring

Props (trees, rocks, cube city) batch by mesh + color — up to **3** `draw_mesh_many` calls per frame after distance cull. Terrain chunks stay per-chunk `draw_mesh_textured` (unique topology/mesh handle).

## Bench (this repo, seed 42, `--preset limit --headless --seconds 8`)

| | fps | ms | chunks | tris | props |
|---|-----|-----|--------|------|-------|
| **Before** (per-prop `draw_mesh`) | 45.1 | 22.18 | 115 | 415824 | 3588 |
| **After** (`draw_mesh_many`) | 65.2 | 15.35 | 118 | 427635 | 3612 |

City-overflight spikes moved from **~18.5 fps / ~578k tris** to **~37 fps** at similar triangle load (seed 42, 8 s headless, limit preset). First run after process start can be slower while buffers warm up; numbers above are from steady runs.

Microbench: `./build/cpu_mesh_bench` flat/textured unchanged (single instance). `./build/cpu_mesh_bench instanced` — 128 instances, one `DrawMeshMany` per frame.

## Tests

- `mesh_layer_tests`: N=2 depth, N=1 pixel parity vs `draw_mesh`.
- `mesh_textured_tests`: `draw_mesh_textured_many` atlas sampling.

Portable: `cmake -DHYPERLITE_MARCH=x86-64 … && ctest --test-dir build-portable --output-on-failure`.
