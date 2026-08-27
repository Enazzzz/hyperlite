# Tile Hi-Z depth reject (Layer 1 CPU fill)

Per-64×64-tile hierarchical depth for the CPU triangle raster. When a tile’s farthest stored window depth is nearer than a triangle’s closest vertex depth, the tile skips the pixel loop for that triangle.

## Mechanism

- **Depth convention:** GL-style `[0,1]`, clear = `1.0` (far), depth test is `<=`.
- **Tile occluder:** `tile_max` = max depth sample in the tile (farthest visible surface in that tile).
- **Triangle nearest:** `tri_min` = min window depth over the three post-project vertices (`z/w` → `0.5*z+0.5`). Linear in screen space, so the triangle minimum is at a vertex.
- **Reject rule:** `tri_min > tile_max` → skip `RasterScreenTriTile` for this tile (conservative: if even the nearest vertex is behind the tile’s farthest occluder, every covered pixel fails the depth test).
- **Update:** After the first raster write in a tile, scan the tile depth once. While `tile_max < 1.0`, rescan after each subsequent write so `tile_max` stays accurate under overlapping geometry.

OpenMP still parallelizes over tiles; Hi-Z state is per tile with no cross-tile sharing.

## Benchmarks (this VM, Release, `-march=native`, OpenMP 4 threads, headless)

Same machine class as [3d-tri-bench.md](3d-tri-bench.md).

### Open workload (little overlap — no meaningful reject)

| Bench | Before Hi-Z | After Hi-Z | Δ |
|-------|-------------|------------|---|
| `cpu_tri_bench` | **4.51e6** tris/s (266 ms) | **4.47e6** tris/s (268 ms) | ~noise |
| `cpu_mesh_bench` flat | **8.54e6** tris/s (138 ms) | **8.62e6** tris/s (136 ms) | ~noise |
| `cpu_mesh_bench` textured | **7.47e6** tris/s (157 ms) | **7.56e6** tris/s (156 ms) | ~noise |

### Occluded workload (fullscreen occluder + back field)

| Bench | Without Hi-Z | With Hi-Z | Δ wall time |
|-------|--------------|-----------|-------------|
| `cpu_tri_bench occluded` | 349 ms (**3.44e6** tris/s) | 201 ms (**5.96e6** tris/s) | **~−42%** |
| `cpu_mesh_bench occluded` | 243 ms (**4.84e6** tris/s) | 167 ms (**7.04e6** tris/s) | **~−31%** |

Occluded tri: 2 front tris + 10 000 back tris in one `TickTris3d`. Occluded mesh: combined mesh (2 occluder tris + 70×70 grid) in one `TickMesh`.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_tri_bench occluded
./build/cpu_mesh_bench flat
./build/cpu_mesh_bench occluded
```

Portable ISA: `cmake ... -DHYPERLITE_MARCH=x86-64` (see [simd-tri-fill.md](simd-tri-fill.md)).
