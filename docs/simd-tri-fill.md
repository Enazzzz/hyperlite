# SIMD triangle fill (Layer 1 hot path)

Upgrade of the existing tiled half-space fill in `cpu_tri_raster_3d.hpp` — **not** a second rasterizer. Present remains a blit of Hyperlite’s RGBA8 buffer. No graphics API.

## What shipped

Inside `RasterScreenTriTile` (shared by immediate tris, flat mesh, textured mesh):

1. **Incremental half-edge + attribute setup** — edge coeffs `A,B,C`, step `A` per column / `B` per row; incremental window depth and (textured) `u/w`, `v/w`, `1/w`.
2. **Tile AABB reject** — trivial out when the pixel-center box is outside any half-plane (top-left aware).
3. **Tile Hi-Z reject** — when `tri_min > tile_max` for the 64×64 tile, skip the pixel loop ([hiz-tile-depth.md](hiz-tile-depth.md)).
4. **Tile AABB accept** — when the box is inside all three half-planes, skip coverage tests (depth + color/UV only).
4. **Pixel-block SIMD (opaque flat)** — AVX2 8-wide edge masks + depth/color stores; SSE4.2 4-wide fallback; scalar remainder. Gated on `__AVX2__` / `__SSE4_2__`.
5. **AVX-512VL edge path (opaque flat)** — when `__AVX512F__` + `__AVX512VL__` are set, edge (non–fully-covered) blocks use 8-wide **ymm + k-mask** compares/stores (`FillOpaqueFlatBlock8Vl`) instead of movemask/`maskstore` casts. Same lane width as AVX2; no zmm in the hot path. Dense interior tiles keep classic AVX2 stores.
6. **Dirty-mask bitscan** — `ExpandDirtyFromMask` walks set bits with `std::countr_zero` (helps sparse edge blocks on the immediate path; portable vs MSVC).
7. **Textured path** — same reject/accept + incremental perspective UV; **hybrid opaque fill** (shipped): AVX2/AVX-512VL 8-wide (SSE4.2 4-wide) coverage + depth test/write blocks identical to opaque flat, then **scalar** nearest-clamp atlas sample + color store only for depth-passed lanes. Translucent texels revert speculative depth writes in the scalar tail. Gather-based UV SIMD remains **not shipped** (see failed experiments).

Public Python/C++ API unchanged. Top-left, less-equal depth, opaque depth-write / translucent test-only, nearest clamp UV preserved. `ctest` green including `HYPERLITE_MARCH=x86-64` (no AVX2/AVX-512 required).

## Paired benches (this VM, same flags)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP tiles. Machine: 4-core KVM Xeon (CPUID family 6 model 207) with AVX2 + AVX-512 (see [cloud-agent-vm-specs.md](cloud-agent-vm-specs.md)).

### AVX2 fill PR (historical, vs pre-SIMD main)

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` (immediate) | **2.33e6** | **3.55e6** | **~+52%** |
| `cpu_mesh_bench` flat | **4.80e6** | **~5.1e6** | **~+6%** |
| `cpu_mesh_bench` textured | **4.71e6** | **~5.1e6** | **~+9%** |

### AVX-512 follow-up (this PR, vs current main / AVX2 fill)

Interleaved before/after on the same binaries (8 pairs, Release headless). Means:

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` (immediate) | **3.69e6** | **3.79e6** | **~+2.7%** |
| `cpu_mesh_bench` flat | **~5.45e6** | **~5.46e6** | **~flat (noise)** |
| `cpu_mesh_bench` textured | **~5.26e6** | **~5.24e6** | **~flat (noise)** |

Immediate path is still the fill/edge-bound winner (tiny scattered tris). Mesh grids remain more transform/clip/bin bound, so edge-mask / bitscan tweaks barely move them. See [3d-tri-bench.md](3d-tri-bench.md) / [3d-mesh-bench.md](3d-mesh-bench.md).

### Mesh transform / clip / bin (follow-up)

Upstream of fill: transform-once, outcode clip, scratch/bin reuse. Interleaved vs this AVX-512VL main on the same VM: mesh flat **~+55%**, textured **~+47%**, immediate **~+24%** (no fill rewrite). Details: [mesh-transform.md](mesh-transform.md).

### Textured hybrid SIMD coverage+depth (this PR, vs main pre-change)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP tiles. Same 4-core KVM Xeon VM; 8 runs each before/after on one session (not interleaved per run, same binary flags).

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` (immediate) | **9.10e6** | **8.95e6** | **~flat (noise)** |
| `cpu_tri_bench occluded` | **4.75e6** | **4.90e6** | **~flat (noise)** |
| `cpu_tri_bench occluded-2draw` | **3.84e6** | **3.75e6** | **~flat (noise)** |
| `cpu_mesh_bench flat` | **6.59e6** | **7.04e6** | **~flat (noise)** |
| `cpu_mesh_bench textured` | **6.37e6** | **6.68e6** | **~+5%** |
| `cpu_mesh_bench occluded` | **4.67e6** | **5.01e6** | **~flat (noise)** |
| `cpu_mesh_bench occluded-2draw` | **4.30e6** | **4.04e6** | **~flat (noise)** |

**Outcome: ship.** Textured mesh fill gains ~5% from SIMD edge coverage + depth blocks while keeping atlas sampling scalar (no gather). Immediate / flat / occluded paths unchanged in code; deltas within run-to-run noise.

## Failed / dropped experiments

| Experiment | Result |
|------------|--------|
| **AVX-512 16-wide zmm** opaque coverage + depth + color (`FillOpaqueFlatBlock16`) | Net **loss** on `cpu_mesh_bench` flat (~−3–4%) and only tiny immediate uplift once noise was averaged. Dense 512-bit stores / frequency effects on this VM dominate. **Reverted** (not shipped). |
| **Hybrid zmm edge + AVX2 interior** | Still **loss** on mesh: setting up `__m512` constants every opaque triangle executes 512-bit uops even when the tile only uses ymm stores. **Reverted**. |
| **Textured SIMD gather** (AVX2 gather and AVX-512VL gather UV/depth blocks) | Net **loss** on `cpu_mesh_bench` textured vs scalar incremental UV. Dense atlas sampling prefers the tight scalar loop. **Not shipped**. |
| **Textured hybrid SIMD coverage+depth + scalar sample** (this experiment) | **~+5%** on `cpu_mesh_bench textured` (8-run means, same VM). **Shipped**: reuse opaque-flat 8-/4-wide blocks for coverage + depth; scalar `SampleAtlasNearest` + store for passed lanes only. |
| **AMX** | **Skipped** (no profiler evidence a dense AMX kernel would beat tile fill; tile-config overhead wrong tool for sparse 64×64 bins). |
| **Per-row half-plane reject** inside the AABB | Net **loss** (AABB already tight). Reverted in the AVX2 PR. |
| **Per-triangle 64×64 `ScanTileMaxDepth` after writes** | Hot-path **loss** on open mesh/tri (rescans dominate). **Replaced** by write-track Hi-Z + optional row skip ([hiz-tile-depth.md](hiz-tile-depth.md)). |
| **SIMD `ScanTileMaxDepth` alone** (AVX2 8-wide) | Helps only if rescans remain; write-track removes the hot-path caller. Kept for tests / fallback. |
| **Fixed-point / subpixel edge functions** | **Not shipped** (same rationale as AVX2 PR). |
| **OpenMP over triangles** | Still **forbidden** (depth races). Tiles own pixels. |
| **SIMD / streaming color + depth clear** | **Not shipped** — noise on cacheable SIMD; large loss with `MOVNT*` before immediate raster read. See [simd-clear.md](simd-clear.md). |

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_mesh_bench
# Portable story (no AVX2 / AVX-512 required):
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
ctest --test-dir build-portable --output-on-failure
```
