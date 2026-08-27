# Depth prepass & depth-bandwidth experiments (Layer 1 CPU fill)

Investigation of three engine-side changes aimed at **overdraw** and **depth bandwidth** after tile Hi-Z and SIMD opaque fill landed. None improved the standard benches on this VM; all were reverted. Public Python API unchanged.

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

**Keep:** float32 depth, single-pass raster, 64×64 tile Hi-Z, 8-wide SIMD fill.

**Next engine directions** (not tried here): reduce **half-space setup** cost for tris that fail depth in the first few rows; SIMD/max-reduction in `ScanTileMaxDepth`; profile-guided gates tied to **estimated overlap** (tile bin count alone is a poor proxy).

## Tests

All experiments kept `ctest` green (including `HYPERLITE_MARCH=x86-64`) before revert. No public API changes.

```bash
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release \
  -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
cmake --build build-portable -j
ctest --test-dir build-portable --output-on-failure
```
