# 3D wireframe benchmarks (Layer 0)

Measured on the Hyperlite Linux cloud VM (same class as [linux-bench.md](linux-bench.md) / [cloud-agent-vm-specs.md](cloud-agent-vm-specs.md)). Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores |
| ISA | x86_64 with AVX2 + FMA (`-march=native`) |
| Compiler | g++ 13.3.0 |
| OpenMP | 4.5 (4 threads) — **disabled for depth-on 3D** (serial segments to avoid depth races); enabled for depth-off 3D color-only |
| Present | headless |
| CUDA | off |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
```

## What shipped (this PR)

In `cpu_line_raster_3d.hpp` / `depth_buffer.hpp` (public API unchanged):

1. **Window-depth lerp** — drop dead `inv_w` math in the pixel loop; screen-space lerp of window depth (affine of NDC `z/w`).
2. **Axis-aligned spans** — exact horizontal/vertical use span fills; opaque+depth horizontal uses AVX2 8-wide compare/blend/maskstore when `__AVX2__`.
3. **Pointer-walked Bresenham** — incremental window `z`, no per-pixel `t` / bounds re-check; opaque+depth specialized tight loop.
4. **Outcode fast path** — trivial reject before Cohen–Sutherland; trivial accept skips the clip loop.
5. **AVX2+FMA batch MVP** — transform 8 endpoints (4 segments) per pack when `__AVX2__ && __FMA__`; scalar remainder / `HYPERLITE_MARCH=x86-64`.
6. **OpenMP over segments** — only when `depth == nullptr` (same race rule as tris).

## Paired benches (this VM, interleaved 6 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`. Before = current `main`; after = this PR. Same binaries alternated to reduce noise.

Workload: **120 frames × 10 000 lines @ 1280×720**, vsync off, headless.

| Bench | Before (lines/s) | After (lines/s) | Δ |
|-------|------------------|-----------------|---|
| `cpu_line_3d_bench` (world + depth on) | **9.04e6** | **11.38e6** | **~+26%** |
| `cpu_line_bench` (2D int32) | **8.97e6** | **8.93e6** | **~flat (noise)** |

Portable `HYPERLITE_MARCH=x86-64` (no AVX2 batch / horizontal SIMD): `cpu_line_3d_bench` ≈ **8.6e6** on this VM; `ctest` green on native and portable.

## Experiments

| Experiment | Result |
|------------|--------|
| Window-depth lerp + pointer Bresenham + opaque tight loop | **Shipped** — primary per-pixel win |
| Exact H/V spans + AVX2 opaque depth horizontal | **Shipped** (gated); helps long axis-aligned; bench segs are short/diagonal |
| Outcode trivial accept/reject | **Shipped** |
| AVX2+FMA 8-endpoint MVP batch (4 segs) | **Shipped** (gated); measurable on this short-seg workload |
| OpenMP when depth off | **Shipped**; depth-on stays serial |
| Run-length spans for mostly-horizontal Bresenham | **Dropped** — net **loss** (~−10%) on short diagonal bench segs (span setup > benefit) |
| Shared-vert transform-once (mesh-style) | **N/A** — Layer 0 segments are independent endpoints; batch MVP covers the win |
| OpenMP / tiles with depth on | **Not tried** (forbidden without per-tile ownership) |
| Touch triangle fill | **Out of scope** |

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_line_bench
./build/cpu_line_3d_bench
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
ctest --test-dir build-portable --output-on-failure
```
