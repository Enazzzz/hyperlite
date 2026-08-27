# SIMD triangle fill (Layer 1 hot path)

Upgrade of the existing tiled half-space fill in `cpu_tri_raster_3d.hpp` — **not** a second rasterizer. Present remains a blit of Hyperlite’s RGBA8 buffer. No graphics API.

## What shipped

Inside `RasterScreenTriTile` (shared by immediate tris, flat mesh, textured mesh):

1. **Incremental half-edge + attribute setup** — edge coeffs `A,B,C`, step `A` per column / `B` per row; incremental window depth and (textured) `u/w`, `v/w`, `1/w`.
2. **Tile AABB reject** — trivial out when the pixel-center box is outside any half-plane (top-left aware). No Hi-Z.
3. **Tile AABB accept** — when the box is inside all three half-planes, skip coverage tests (depth + color/UV only).
4. **Pixel-block SIMD (opaque flat)** — AVX2 8-wide edge masks + depth `maskstore` + color `maskstore`; SSE4.2 4-wide fallback; scalar remainder. Gated on `__AVX2__` / `__SSE4_2__` (portable `HYPERLITE_MARCH=x86-64` stays scalar and still passes tests).
5. **Textured path** — same reject/accept + incremental perspective UV; per-pixel nearest clamp unchanged.

Public Python/C++ API unchanged. Top-left, less-equal depth, opaque depth-write / translucent test-only, nearest clamp UV preserved. `ctest` green.

## Paired benches (this VM, same flags)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP tiles. **Before** = `main` binary; **after** = this branch. Same machine, back-to-back.

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` (immediate) | **2.33e6** | **3.55e6** | **~+52%** |
| `cpu_mesh_bench` flat | **4.80e6** | **~5.1e6** | **~+6%** |
| `cpu_mesh_bench` textured | **4.71e6** | **~5.1e6** | **~+9%** |

Immediate path gains most (tiny scattered tris → fill/edge-bound). Mesh grids spend more in transform/clip/bin relative to fill, so SIMD fill helps less but still shows a real paired uplift. See [3d-tri-bench.md](3d-tri-bench.md) / [3d-mesh-bench.md](3d-mesh-bench.md).

## Failed / dropped experiments

| Experiment | Result |
|------------|--------|
| **Per-row half-plane reject** inside the AABB | Net **loss** on these workloads (AABB already tight; extra max-E math every row). Reverted. |
| **Fixed-point / subpixel edge functions** | **Not shipped.** Float incremental edges + SIMD kept top-left tests green; fixed-point would need a separate exactness proof vs the float reference and did not look necessary once SIMD moved the needle. Revisit if profiles pin remaining time on edge compares with fill-rule edge cases. |
| **OpenMP over triangles** | Still **forbidden** (depth races). Tiles own pixels. |

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_mesh_bench
# Portable story (no AVX2 required):
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
```
