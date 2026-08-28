# Depth prepass & depth-bandwidth experiments (Layer 1 CPU fill)

Investigation of three engine-side changes aimed at **overdraw** and **depth bandwidth** after tile Hi-Z and SIMD opaque fill landed. None improved the standard benches on this VM; all were reverted. Public Python API unchanged.

> **Current production:** raster tiles are **128×128**. Table rows that say 64×64 describe the Hi-Z grid used during these experiments.

## Context

| Mechanism already shipped | Effect |
|---------------------------|--------|
| Per-64×64 tile Hi-Z | Occluded benches ~−31–42% wall time ([hiz-tile-depth.md](hiz-tile-depth.md)) |
| AVX2 / AVX-512VL 8-wide opaque fill | Open fill bound; 16-wide zmm measured slower |
| Transform-once + `draw_mesh_many` | Mesh path CPU reduction |

Remaining cost on occluded workloads is mostly **pixels that still run the half-space + depth loop** before failing depth, not Python demo LOD.

## Benchmark setup (this VM)

Release, `-march=native`, OpenMP, headless:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
export HYPERLITE_HEADLESS=1
./build/cpu_tri_bench
./build/cpu_tri_bench occluded
./build/cpu_mesh_bench flat
./build/cpu_mesh_bench textured
./build/cpu_mesh_bench occluded
```

Portable ISA: `-DHYPERLITE_MARCH=x86-64` (see [simd-tri-fill.md](simd-tri-fill.md)).

### Baseline (main, same session)

| Bench | total_ms | tris/s |
|-------|----------|--------|
| `cpu_tri_bench` | 352 | 3.41e6 |
| `cpu_tri_bench occluded` | 275 | 4.36e6 |
| `cpu_mesh_bench flat` | 199 | 5.91e6 |
| `cpu_mesh_bench textured` | 230 | 5.12e6 |
| `cpu_mesh_bench occluded` | 242 | 4.86e6 |

(Run-to-run variance on this VM is ±15–25%; compare only paired before/after from the same build.)

---

## 1. Hybrid opaque depth prepass — **reverted (loss)**

**Idea:** In high-overdraw tiles, pass 1 writes depth for opaque flat tris only; pass 2 color with depth test only (no depth write). Gate on tile bin count (`kDepthPrepassMinTrisPerTile`).

**Why it should help:** Cut color stores / texture work under heavy overlap.

**Why it lost:**

- Every gated tile pays **two half-space raster setups** per opaque flat tri. SIMD already depth-tests before color in one pass.
- Tile Hi-Z already skips whole tris when `tri_min > tile_max` (occluded benches). Prepass rarely adds reject after Hi-Z; it mostly doubles front-tris work.
- Low gate (8 tris/tile) triggered on **open** `cpu_tri_bench` (~44 tris/tile average) → ~1.4–1.9× slower. High gate (128) still regressed open and occluded.

| Bench | Baseline ms | Prepass ms (gate=128) | Δ |
|-------|-------------|------------------------|---|
| tri open | 352 | 425 | slower |
| tri occluded | 275 | 294 | slower |
| mesh flat | 199 | 209 | slower |
| mesh occluded | 242 | 234 | ~noise |

**Conclusion:** Do not ship. Hi-Z + single-pass SIMD fill is the better default.

---

## 2. 16-bit unorm depth plane — **reverted (loss)**

**Idea:** Store depth as `uint16` (GL `[0,1]` → unorm), compare in float registers after widen in SIMD blocks. Half depth bandwidth vs float32.

**Implementation notes:** `DepthBuffer` → `std::vector<uint16_t>`, `EncodeDepthUnorm` / `DecodeDepthUnorm`, AVX2 load/store helpers at block boundaries. Tests (`DepthAt`, depth ordering) still pass.

**Why it lost:**

- AVX2 fill is **8-wide float**; each block pays widen/narrow + pack on load/store. Memory savings are smaller than ALU cost at 1280×720 and current tri counts.
- `ScanTileMaxDepth` and Hi-Z scans remain scalar; less benefit than expected.

| Bench | Baseline ms | uint16 ms | Δ |
|-------|-------------|-----------|---|
| tri open | 352 | 417 | ~−18% throughput |
| tri occluded | 275 | 299 | slower |
| mesh flat | 199 | 184 | ~noise |
| mesh occluded | 242 | 268 | slower |

**Conclusion:** Keep float32 depth unless resolution or depth-heavy paths grow enough that bandwidth dominates (not true on these benches).

---

## 3. Finer Hi-Z (16×16 inside 64×64 tile) — **reverted (loss)**

**Idea:** Per-tile `subtile_max_depth[16]`; when bin count ≥ 128 **and** tile occluder exists, reject or clip raster to 16×16 cells whose `tri_min` is behind local max.

**Why it should help:** Partial occluders (tile max still far because of empty subcells) get finer reject than 64×64 `tile_max`.

**Why it lost:**

- Full-screen occluder benches already set `tile_max` and reject back tris coarsely; subcells add scans + multi-call raster overhead without skip wins.
- High-bin open tiles pay extra `ScanTileMaxDepth` on subcells after each write when tracking is enabled.

| Bench | Baseline ms | 16×16 Hi-Z ms | Δ |
|-------|-------------|---------------|---|
| tri open | 352 | 438 | slower |
| tri occluded | 275 | 292 | slower |
| mesh flat | 199 | 273 | slower |
| mesh occluded | 242 | 254 | slower |

**Conclusion:** Finer Hi-Z may matter for **partial** occluders (e.g. proc-world tiles with mixed terrain/buildings) but not for the standard occluded micro-benches; overhead dominates there.

---

## Summary

| Experiment | Open benches | Occluded benches | Ship? |
|------------|--------------|------------------|-------|
| Depth prepass (gated) | Regressed | Regressed / noise | **No** |
| 16-bit depth | Regressed | Regressed | **No** |
| 16×16 subtile Hi-Z | Regressed | Regressed | **No** |
| Tile AABB depth probe (pre-edge) | ~noise | ~noise | **No** |

**Keep:** float32 depth, single-pass raster, per-tile Hi-Z (tiles are **128×128** in current code), 8-wide SIMD fill.

**Next engine directions** (not tried here): SIMD/max-reduction in `ScanTileMaxDepth`; profile-guided gates tied to **estimated overlap** (tile bin count alone is a poor proxy).

---

## 4. Tile AABB depth probe before half-space setup — **reverted (no win)**

**Idea:** After tile Hi-Z vertex reject (`tri_min > tile_max`), many tris still enter `RasterScreenTriTile` when `tri_min <= tile_max` but every pixel in the tile clip fails depth. Defer `MakeHalfEdge` / SIMD constant setup until a cheap **z-only** probe on the tile-intersection AABB says the tri might write:

- Build screen-linear window depth plane from vertex `zw` (no `top_left` / half-space `C` beyond one anchor eval).
- If min depth at the four pixel-center corners of `[ix0,ix1)×[iy0,iy1)` is strictly behind `tile_max`, skip raster (linear z/w → corner min is exact min over the rectangle; triangle pixels in the clip are a subset → conservative).
- Also call the probe from the tile loop before `RasterScreenTriTile` to skip the function entirely when it fires.

**Why it should help:** Occluded / partial-overlap tris that pass vertex Hi-Z but fail every row/block depth test would skip edge + AVX2 setup.

**Why it did not win on standard benches:**

- `cpu_tri_bench occluded` / `occluded-2draw`: back-field tris are uniformly behind the fullscreen occluder → **`TriTileDepthReject` already skips them** before the probe runs. Remaining `RasterScreenTriTile` entries are occluder/near tris where the probe correctly does not fire.
- Open / flat / textured: `tile_occluder_max` stays at far plane → probe gated off (~zero overhead).
- When the probe runs but returns false (candidate tri may write), cost is one extra depth-plane eval — shows up as run-to-run noise only.

**Paired interleaved before/after** (this VM, Release, `-march=native`, OpenMP, `HYPERLITE_HEADLESS=1`, three rounds each):

| Bench | Before (main) | After (AABB probe) | Δ |
|-------|-------------|---------------------|---|
| `cpu_tri_bench` | **10.0e6** tris/s (~117 ms) | **9.9e6** tris/s (~122 ms) | ~noise |
| `cpu_tri_bench occluded` | **6.7e6** tris/s (~178 ms) | **6.5e6** tris/s (~184 ms) | ~noise |
| `cpu_tri_bench occluded-2draw` | **5.0e6** tris/s (~242 ms) | **5.0e6** tris/s (~241 ms) | ~noise |
| `cpu_mesh_bench flat` | **1.10e7** tris/s (~107 ms) | **1.11e7** tris/s (~106 ms) | ~noise |
| `cpu_mesh_bench textured` | **1.04e7** tris/s (~113 ms) | **1.04e7** tris/s (~113 ms) | ~noise |
| `cpu_mesh_bench occluded` | **7.3e6** tris/s (~161 ms) | **7.3e6** tris/s (~160 ms) | ~noise |
| `cpu_mesh_bench occluded-2draw` | **6.1e6** tris/s (~193 ms) | **6.1e6** tris/s (~192 ms) | ~noise |

**Losers also not retried:** depth prepass, 16-bit depth, 16×16 subtile Hi-Z, per-row half-plane reject before edges, edge-coef hoisting across tiles.

**Conclusion:** Do not ship. Leftover setup cost on micro-benches is mostly **binning + vertex Hi-Z**, not post-reject half-space setup. A future win may need a **different reject granularity** (e.g. partial occluders in proc-world-style tiles) or fewer bin entries — not this probe on the standard occluded paths.

## Tests

All experiments kept `ctest` green (including `HYPERLITE_MARCH=x86-64`) before revert. No public API changes.

```bash
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release \
  -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
cmake --build build-portable -j
ctest --test-dir build-portable --output-on-failure
```
