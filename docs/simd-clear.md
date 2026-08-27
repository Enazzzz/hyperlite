# SIMD color + depth clear (investigation)

Follow-up to [mesh-transform.md](mesh-transform.md): clear color + depth was ~**18%** of a mesh frame after transform/fill work landed. This experiment tried AVX2 / AVX-512VL / SSE streaming (`MOVNT*`) and regular SIMD clears for the per-frame paths:

- `FillSpan` / `kClear` / `ClearAndRaster*` color (`cpu_blend.hpp`, `rasterizer.cpp`, `cpu_tri_raster_3d.hpp`)
- `DepthBuffer::Clear` / `Resize` init (`depth_buffer.hpp`)

Hyperlite still owns all pixels. Public API unchanged.

## What we tried

1. **`simd_clear.hpp`** — `ClearColorSpan` / `ClearDepthSpan` with AVX2 8-wide, AVX-512VL 8-wide (no 16-wide zmm — same policy as [simd-tri-fill.md](simd-tri-fill.md)), SSE2 4-wide, scalar fallback for `HYPERLITE_MARCH=x86-64`.
2. **Nontemporal streaming stores** — `_mm256_stream_*` / `_mm_stream_*` for spans ≥ 256 samples, with alignment prologue + `_mm_sfence()`.
3. **Regular cacheable SIMD stores** — same widths, no `MOVNT*`.

Depth and color clears run **every frame** and buffers are **read immediately** by the raster loop (fill / Hi-Z). Streaming stores bypass cache and force reload on the next pass.

## Paired benches (this VM, interleaved 8 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP tiles. Same 4-core KVM Xeon as [cloud-agent-vm-specs.md](cloud-agent-vm-specs.md).

### Regular SIMD (cacheable stores)

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` | **1.02e7** | **1.04e7** | **~+1% (noise)** |
| `cpu_tri_bench occluded` | **7.32e6** | **7.27e6** | **~flat** |
| `cpu_tri_bench occluded-2draw` | **5.17e6** | **5.17e6** | **~flat** |
| `cpu_mesh_bench flat` | **1.11e7** | **1.11e7** | **~flat** |
| `cpu_mesh_bench textured` | **1.03e7** | **1.01e7** | **~flat** |
| `cpu_mesh_bench occluded` | **7.63e6** | **7.55e6** | **~flat** |
| `cpu_mesh_bench occluded-2draw` | **6.19e6** | **6.17e6** | **~flat** |

### Nontemporal streaming stores (reverted prototype)

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_tri_bench` | **1.00e7** | **9.53e6** | **~−5%** |
| `cpu_mesh_bench flat` | **1.10e7** | **9.77e6** | **~−11%** |
| `cpu_mesh_bench textured` | **1.03e7** | **9.05e6** | **~−12%** |
| `cpu_mesh_bench occluded` | **7.60e6** | **4.65e6** | **~−39%** |

Streaming also required 32-byte-aligned destinations for `MOVNTDQ` / `MOVNTPS`; unaligned stream stores faulted (fixed with prologue, but perf still lost).

### Clear-only microbench (1280×720, 2000 iters)

| Path | Scalar (`std::fill` / `fill_n`) | SIMD clear | Speedup |
|------|----------------------------------|------------|---------|
| Color RGBA8 | 255.6 ms | 250.9 ms | **~1.02×** |
| Depth float32 | 251.4 ms | 254.2 ms | **~0.99×** |
| Combined | 507.0 ms | 505.1 ms | **~flat** |

At −O3 `-march=native`, the compiler already vectorizes `std::fill` / `std::fill_n` on the depth and color buffers. Color `FillSpan` already used AVX2 `storeu` before this experiment.

## Outcome: **do not ship**

No ≥3–5% win on open mesh/tri; full-frame streaming clear is a **large loss** because clears are immediately followed by raster reads. **Engine code reverted**; keep existing scalar/SSE/AVX2 `FillSpan` and `std::fill` depth clear.

## Do not retry

| Experiment | Result |
|------------|--------|
| Nontemporal stream clear (color + depth) before raster | **Large loss** — cache bypass + immediate readback |
| Dedicated `ClearDepthSpan` AVX2 vs `std::fill` | **Noise / slight loss** — libc/compiler already vectorizes |
| `simd_clear.hpp` wrapper over `FillSpan` | **Noise** — duplicate of existing AVX2 color path |
| 16-wide zmm clear | **Not tried** — same VM policy as opaque zmm fill ([simd-tri-fill.md](simd-tri-fill.md)) |

If clear share rises again, profile whether time is actually in memset vs `InvalidateTileHiZ` / dirty tracking before revisiting.

## Reproduce

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
# interleaved pairs: build main + branch binaries, alternate runs (see simd-tri-fill.md)
./build/cpu_tri_bench
./build/cpu_mesh_bench flat
./build/cpu_mesh_bench textured
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64 -DCMAKE_CXX_COMPILER=g++
ctest --test-dir build-portable --output-on-failure
```
