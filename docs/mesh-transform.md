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

Follow-up: dedicated SIMD / streaming clear for color + depth was tried and **not shipped** (~flat full-frame benches; nontemporal stores regressed heavily). See [simd-clear.md](simd-clear.md).

Indexed mesh: **5 041** unique verts vs **9 800** tris → old path ran ~**29 400** `MulViewProj` + full frustum clips per draw.

## What shipped

In `cpu_tri_raster_3d.hpp` (public API unchanged):

1. **Transform once per mesh vertex** — `TransformMeshPositions` writes clip-space + outcodes; trivial-in verts are **projected once**. Optional AVX2+FMA 8-wide AoS gather when `__AVX2__ && __FMA__` (native); scalar remainder / `HYPERLITE_MARCH=x86-64`.
2. **Outcode clip** — `ClipTriangleHomogeneous` trivial accept/reject; mesh emit uses accept → `TryAppendScreenTri` with pre-projected attrs, reject → skip, else Sutherland–Hodgman only.
3. **Cheaper binning** — nested `min`/`max` (no `initializer_list`); tile lists reused via thread-local scratch (`clear`, keep capacity). Still AABB → tile range only (no per-pixel work).
4. **Scratch reuse** — `MeshDrawScratch` (clip, outcodes, screen attrs, `ScreenTri` list, bins) is process-static across `DrawMesh` / `TickMesh` / immediate tris (not `thread_local`: TLS in a non-PIC static archive fails linking the Python `.so`). No OpenMP over triangles (depth races); OpenMP over **vertices** (transform, ≥512 verts, 64-vert chunks) and over **tiles** (fill).

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

### OpenMP vertex transform (follow-up)

After transform-once, emit remained ~43% of frame; parallelizing MVP+project over disjoint 64-vert chunks (gated ≥512 verts) speeds the transform slice without touching triangle emit/clip/bin/fill.

Interleaved 8 pairs vs pre-change `main` on this VM (Release headless, `HYPERLITE_MARCH=native`):

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_mesh_bench` flat | **11.07e6** | **11.45e6** | **~+3.4%** |
| `cpu_mesh_bench` textured | **10.23e6** | **10.44e6** | **~+2.0%** |
| `cpu_mesh_bench` occluded | **7.65e6** | **7.63e6** | **~flat** |
| `cpu_mesh_bench` occluded-2draw | **6.28e6** | **6.32e6** | **~flat** |
| `cpu_tri_bench` (immediate) | **10.31e6** | **10.31e6** | **~flat** |

Modest flat win; indexed emit loop still dominates emit. `ctest` green native + portable.

### Indexed emit fast path (follow-up)

After OpenMP vertex transform (#28), the **serial indexed emit loop** (index gather → outcode accept/reject → `TryAppendScreenTri` / rare Sutherland–Hodgman) still dominates emit on the 70×70 grid (~9800 tris/draw).

Shipped in `cpu_tri_raster_3d.hpp` (public API unchanged):

1. **`TryAppendFlatScreenTriFast` / `TryAppendTexturedScreenTriFast`** — trivial-accept path uses `emplace_back` + direct field writes (no zero-init `ScreenTri{}` copy, skips UV/atlas work on flat).
2. **`EmitIndexedFlatRange` / `EmitIndexedTexturedRange`** — dedicated indexed loops with outcode branching inlined, index/outcode/attr prefetch, no per-tri index bounds check (mesh load validates indices).
3. **`AppendFlatMeshTris` / `AppendTexturedMeshTris`** — call the range emitters instead of a lambda → `EmitIndexedClipTri` per triangle.

Interleaved 8 pairs vs post-#28 `main` on this VM (Release headless, `HYPERLITE_MARCH=native`, OpenMP transform + tile fill):

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_mesh_bench` flat | **11.04e6** | **11.29e6** | **~+2.3%** |
| `cpu_mesh_bench` textured | **10.19e6** | **10.14e6** | **~flat** |
| `cpu_mesh_bench` occluded | **7.54e6** | **7.53e6** | **~flat** |
| `cpu_mesh_bench` occluded-2draw | **6.14e6** | **6.18e6** | **~flat** |
| `cpu_tri_bench` (immediate) | **10.11e6** | **10.17e6** | **~flat** |
| `cpu_tri_bench` occluded | **7.11e6** | **7.20e6** | **~flat** |
| `cpu_tri_bench` occluded-2draw | **5.10e6** | **5.09e6** | **~flat** |

`ctest` green native + portable (`HYPERLITE_MARCH=x86-64`).

## Experiments

| Experiment | Result |
|------------|--------|
| Transform-once + project-once + outcode accept | **Shipped** — primary mesh win |
| AVX2+FMA 8-wide gather MVP | **Shipped** (gated); small vs scalar once-per-vert on this grid |
| Process-static scratch (screen + bins) | **Shipped** (not TLS — Python `.so` link) |
| Nested AABB min/max in bin | **Shipped** |
| OpenMP over triangles (fill) | **Not tried** (forbidden without per-tile ownership) |
| OpenMP over vertices (64-vert chunks, ≥512 verts) | **Shipped** — mesh flat ~+3.4%, textured ~+2% (interleaved 8 pairs); emit/index loop still serial |
| Indexed emit fast path (`emplace_back`, range loops, prefetch) | **Shipped** — mesh flat ~+2.3%; textured ~flat |
| SoA mesh vertex buffers (`clip_x/y/z/w` instead of `ClipVert` AoS) | **Dropped** — mesh flat ~−2.1% (interleaved 8 pairs); trivial-accept emit already skips clip gather. **Do not retry**. |
| OpenMP over indexed emit (thread-local `ScreenTri` lists + merge, ≥4096 tris) | **Dropped** — ~−2% mesh flat on 9800-tris grid; fork + list merge overhead exceeds parallel gain |
| SIMD batch outcode accept/reject (4–8 tris) | **Not tried** — serial fast path already near noise on headline benches |
| Rewrite fill / zmm / textured gather | **Out of scope** (already lost or forbidden) |
| CSR two-pass bin counts | **Dropped** — reused `vector` bins were enough; extra pass not needed |
| Skip `ClipVert` store for trivial-in verts (sparse clip / rematerialize on partial emit) | **Dropped** — mesh flat ~flat (−0.4%); no ≥2% win; **do not retry** |

## Skip trivial-in `ClipVert` stores (post-#29) {#skip-trivial-in-clipvert-stores}

**Hypothesis:** after #29, trivial-accept indexed emit never reads `clip_verts` (only outcodes + pre-projected `px/py/zw/iw`). `TransformMeshPositions` still wrote a full AoS `ClipVert` per vertex every draw — wasted on the 70×70 grid where almost all verts are trivial-in. Skip clip-space retention for `outcode == 0`; partial-clip emit rematerializes clip from object position × MVP.

**Tried:** remove `MeshDrawScratch::clip_verts`; transform writes outcodes + screen attrs only; `EmitIndexedClipTri` rematerializes three corners via `MulViewProj` on the rare Sutherland–Hodgman path. Screen/outcode SoA layout unchanged.

### Paired benches (this VM, interleaved 8 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP. Baseline: `main` at `1905fa8` (post-#29 indexed emit fast path).

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_mesh_bench` flat | **12.80e6** | **12.75e6** | **~−0.4%** |
| `cpu_mesh_bench` textured | **10.43e6** | **10.17e6** | **~−2.5%** |
| `cpu_mesh_bench` occluded | **8.44e6** | **8.24e6** | **~−2.4%** |
| `cpu_mesh_bench` occluded-2draw | **6.83e6** | **6.71e6** | **~−1.8%** |
| `cpu_tri_bench` (immediate) | **10.05e6** | **10.39e6** | **~+3.4%** |
| `cpu_tri_bench` occluded | **7.30e6** | **7.27e6** | **~−0.4%** |
| `cpu_tri_bench` occluded-2draw | **5.22e6** | **5.14e6** | **~−1.5%** |

`ctest` green native + portable (`HYPERLITE_MARCH=x86-64`).

**Outcome: not shipped.** Primary mesh flat within noise; saves clip writes but does not move headline benches. Engine reverted; **do not retry** skipping `ClipVert` stores for trivial-in verts.


**Hypothesis:** fat `ScreenTri` (~18 floats + atlas pointer + dims, ~96B) copied by value into `RasterScreenTriTile` on every tile visit; flat emit skips UV writes but `emplace_back` still value-inits the whole struct.

**Tried (in order):**

1. **`const ScreenTri&` + `ScreenTriFillGeom`** — stop by-value copy; build CCW-corrected local geometry once per tile call (winding swap on locals only).
2. **Compact layout** — geometry block first (`xy/zw` per vertex, then `color` + atlas tag); flat fast emit drops `iw` parameters; `BuildScreenTriFillGeom` skips reading `iw`/UV when `atlas_rgba == null`.
3. **Cached signed area at emit** — not pursued; steps (1)+(2) did not show a stable win.

Fill SIMD kernels and binning unchanged.

### Paired benches (this VM, interleaved 8 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP. Baseline: parent `main` at `a4b50bc` (post-#29).

**Variant A — steps (1)+(2) combined** (three 8-pair runs; high variance):

| Bench | Δ run 1 | Δ run 2 | Δ run 3 |
|-------|---------|---------|---------|
| `cpu_tri_bench` | **+0.7%** | **+2.5%** | **+4.2%** |
| `cpu_tri_bench occluded` | **+6.4%** | **+3.7%** | **+8.4%** |
| `cpu_tri_bench occluded-2draw` | **+19.7%** | **+13.7%** | **+6.6%** |
| `cpu_mesh_bench flat` | **+0.5%** | **−15.1%** | **+8.6%** |
| `cpu_mesh_bench textured` | **−8.3%** | **−3.4%** | **−1.7%** |
| `cpu_mesh_bench occluded` | **−13.7%** | **+13.3%** | **−9.2%** |
| `cpu_mesh_bench occluded-2draw` | **−17.0%** | **+8.7%** | **+14.0%** |

**Variant B — step (1) only** (`const ScreenTri&` + `ScreenTriFillGeom`, original interleaved layout):

| Bench | Δ |
|-------|---|
| `cpu_tri_bench` | **~flat (+0.1%)** |
| `cpu_tri_bench occluded` | **+33.8%** (outlier / noise) |
| `cpu_tri_bench occluded-2draw` | **−6.0%** |
| `cpu_mesh_bench flat` | **+0.5%** |
| `cpu_mesh_bench textured` | **+5.6%** |
| `cpu_mesh_bench occluded` | **+0.7%** |
| `cpu_mesh_bench occluded-2draw` | **−12.7%** |

**Outcome: not shipped.** Swings exceed prior emit-noise band; time remains in bin + `RasterScreenTrisTiled` + fill inner loops, not struct memcpy. Engine reverted; **do not retry** unless profiling shows tile-fill parameter copies in hot samples.

| Experiment | Result |
|------------|--------|
| `RasterScreenTriTile` `const ScreenTri&` + local fill geom | **Noise** — see tables above |
| Geometry-first `ScreenTri` + flat emit without `iw` | **Noise** — no consistent mesh win |
| Cached `area2` at emit | **Not tried** (no signal from 1–2) |

## SoA mesh vertex buffers (post-#29) {#soa-mesh-vertex-buffers-post-29}

**Hypothesis:** after indexed emit fast path (#29), the serial emit loop still dominates; storing transformed mesh vertices as **SoA** (`clip_x/y/z/w` + existing `px/py/zw/iw` + `outcodes`) instead of `ClipVert` AoS would make indexed emit use sequential component loads instead of 3 AoS gathers per triangle on the rare clip path, and let AVX2 transform store clip components directly.

**Tried:** `MeshDrawScratch` replaced `clip_verts` with `clip_x/y/z/w`; `TransformMeshPositionsRange` writes SoA (AVX2 stores directly to component arrays); `EmitIndexedFlatRange` / `EmitIndexedTexturedRange` / `EmitIndexedClipTri` read SoA via `GatherClipVert` on partial-clip tris only. Scalar portable path uses `ComputeClipOutcodeSoA` / `ProjectToPixelsSoA`. Public API unchanged.

### Paired benches (this VM, interleaved 8 pairs)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, OpenMP. Baseline: `main` @ `1905fa8` (post-#29).

| Bench | Before (tris/s) | After (tris/s) | Δ |
|-------|-----------------|----------------|---|
| `cpu_mesh_bench` flat | **12.71e6** | **12.44e6** | **~−2.1%** |
| `cpu_mesh_bench` textured | **10.02e6** | **10.05e6** | **~flat** |
| `cpu_mesh_bench` occluded | **8.13e6** | **8.14e6** | **~flat** |
| `cpu_mesh_bench` occluded-2draw | **6.57e6** | **6.56e6** | **~flat** |
| `cpu_tri_bench` (immediate) | **9.98e6** | **10.06e6** | **~flat** |
| `cpu_tri_bench` occluded | **7.10e6** | **7.14e6** | **~flat** |
| `cpu_tri_bench` occluded-2draw | **5.08e6** | **5.07e6** | **~flat** |

**Outcome: not shipped.** Primary mesh flat regressed ~2% (outside noise, below the ~+2% win bar). Trivial-accept indexed emit already avoided clip_verts; extra SoA arrays add memory traffic without helping the hot path. Engine reverted; **do not retry** SoA clip layout unless profiling shows AoS gather in emit samples.

| Experiment | Result |
|------------|--------|
| SoA `clip_x/y/z/w` in `MeshDrawScratch` + transform/emit readers | **Loss** on mesh flat (~−2.1%); other benches ~flat |

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
