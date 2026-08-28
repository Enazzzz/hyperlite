# 64×64 tile binning / vertex Hi-Z micro-opts

Follow-up to [hiz-tile-depth.md](hiz-tile-depth.md) and [mesh-transform.md](mesh-transform.md). Goal: speed `RasterScreenTrisTiled` binning and/or cheaper vertex Hi-Z without a second raster pass or public API changes.

## What shipped (PR #24)

**Single-tile bin fast path** in `RasterScreenTrisTiled`: when a screen tri’s clamped tile AABB is 1×1, push its index once and skip the nested `ty`/`tx` loops. Most immediate-path tris and grid mesh quads hit this branch.

## Paired benches (16 interleaved pairs, this VM)

Release, headless, `HYPERLITE_MARCH=native`, OpenMP tiles.

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` (open) | **4.64e6** | **4.67e6** | **~+0.7%** |
| `cpu_tri_bench occluded` | **5.88e6** | **5.89e6** | **~+0.3%** |
| `cpu_mesh_bench` flat | **8.31e6** | **8.37e6** | **~+0.7%** |
| `cpu_mesh_bench` textured | **7.26e6** | **7.38e6** | **~+1.7%** |
| `cpu_mesh_bench` occluded | **7.26e6** | **7.27e6** | **~+0.2%** |

Open stays within noise; gains are modest.

## Experiments reverted (do not retry)

| Experiment | Result |
|------------|--------|
| **Two-pass CSR bin** (count → resize → fill) | **Loss** ~−13% mesh flat — triple scan + `resize` dominated `push_back` with retained capacity. |
| **`min_win_z` on `ScreenTri` / eager bin precompute** | **Open regression ~−3%** — pays `ScreenSignedArea2` for every tri even when Hi-Z never fires. |
| **Lazy per-tri Hi-Z cache in parallel OpenMP tiles** | **Unsafe** (unsynchronized writes) + no win once fixed. |
| **Selective precompute** (only tris in multi-tri tiles) | Extra pass; ~noise / slight loss on occluded. |
| **`active_tiles` OpenMP list** | ~noise / slight loss — 240 tiles is already small. |
| **`FloorDivTile` (`>> 6`)** | ~noise when paired; not shipped. |
| **Bin `reserve` hint from `tris/tiles`** | No measurable win. |
| **Bin-time persist Hi-Z skip** (`TriTileDepthRejectMin` before bin `push_back`) | **Loss ~−4…−9%** on `cpu_tri_bench occluded-2draw`; **~−7%** mesh flat — per-tri AABB `tile_max` scan + `tri_min` on persist path; fill-time reject already cheap. See [hiz-tile-depth.md § Bin-time persist Hi-Z skip](hiz-tile-depth.md#bin-time-persist-hi-z-skip--not-shipped). **Do not retry** without hot-sample proof bin is memcpy-/push-bound. |

Also see main’s shipped Hi-Z follow-ups: write-track (#19), front-to-back sort (#20), span reject (#21), persist-until-clear (#22).

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_tri_bench occluded
./build/cpu_mesh_bench flat
./build/cpu_mesh_bench textured
./build/cpu_mesh_bench occluded
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_MARCH=x86-64
ctest --test-dir build-portable --output-on-failure
```
