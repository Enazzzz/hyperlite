# Mesh transform / clip / bin (Layer 2 hot path)

Follow-up to [simd-tri-fill.md](simd-tri-fill.md): after AVX2 / AVX-512VL fill, `cpu_mesh_bench` stayed nearly flat because time moved **upstream** of `RasterScreenTriTile`. This change speeds retained meshes by optimizing MVP transform, homogeneous clip, and 64×64 binning — **not** the pixel fill. Hyperlite still owns all pixels (no Vulkan/GL/D3D/Metal).

## Investigation (this VM)

Phase split on the 70×70 grid @ 1280×720 (Release headless, before this PR):

| Phase | Share of frame |
|-------|----------------|
| Clear color + depth | ~18% |
| Emit (per-corner MVP × 3 + Sutherland–Hodgman + project) | ~43% |
| Bin 64×64 + tiled fill | ~39% |

Flat ≈ textured tris/s on main (~5.2–5.3e6) also pointed at transform/clip/bin, not atlas fill.

Indexed mesh: **5 041** unique verts vs **9 800** tris → old path ran ~**29 400** `MulViewProj` + full frustum clips per draw.

## What shipped

In `cpu_tri_raster_3d.hpp` (public API unchanged):

1. **Transform once per mesh vertex** — `TransformMeshPositions` writes clip-space + outcodes; trivial-in verts are **projected once**. Optional AVX2+FMA 8-wide AoS gather when `__AVX2__ && __FMA__` (native); scalar remainder / `HYPERLITE_MARCH=x86-64`.
2. **Outcode clip** — `ClipTriangleHomogeneous` trivial accept/reject; mesh emit uses accept → `TryAppendScreenTri` with pre-projected attrs, reject → skip, else Sutherland–Hodgman only.
3. **Cheaper binning** — nested `min`/`max` (no `initializer_list`); tile lists reused via thread-local scratch (`clear`, keep capacity). Still AABB → tile range only (no per-pixel work).
4. **Scratch reuse** — `MeshDrawScratch` (clip, outcodes, screen attrs, `ScreenTri` list, bins) is process-static across `DrawMesh` / `TickMesh` / immediate tris (not `thread_local`: TLS in a non-PIC static archive fails linking the Python `.so`). No OpenMP over triangles (depth races); OpenMP remains over tiles.

`RasterScreenTriTile` fill inner loop untouched.

## Paired benches (this VM, interleaved 8 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP tiles. Same machine as [cloud-agent-vm-specs.md](cloud-agent-vm-specs.md) / [simd-tri-fill.md](simd-tri-fill.md). Before = current `main` (AVX2 fill + AVX-512VL edge); after = this PR.

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_mesh_bench` flat | **5.31e6** | **8.26e6** | **~+55%** |
| `cpu_mesh_bench` textured | **5.21e6** | **7.68e6** | **~+47%** |
| `cpu_tri_bench` (immediate) | **3.82e6** | **4.74e6** | **~+24%** |

Immediate also gains from outcode accept + reused screen/bin scratch (no shared-vert transform-once). No regression.

Portable `HYPERLITE_MARCH=x86-64` (no AVX2 transform): mesh flat still ~**8.1e6** on this VM — the big win is algorithm/scratch, not the gather kernel. `ctest` green on native and portable.

## Experiments

| Experiment | Result |
|------------|--------|
| Transform-once + project-once + outcode accept | **Shipped** — primary mesh win |
| AVX2+FMA 8-wide gather MVP | **Shipped** (gated); small vs scalar once-per-vert on this grid |
| Process-static scratch (screen + bins) | **Shipped** (not TLS — Python `.so` link) |
| Nested AABB min/max in bin | **Shipped** |
| OpenMP over triangles | **Not tried** (forbidden without per-tile ownership) |
| Rewrite fill / zmm / textured gather | **Out of scope** (already lost or forbidden) |
| CSR two-pass bin counts | **Dropped** — reused `vector` bins were enough; extra pass not needed |

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_mesh_bench
./build/cpu_tri_bench
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
ctest --test-dir build-portable --output-on-failure
```
