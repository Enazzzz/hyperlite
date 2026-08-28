# Tile-local scratch fill (investigation — not shipped)

Follow-up to [simd-tri-fill.md](simd-tri-fill.md) and [simd-clear.md](simd-clear.md). Hypothesis: rasterizing each 64×64 OpenMP tile into a **thread-local contiguous scratch** (RGBA8 + float32 depth, pitch = tile width) instead of strided global `y*width+x` accesses, then writing the tile back, would cut fill bandwidth on dense meshes.

**Outcome:** Correctness fixable, but paired benches regressed **~17–36%** on this VM. Engine code **not merged**; keep direct global fill.

## What was tried

In `RasterScreenTrisTiled` (`cpu_tri_raster_3d.hpp`):

1. Per-OpenMP-thread `TileFillScratch` (64×64×RGBA8 + 64×64×float depth, 32-byte aligned).
2. Per active tile: **copy-in** global depth (required for persisted Hi-Z / multi-draw LE tests).
3. Fill via existing `RasterScreenTriTile` with local pitch + origin (`local_pitch = tile_w`, `depth_mem` scratch pointer).
4. **Write-back** dirty color (+ depth) to global row-major buffers.

Variants:

| Variant | Write-back | Color copy-in | Tests | Notes |
|---------|------------|---------------|-------|-------|
| A | Merged tile AABB after all tris | Depth only | **Fail** `depth_tri_tests` near-clip | AABB gaps wrote uninitialized scratch (0,0,0) |
| B | Merged tile AABB | Color + depth | Pass | ~**−18%** vs main on immediate + flat mesh |
| C | Per-tri dirty AABB | Depth only | **Fail** near-clip | Single-tri AABB still wider than coverage |
| D | Per-tri dirty AABB | Color + depth once/tile | Pass | ~**−17–36%** (extra write-back calls hurt occluded paths) |

## Paired benches (this VM, Release headless, native march, OpenMP)

Main baseline vs best passing variant **B** (merged write-back + color/depth copy-in):

| Bench | Main (tris/s) | Tile scratch B (tris/s) | Δ |
|-------|---------------|-------------------------|---|
| `cpu_tri_bench` | **9.48e6** | **7.81e6** | **~−18%** |
| `cpu_tri_bench occluded` | **6.61e6** | *(not re-run for B)* | — |
| `cpu_mesh_bench flat` | **10.02e6** | **8.17e6** | **~−18%** |
| `cpu_mesh_bench textured` | **9.11e6** | *(not re-run for B)* | — |

Variant **D** (per-tri write-back, all tests green):

| Bench | Main (tris/s) | Tile scratch D (tris/s) | Δ |
|-------|---------------|-------------------------|---|
| `cpu_tri_bench` | **9.48e6** | **7.83e6** | **~−17%** |
| `cpu_tri_bench occluded` | **6.61e6** | **4.30e6** | **~−35%** |
| `cpu_tri_bench occluded-2draw` | **4.82e6** | **3.34e6** | **~−31%** |
| `cpu_mesh_bench flat` | **10.02e6** | **6.40e6** | **~−36%** |
| `cpu_mesh_bench textured` | **9.11e6** | **5.81e6** | **~−36%** |
| `cpu_mesh_bench occluded` | **6.71e6** | **4.43e6** | **~−34%** |
| `cpu_mesh_bench occluded-2draw` | **5.47e6** | **3.65e6** | **~−33%** |

`ctest` green on variant B/D (native); portable `x86-64` not re-benchmarked after revert (no engine delta shipped).

## Why it lost

- **Extra traffic dominates:** each active tile pays at least one 64×w depth copy-in (~16 KiB) plus color copy-in (~16 KiB) when write-back uses pixel bounds wider than actual coverage (always, for AABB dirty rects). Write-back adds another read+write of the dirty rect.
- **Fill is already cache-friendly enough:** SIMD blocks touch contiguous `row_depth + x` spans; global stride is large but each tile’s rows are still sequential within the tile. Scratch adds a full-tile memcpy tax before/after fill.
- **Correctness tax:** merged dirty AABBs across tris (or half-open coverage vs AABB) require color copy-in so write-back does not spill black `(0,0,0)` into gaps. Per-tri write-back avoids merged gaps but multiplies write-back syscall/memcpy overhead — worse on Hi-Z occluded workloads.

## Do not retry (without a new idea)

- Thread-local 64×64 color+depth scratch with bulk tile write-back in `RasterScreenTrisTiled` as described above.
- Depth-only copy-in with AABB write-back (correctness failure on clipped/near tris).
- Per-tri write-back from tile scratch on every triangle (amplifies overhead on occluded bins).

If revisiting: need **exact dirty tracking** (bitmask or span list per tile, not tri AABB) *and* a proof that copy-in can be skipped when Hi-Z/`TileHiZEpoch` proves the tile depth is uniform far — still unlikely to beat direct fill on full-screen meshes without measured win on copy elision alone.
