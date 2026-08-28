# Tile Hi-Z depth reject (Layer 1 CPU fill)

> **Current production:** raster tiles are **128×128**. This page records Hi-Z as implemented on **64×64** tiles; the reject rule is the same, per raster tile.

Per-tile hierarchical depth for the CPU triangle raster. When a tile’s farthest stored window depth is nearer than a triangle’s closest vertex depth, the tile skips the pixel loop for that triangle.

## Mechanism

- **Depth convention:** GL-style `[0,1]`, clear = `1.0` (far), depth test is `<=`.
- **Tile occluder:** `tile_max` = max depth sample in the tile (farthest visible surface in that tile).
- **Triangle nearest:** `tri_min` = min window depth over the three post-project vertices (`z/w` → `0.5*z+0.5`). Linear in screen space, so the triangle minimum is at a vertex.
- **Reject rule:** `tri_min > tile_max` → skip `RasterScreenTriTile` for this tile (conservative: if even the nearest vertex is behind the tile’s farthest occluder, every covered pixel fails the depth test).
- **Update:** After the first raster write in a tile, `tile_max` advances from the farthest depth actually written by that triangle (`MergeTileHiZFromWrite`). No per-triangle 64×64 depth rescan. `ScanTileMaxDepth` remains for tests / fallback (AVX2 8-wide reduction when available).
- **Persist:** `tile_max_depth` is retained across `RasterScreenTrisTiled` calls until depth is cleared (`TileHiZEpoch` bump from `DepthBuffer::Clear` / `Resize` / `Reset`). Later draws in the same frame reuse prior occluder `tile_max` for `TriTileDepthReject` + span reject.
- **Row skip:** When a triangle's pixel AABB inside the tile has every interpolated depth on a row strictly behind `tile_max`, the row skips fill work (conservative linear span min; partial edge coverage included).
- **Block skip:** Same rule per AVX2/SSE SIMD block (8- or 4-wide) before the opaque flat fill inner loop.
- **Front-to-back bin order:** Before tile binning, draws with depth on may reorder tris by cached `tri_min` (ascending). Nearer tris raster first so write-track `tile_max` is tighter when overlapping farther tris in the same draw are processed. Skipped when: (1) an 8-sample probe looks uniform-depth (flat mesh), (2) depth span is negligible, (3) ≤4 tris sit at the global minimum (fullscreen occluder prefix already nearest-first), or (4) >25% of tris share the minimum depth (coplanar layer — reorder cannot help).

OpenMP still parallelizes over tiles; Hi-Z state is per tile with no cross-tile sharing.

## Benchmarks (this VM, Release, `-march=native`, OpenMP 4 threads, headless)

Same machine class as [3d-tri-bench.md](3d-tri-bench.md).

### Open workload (little overlap — scan removal dominates)

| Bench | Before (rescan) | After (write-track) | Δ |
|-------|-----------------|---------------------|---|
| `cpu_tri_bench` | **4.35e6** tris/s (276 ms) | **5.72e6** tris/s (210 ms) | **~+31%** |
| `cpu_mesh_bench` flat | **8.41e6** tris/s (140 ms) | **10.1e6** tris/s (116 ms) | **~+21%** |
| `cpu_mesh_bench` textured | **6.98e6** tris/s (168 ms) | **9.45e6** tris/s (124 ms) | **~+35%** |

Open scenes still paid a full 64×64 depth scan after most raster writes (while `tile_max < 1`). Write tracking removes that hot-path cost.

### Occluded workload (fullscreen occluder + back field)

| Bench | Before | After | Δ |
|-------|--------|-------|---|
| `cpu_tri_bench occluded` | **5.53e6** tris/s (217 ms) | **6.32e6** tris/s (190 ms) | **~+14%** |
| `cpu_mesh_bench occluded` | **6.60e6** tris/s (178 ms) | **6.40e6** tris/s (184 ms) | **~noise** |

Occluded tri gains from fewer rescans on partial occluder tiles plus row skip. Occluded mesh was already Hi-Z-bound on back tris; open mesh flat/textured were the big winners.

### Historical (first Hi-Z landing vs no Hi-Z)

| Bench | Without Hi-Z | With Hi-Z (rescan) | Δ wall time |
|-------|--------------|-----------|-------------|
| `cpu_tri_bench occluded` | 349 ms (**3.44e6** tris/s) | 201 ms (**5.96e6** tris/s) | **~−42%** |
| `cpu_mesh_bench occluded` | 243 ms (**4.84e6** tris/s) | 167 ms (**7.04e6** tris/s) | **~−31%** |

Occluded tri: 2 front tris + 10 000 back tris in one `TickTris3d`. Occluded mesh: combined mesh (2 occluder tris + 70×70 grid) in one `TickMesh`.

### Front-to-back sort (write-track + in-draw Hi-Z), same session

Paired before/after on write-track baseline (`main` after PR #19). Release, `-march=native`, OpenMP, headless.

| Bench | Before (write-track) | After (+ front-to-back) | Δ |
|-------|----------------------|---------------------------|---|
| `cpu_tri_bench` | **4.52e6** tris/s (265 ms) | **7.88e6** tris/s (152 ms) | **~+74%** |
| `cpu_tri_bench occluded` | **5.37e6** tris/s (224 ms) | **5.17e6** tris/s (232 ms) | **~noise** |
| `cpu_mesh_bench` flat | **8.09e6** tris/s (145 ms) | **7.42e6** tris/s (159 ms) | **~noise** |
| `cpu_mesh_bench` textured | **7.22e6** tris/s (163 ms) | **6.75e6** tris/s (174 ms) | **~noise** |
| `cpu_mesh_bench` occluded | **5.86e6** tris/s (201 ms) | **5.74e6** tris/s (205 ms) | **~noise** |

Open tri (10k scattered quads, varying `z`): sort enables in-draw Hi-Z between overlapping tris — large win. Occluded draws skip sort (tiny near-depth prefix); mesh flat/textured skip sort (uniform/coplanar depth). No regressions beyond run-to-run noise on those paths.

### In-tile span depth reject (partial coverage), same session

Extends Hi-Z past tile-level `tri_min` reject into `RasterScreenTriTile`: when `tile_max < far`, skip rows and SIMD blocks whose minimum interpolated window depth exceeds `tile_max` (linear in x; conservative for partial edge coverage). Scalar pixels and textured opaque texels use the same rule before loading the depth buffer.

Paired before/after on write-track + front-to-back `main` (this VM, Release, `-march=native`, OpenMP, headless).

| Bench | Before | After (+ span reject) | Δ |
|-------|--------|-------------------------|---|
| `cpu_tri_bench` | **9.75e6** tris/s (123 ms) | **9.65e6** tris/s (124 ms) | **~noise** |
| `cpu_tri_bench occluded` | **6.66e6** tris/s (180 ms) | **7.00e6** tris/s (171 ms) | **~+5%** |
| `cpu_mesh_bench` flat | **10.7e6** tris/s (110 ms) | **10.6e6** tris/s (111 ms) | **~noise** |
| `cpu_mesh_bench` textured | **9.89e6** tris/s (119 ms) | **10.5e6** tris/s (113 ms) | **~+6%** |
| `cpu_mesh_bench` occluded | **7.17e6** tris/s (164 ms) | **7.18e6** tris/s (164 ms) | **~noise** |

Occluded / textured paths benefit when back-field tris enter tiles with tight `tile_max` but only partially overlap the AABB (row/block skip avoids SIMD fill and atlas sampling). Open scattered tris already overlap heavily after front-to-back sort; extra span checks are ~neutral.

### Persist Hi-Z across draws (until depth clear), same session

`RasterScreenTrisTiled` no longer `std::fill`s `tile_max_depth` at every draw. `MeshDrawScratch::tile_max_depth` survives until `DepthBuffer::Clear` / `Resize` / `Reset` bumps `TileHiZEpoch()` (also on `kClear` via `depth->Clear`). A later `Tris3d` / `DrawMesh` in the same frame rejects back-field tris against occluders from an earlier draw. Hi-Z resets when the epoch changes or tile grid size changes.

Paired interleaved before/after on this VM (Release, `-march=native`, OpenMP, headless). Single-draw benches should stay ~noise; the win is on **two-draw occluded** (`occluded-2draw`).

| Bench | Before (fill each draw) | After (persist) | Δ |
|-------|-------------------------|-----------------|-----|
| `cpu_tri_bench` | **9.02e6** tris/s | **8.61e6** tris/s | **~noise** |
| `cpu_tri_bench occluded` | **5.20e6** tris/s | **5.58e6** tris/s | **~noise** |
| `cpu_tri_bench occluded-2draw` | **3.13e6** tris/s | **4.37e6** tris/s | **~+40%** |
| `cpu_mesh_bench` flat | **7.14e6** tris/s | **7.33e6** tris/s | **~noise** |
| `cpu_mesh_bench` textured | **6.71e6** tris/s | **7.28e6** tris/s | **~noise** |
| `cpu_mesh_bench occluded` | **5.99e6** tris/s | **3.85e6** tris/s | **~noise** (run variance) |
| `cpu_mesh_bench occluded-2draw` | **4.34e6** tris/s | **3.53e6** tris/s | **~noise** (run variance) |

Two-draw tri bench: clear + occluder `Tris3d`, then back-field `Tris3d` without clearing depth. Mesh variant: `DrawMesh` occluder then grid mesh.

### Tile AABB depth probe before edge setup — **not shipped**

See [depth-prepass.md §4](depth-prepass.md#4-tile-aabb-depth-probe-before-half-space-setup--reverted-no-win): corner-min z/w probe (and tile-loop skip) before `MakeHalfEdge` / SIMD constants. Paired runs on this VM were ~noise — vertex Hi-Z already rejects the standard occluder back-field; probe overhead dominates when it rarely fires.

### Bin-time persist Hi-Z skip — **not shipped**

**Idea:** In `RasterScreenTrisTiled`, when `tile_max_depth` already has tight occluders from a prior draw in the same Hi-Z epoch, skip `push_back` into tile bins if `TriTileDepthRejectMin` would reject at fill time. Optional whole-tri skip when `tri_min` is behind the max `tile_max` over the tri’s tile AABB.

**Why it lost:** Fill-time `TriTileDepthReject` already skips the pixel loop cheaply (one float compare per tile-list entry). Bin-time skip adds a per-tri AABB `tile_max` scan (and often reuses or computes `tri_min`) on the persist path without removing enough bin/fill work. Primary metric regressed; open/mesh paths also noisy-to-negative.

Paired interleaved before/after on this VM (Release, `-march=native`, OpenMP, headless, 8 pairs + 16 extra pairs on primary).

| Bench | Before (tris/s) | After (bin-time skip) | Δ |
|-------|-----------------|------------------------|---|
| `cpu_tri_bench` | **8.93e6** | **8.98e6** | **~noise (+0.5%)** |
| `cpu_tri_bench occluded` | **4.28e6** | **4.37e6** | **~noise (+2.1%)** |
| `cpu_tri_bench occluded-2draw` | **4.09e6** | **3.95e6** | **~−3.5%** (8 pairs); **~−8.8%** (16 extra pairs) |
| `cpu_mesh_bench` flat | **8.80e6** | **8.19e6** | **~−6.9%** |
| `cpu_mesh_bench` textured | **5.94e6** | **5.73e6** | **~−3.6%** |
| `cpu_mesh_bench` occluded | **4.59e6** | **4.54e6** | **~noise (−1.1%)** |
| `cpu_mesh_bench occluded-2draw` | **4.44e6** | **4.54e6** | **~noise (+2.3%)** |

**Do not retry** without a profile showing bin push + tile-list walks dominate over `tri_min` + AABB `tile_max` scans on the persist path.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_tri_bench occluded
./build/cpu_tri_bench occluded-2draw
./build/cpu_mesh_bench flat
./build/cpu_mesh_bench occluded
./build/cpu_mesh_bench occluded-2draw
```

Portable ISA: `cmake ... -DHYPERLITE_MARCH=x86-64` (see [simd-tri-fill.md](simd-tri-fill.md)).
