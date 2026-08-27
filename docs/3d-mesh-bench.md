# 3D retained-mesh benchmarks (Layer 2 / 2.1)

Measured on the Hyperlite Linux cloud VM (same class as [linux-bench.md](linux-bench.md) / [3d-tri-bench.md](3d-tri-bench.md)). Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores |
| ISA | x86_64 with AVX2 (`-march=native`) |
| Compiler | g++ 13.3.0 |
| OpenMP | 4.5 (4 threads) — tile parallel over 64×64 bins |
| Present | headless |
| CUDA | off |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
```

## Results (Layer 2.1 PR)

Workload: **120 frames @ 1280×720**, vsync off, headless, depth on, backface cull on, perspective camera. Same 70×70 quad grid (9 800 tris) for flat and textured.

| Bench | Path | Tris/frame | `total_ms` | Throughput |
|-------|------|------------|------------|------------|
| `cpu_tri_bench` (immediate) | `TickTris3d` | 10 000 | **598.7** | **2.00e6** tris/s |
| `cpu_mesh_bench` (flat) | `TickMesh` | 9 800 (70×70 quads) | **229.6** | **5.12e6** tris/s |
| `cpu_mesh_bench` (textured) | `TickMeshTextured` | 9 800 (70×70 quads) | **240.7** | **4.89e6** tris/s |

Notes:

- Mesh path loads once (`load_mesh`), then draws with identity/near-identity model each frame — no per-frame vertex upload from the caller.
- Textured path uses the same mesh UVs plus a 64×64 opaque checker atlas (`load_atlas`); fill samples nearest + clamp with perspective-correct UV.
- Immediate path rebuilds a 10k-tri float buffer every frame (`FillWorldTris`) then `TickTris3d` — closer to dynamic geometry.
- Geometry and screen coverage differ (scattered tiny tris vs one large indexed grid); treat the table as paired measurements on one build/machine, not a strict apples-to-apples microbench.
- Flat and textured share the same tiled half-space raster + depth; textured adds per-pixel UV divide + atlas fetch (opaque atlas → same depth-write path as flat).

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_tri_bench
./build/cpu_mesh_bench          # flat then textured
./build/cpu_mesh_bench flat     # flat only
./build/cpu_mesh_bench textured # textured only
```
