"""Procedural continent — flyable game-shaped CPU stress bench for Hyperlite.

Chunked retained terrain meshes (unique topology per chunk), textured height bands,
instanced trees/rocks/cube-city props, frustum culling, and a font-free HUD.

Presets:
  smoke  — tiny world, CI-safe headless (~3 s)
  play   — default 1280×720 windowed flyover
  limit  — dense meshes + forest + cube city (headless stress)

Controls (windowed):
  WASD move, mouse-look (click or Enter to capture), Space/Ctrl up/down, Esc quit.
"""

from __future__ import annotations

import argparse
import math
import sys
import time
from dataclasses import dataclass
from typing import Iterable

import hyperlite
import numpy as np

# Virtual-key codes shared by Win32 and the X11 mapper.
VK_CONTROL = 0x11
VK_SPACE = 0x20

# Height bands (world units) — tuned for fBm output scale.
WATER_LEVEL = 2.0
SAND_TOP = 4.5
GRASS_TOP = 18.0
ROCK_TOP = 42.0
SNOW_LINE = 38.0


@dataclass(frozen=True)
class PresetConfig:
	"""One runnable world/bench configuration."""

	name: str
	width: int
	height: int
	world_chunks: int
	grid_verts: int
	chunk_size: float
	draw_radius: int
	tree_count: int
	rock_count: int
	city_size: int
	forest_density: float
	default_seconds: float | None
	ridge_lines: bool
	headless: bool


PRESETS: dict[str, PresetConfig] = {
	"smoke": PresetConfig(
		name="smoke",
		width=320,
		height=180,
		world_chunks=4,
		grid_verts=17,
		chunk_size=32.0,
		draw_radius=2,
		tree_count=12,
		rock_count=6,
		city_size=0,
		forest_density=0.15,
		default_seconds=3.0,
		ridge_lines=False,
		headless=True,
	),
	"play": PresetConfig(
		name="play",
		width=1280,
		height=720,
		world_chunks=24,
		grid_verts=33,
		chunk_size=32.0,
		draw_radius=8,
		tree_count=900,
		rock_count=450,
		city_size=22,
		forest_density=0.38,
		default_seconds=None,
		ridge_lines=True,
		headless=False,
	),
	"limit": PresetConfig(
		name="limit",
		width=1280,
		height=720,
		world_chunks=28,
		grid_verts=41,
		chunk_size=32.0,
		draw_radius=13,
		tree_count=3600,
		rock_count=1400,
		city_size=46,
		forest_density=0.58,
		default_seconds=8.0,
		ridge_lines=True,
		headless=True,
	),
}


@dataclass
class ChunkRecord:
	"""One terrain chunk uploaded to the engine."""

	mesh_id: int
	cx: int
	cz: int
	center: np.ndarray
	aabb_min: np.ndarray
	aabb_max: np.ndarray
	tri_count: int
	model: np.ndarray


@dataclass
class PropInstance:
	"""One instanced prop draw."""

	mesh_id: int
	model: np.ndarray
	kind: str
	tri_count: int


@dataclass
class FrameStats:
	"""Per-frame draw counters."""

	chunks_drawn: int = 0
	tris_submitted: int = 0
	props_drawn: int = 0


class NoiseField:
	"""Deterministic value-noise heightfield with fBm stacking."""

	def __init__(self, seed: int) -> None:
		"""Build permutation table from seed."""
		self.seed = seed & 0xFFFFFFFF
		rng = np.random.default_rng(seed)
		self.perm = np.arange(256, dtype=np.int32)
		rng.shuffle(self.perm)
		self.perm = np.concatenate([self.perm, self.perm])

	def _hash2(self, ix: int, iz: int) -> float:
		"""Lattice hash in [0, 1)."""
		n = (ix * 374761393 + iz * 668265263 + self.seed) & 0xFFFFFFFF
		n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
		return float(n & 0xFFFF) / 65535.0

	def _smooth(self, t: float) -> float:
		"""Smoothstep for interpolation."""
		return t * t * (3.0 - 2.0 * t)

	def value_noise(self, x: float, z: float) -> float:
		"""Bilinear value noise sample."""
		xf = math.floor(x)
		zf = math.floor(z)
		tx = self._smooth(x - xf)
		tz = self._smooth(z - zf)
		ix = int(xf)
		iz = int(zf)
		v00 = self._hash2(ix, iz)
		v10 = self._hash2(ix + 1, iz)
		v01 = self._hash2(ix, iz + 1)
		v11 = self._hash2(ix + 1, iz + 1)
		a = v00 + (v10 - v00) * tx
		b = v01 + (v11 - v01) * tx
		return a + (b - a) * tz

	def fbm(self, x: float, z: float, octaves: int = 5) -> float:
		"""Fractal Brownian motion height sample."""
		amp = 1.0
		freq = 1.0 / 180.0
		total = 0.0
		norm = 0.0
		for _ in range(octaves):
			total += self.value_noise(x * freq, z * freq) * amp
			norm += amp
			amp *= 0.5
			freq *= 2.05
		base = total / max(norm, 1e-6)
		# Macro valleys + sharp ridges.
		ridge = 1.0 - abs(self.value_noise(x * 0.004, z * 0.004) * 2.0 - 1.0)
		return base * 52.0 + ridge * ridge * 28.0 - 8.0

	def height(self, x: float, z: float) -> float:
		"""Public height query."""
		return self.fbm(x, z)


def mat4_identity() -> np.ndarray:
	"""Column-major 4×4 identity."""
	m = np.zeros(16, dtype=np.float32)
	m[0] = m[5] = m[10] = m[15] = 1.0
	return m


def mat4_translation(x: float, y: float, z: float) -> np.ndarray:
	"""Column-major translation matrix."""
	m = mat4_identity()
	m[12] = x
	m[13] = y
	m[14] = z
	return m


def mat4_scale(sx: float, sy: float, sz: float) -> np.ndarray:
	"""Column-major scale matrix."""
	m = mat4_identity()
	m[0] = sx
	m[5] = sy
	m[10] = sz
	return m


def mat4_rotation_y(radians: float) -> np.ndarray:
	"""Column-major rotation about +Y."""
	c = math.cos(radians)
	s = math.sin(radians)
	m = mat4_identity()
	m[0] = c
	m[2] = s
	m[8] = -s
	m[10] = c
	return m


def mat4_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
	"""Multiply column-major 4×4 matrices: out = a × b."""
	out = np.zeros(16, dtype=np.float32)
	for col in range(4):
		for row in range(4):
			out[col * 4 + row] = (
				a[0 * 4 + row] * b[col * 4 + 0]
				+ a[1 * 4 + row] * b[col * 4 + 1]
				+ a[2 * 4 + row] * b[col * 4 + 2]
				+ a[3 * 4 + row] * b[col * 4 + 3]
			)
	return out


def mat4_perspective(fovy_rad: float, aspect: float, znear: float, zfar: float) -> np.ndarray:
	"""OpenGL-style column-major perspective (-Z forward)."""
	m = np.zeros(16, dtype=np.float32)
	f = 1.0 / math.tan(fovy_rad * 0.5)
	m[0] = f / aspect
	m[5] = f
	m[10] = (zfar + znear) / (znear - zfar)
	m[11] = -1.0
	m[14] = (2.0 * zfar * znear) / (znear - zfar)
	return m


def mat4_look_at(eye: np.ndarray, target: np.ndarray, up: np.ndarray) -> np.ndarray:
	"""Column-major view matrix."""
	f = target - eye
	f = f / np.linalg.norm(f)
	r = np.cross(f, up)
	rn = np.linalg.norm(r)
	if rn < 1e-8:
		r = np.array([1.0, 0.0, 0.0], dtype=np.float64)
	else:
		r = r / rn
	u = np.cross(r, f)
	m = np.zeros(16, dtype=np.float32)
	m[0] = r[0]
	m[4] = r[1]
	m[8] = r[2]
	m[1] = u[0]
	m[5] = u[1]
	m[9] = u[2]
	m[2] = -f[0]
	m[6] = -f[1]
	m[10] = -f[2]
	m[12] = -np.dot(r, eye)
	m[13] = -np.dot(u, eye)
	m[14] = np.dot(f, eye)
	m[15] = 1.0
	return m


def height_to_uv_v(h: float) -> float:
	"""Map world height to atlas V band center (sand/grass/rock/snow stripes)."""
	if h < WATER_LEVEL + 0.5:
		return 0.125
	if h < SAND_TOP:
		return 0.375
	if h < GRASS_TOP:
		return 0.625
	if h < SNOW_LINE:
		return 0.875
	return 0.95


def build_terrain_atlas(width: int = 256, height: int = 64) -> np.ndarray:
	"""Procedural RGBA8 atlas: sand, grass, rock, snow horizontal bands."""
	rgba = np.zeros((height, width, 4), dtype=np.uint8)
	bands = (
		(180, 165, 110),
		(70, 140, 65),
		(110, 105, 100),
		(230, 235, 245),
	)
	band_h = height // 4
	for band_idx, rgb in enumerate(bands):
		y0 = band_idx * band_h
		y1 = height if band_idx == 3 else (band_idx + 1) * band_h
		for y in range(y0, y1):
			for x in range(width):
				n = (
					math.sin(x * 0.31 + band_idx * 1.7) * 0.5
					+ math.sin(y * 0.47 + x * 0.09) * 0.5
				)
				shade = 0.85 + 0.15 * n
				rgba[y, x, 0] = int(rgb[0] * shade)
				rgba[y, x, 1] = int(rgb[1] * shade)
				rgba[y, x, 2] = int(rgb[2] * shade)
				rgba[y, x, 3] = 255
	return rgba.reshape(-1)


def build_chunk_mesh(
	noise: NoiseField,
	cx: int,
	cz: int,
	grid_verts: int,
	chunk_size: float,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, int]:
	"""Build verts/indices and AABB for one terrain chunk."""
	quads = grid_verts - 1
	cell = chunk_size / float(quads)
	origin_x = cx * chunk_size
	origin_z = cz * chunk_size
	vert_count = grid_verts * grid_verts
	verts = np.zeros(vert_count * 6, dtype=np.float32)
	heights = np.zeros((grid_verts, grid_verts), dtype=np.float32)
	idx = 0
	for iz in range(grid_verts):
		for ix in range(grid_verts):
			wx = origin_x + ix * cell
			wz = origin_z + iz * cell
			h = noise.height(wx, wz)
			heights[iz, ix] = h
			u = (wx * 0.02) % 1.0
			v = height_to_uv_v(h)
			verts[idx + 0] = wx
			verts[idx + 1] = h
			verts[idx + 2] = wz
			verts[idx + 3] = u
			verts[idx + 4] = v
			verts[idx + 5] = 0.0
			idx += 6
	indices = []
	for iz in range(quads):
		for ix in range(quads):
			i00 = iz * grid_verts + ix
			i10 = i00 + 1
			i01 = i00 + grid_verts
			i11 = i01 + 1
			indices.extend((i00, i10, i11, i00, i11, i01))
	indices_arr = np.asarray(indices, dtype=np.uint32)
	aabb_min = np.array([origin_x, float(heights.min()), origin_z], dtype=np.float32)
	aabb_max = np.array(
		[origin_x + chunk_size, float(heights.max()), origin_z + chunk_size],
		dtype=np.float32,
	)
	tri_count = quads * quads * 2
	return verts, indices_arr, aabb_min, aabb_max, tri_count


def build_water_mesh(extent: float, water_y: float) -> tuple[np.ndarray, np.ndarray, int]:
	"""Large translucent water plane covering the continent."""
	verts = np.array(
		[
			0.0, water_y, 0.0, 0.0, 0.0, 0.0,
			extent, water_y, 0.0, 1.0, 0.0, 0.0,
			extent, water_y, extent, 1.0, 1.0, 0.0,
			0.0, water_y, extent, 0.0, 1.0, 0.0,
		],
		dtype=np.float32,
	)
	indices = np.array([0, 1, 2, 0, 2, 3], dtype=np.uint32)
	return verts, indices, 2


def _append_vertex(
	verts: list[float],
	x: float,
	y: float,
	z: float,
	u: float = 0.0,
	v: float = 0.0,
) -> int:
	"""Append one 6-float vertex; return its index."""
	index = len(verts) // 6
	verts.extend((x, y, z, u, v, 0.0))
	return index


def build_tree_mesh() -> tuple[np.ndarray, np.ndarray, int]:
	"""Simple trunk + cone tree (low poly)."""
	verts: list[float] = []
	indices: list[int] = []
	# Trunk box (8 verts, 12 tris).
	trunk_w = 0.35
	trunk_h = 1.2
	tx0 = -trunk_w
	tx1 = trunk_w
	tz0 = -trunk_w
	tz1 = trunk_w
	trunk_indices = []
	for y in (0.0, trunk_h):
		for x, z in ((tx0, tz0), (tx1, tz0), (tx1, tz1), (tx0, tz1)):
			trunk_indices.append(_append_vertex(verts, x, y, z))
	# Cube faces (CCW).
	box = trunk_indices
	for a, b, c, d in (
		(0, 1, 2, 3),
		(4, 7, 6, 5),
		(0, 4, 5, 1),
		(1, 5, 6, 2),
		(2, 6, 7, 3),
		(3, 7, 4, 0),
	):
		indices.extend((box[a], box[b], box[c], box[a], box[c], box[d]))
	# Foliage cone.
	center = _append_vertex(verts, 0.0, trunk_h + 2.2, 0.0)
	sides = 8
	radius = 1.4
	base_y = trunk_h + 0.2
	ring: list[int] = []
	for i in range(sides):
		ang = (i / sides) * math.tau
		ring.append(_append_vertex(verts, math.cos(ang) * radius, base_y, math.sin(ang) * radius))
	for i in range(sides):
		nxt = (i + 1) % sides
		indices.extend((center, ring[i], ring[nxt]))
	return np.asarray(verts, dtype=np.float32), np.asarray(indices, dtype=np.uint32), len(indices) // 3


def build_rock_mesh() -> tuple[np.ndarray, np.ndarray, int]:
	"""Irregular rock — unit cube with perturbed vertices."""
	verts: list[float] = []
	indices: list[int] = []
	# Unit cube corners with slight jitter (deterministic).
	jitter = (
		(-0.5, 0.0, -0.5),
		(0.5, 0.05, -0.45),
		(0.48, -0.04, 0.52),
		(-0.48, 0.03, 0.5),
		(-0.46, 0.55, -0.48),
		(0.52, 0.58, -0.5),
		(0.5, 0.52, 0.48),
		(-0.52, 0.54, 0.46),
	)
	corner_idx = [_append_vertex(verts, *p) for p in jitter]
	faces = (
		(0, 1, 2, 3),
		(4, 7, 6, 5),
		(0, 4, 5, 1),
		(1, 5, 6, 2),
		(2, 6, 7, 3),
		(3, 7, 4, 0),
	)
	for a, b, c, d in faces:
		indices.extend((corner_idx[a], corner_idx[b], corner_idx[c], corner_idx[a], corner_idx[c], corner_idx[d]))
	return np.asarray(verts, dtype=np.float32), np.asarray(indices, dtype=np.uint32), len(indices) // 3


def build_cube_mesh() -> tuple[np.ndarray, np.ndarray, int]:
	"""Unit cube centered at origin (city blocks)."""
	verts: list[float] = []
	indices: list[int] = []
	h = 0.5
	corners = (
		(-h, -h, -h),
		(h, -h, -h),
		(h, -h, h),
		(-h, -h, h),
		(-h, h, -h),
		(h, h, -h),
		(h, h, h),
		(-h, h, h),
	)
	corner_idx = [_append_vertex(verts, *p) for p in corners]
	faces = (
		(0, 1, 2, 3),
		(4, 7, 6, 5),
		(0, 4, 5, 1),
		(1, 5, 6, 2),
		(2, 6, 7, 3),
		(3, 7, 4, 0),
	)
	for a, b, c, d in faces:
		indices.extend((corner_idx[a], corner_idx[b], corner_idx[c], corner_idx[a], corner_idx[c], corner_idx[d]))
	return np.asarray(verts, dtype=np.float32), np.asarray(indices, dtype=np.uint32), len(indices) // 3


def mat4_row(m: np.ndarray, row: int) -> np.ndarray:
	"""Fetch one row from a column-major 4×4 matrix."""
	return np.array([m[0 * 4 + row], m[1 * 4 + row], m[2 * 4 + row], m[3 * 4 + row]], dtype=np.float64)


def extract_frustum_planes(view_proj: np.ndarray) -> list[np.ndarray]:
	"""Six normalized frustum planes from column-major view-proj."""
	rows = [mat4_row(view_proj, r) for r in range(4)]
	combos = (
		rows[3] + rows[0],
		rows[3] - rows[0],
		rows[3] + rows[1],
		rows[3] - rows[1],
		rows[3] + rows[2],
		rows[3] - rows[2],
	)
	planes: list[np.ndarray] = []
	for p in combos:
		length = math.sqrt(float(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]))
		if length > 1e-8:
			planes.append((p / length).astype(np.float32))
		else:
			planes.append(p.astype(np.float32))
	return planes


def aabb_visible(aabb_min: np.ndarray, aabb_max: np.ndarray, planes: list[np.ndarray]) -> bool:
	"""Conservative AABB vs frustum test."""
	for plane in planes:
		px = aabb_max[0] if plane[0] >= 0 else aabb_min[0]
		py = aabb_max[1] if plane[1] >= 0 else aabb_min[1]
		pz = aabb_max[2] if plane[2] >= 0 else aabb_min[2]
		if plane[0] * px + plane[1] * py + plane[2] * pz + plane[3] < 0.0:
			return False
	return True


def model_from_transform(
	x: float,
	y: float,
	z: float,
	yaw: float = 0.0,
	sx: float = 1.0,
	sy: float = 1.0,
	sz: float = 1.0,
) -> np.ndarray:
	"""Build model matrix: T × R × S."""
	s = mat4_scale(sx, sy, sz)
	r = mat4_rotation_y(yaw)
	t = mat4_translation(x, y, z)
	return mat4_mul(t, mat4_mul(r, s))


class ProcWorld:
	"""Procedural continent state: meshes, props, and draw helpers."""

	def __init__(self, cfg: PresetConfig, seed: int, engine: hyperlite.Engine) -> None:
		"""Generate bounded world at startup."""
		self.cfg = cfg
		self.seed = seed
		self.noise = NoiseField(seed)
		self.extent = cfg.world_chunks * cfg.chunk_size
		self.chunks: list[ChunkRecord] = []
		self.props: list[PropInstance] = []
		self.water_mesh = -1
		self.water_tris = 0
		self.tree_mesh = -1
		self.tree_tris = 0
		self.rock_mesh = -1
		self.rock_tris = 0
		self.cube_mesh = -1
		self.cube_tris = 0
		self.atlas_id = -1
		self.ridge_segments = np.zeros(0, dtype=np.float32)
		self._generate(engine)

	def _generate(self, engine: hyperlite.Engine) -> None:
		"""Upload all retained meshes and scatter props."""
		t0 = time.perf_counter()
		atlas_rgba = build_terrain_atlas()
		self.atlas_id = engine.load_atlas(atlas_rgba, 256, 64)

		for cz in range(self.cfg.world_chunks):
			for cx in range(self.cfg.world_chunks):
				verts, indices, aabb_min, aabb_max, tri_count = build_chunk_mesh(
					self.noise,
					cx,
					cz,
					self.cfg.grid_verts,
					self.cfg.chunk_size,
				)
				mesh_id = engine.load_mesh(verts, indices)
				center = (aabb_min + aabb_max) * 0.5
				model = mat4_identity()
				self.chunks.append(
					ChunkRecord(
						mesh_id=mesh_id,
						cx=cx,
						cz=cz,
						center=center,
						aabb_min=aabb_min,
						aabb_max=aabb_max,
						tri_count=tri_count,
						model=model,
					)
				)

		wv, wi, self.water_tris = build_water_mesh(self.extent, WATER_LEVEL)
		self.water_mesh = engine.load_mesh(wv, wi)

		tv, ti, self.tree_tris = build_tree_mesh()
		self.tree_mesh = engine.load_mesh(tv, ti)
		rv, ri, self.rock_tris = build_rock_mesh()
		self.rock_mesh = engine.load_mesh(rv, ri)
		cv, ci, self.cube_tris = build_cube_mesh()
		self.cube_mesh = engine.load_mesh(cv, ci)

		self._scatter_props()
		if self.cfg.ridge_lines:
			self._build_ridge_lines()

		elapsed = time.perf_counter() - t0
		total_tris = sum(c.tri_count for c in self.chunks) + self.water_tris
		print(
			f"proc_world generated preset={self.cfg.name} seed={self.seed} "
			f"chunks={len(self.chunks)} props={len(self.props)} "
			f"terrain_tris={total_tris} gen_s={elapsed:.2f}"
		)

	def _city_origin(self) -> tuple[float, float]:
		"""Place cube city near world center on flatter ground."""
		cx = self.cfg.world_chunks // 2
		cz = self.cfg.world_chunks // 2
		ox = cx * self.cfg.chunk_size + self.cfg.chunk_size * 0.5
		oz = cz * self.cfg.chunk_size + self.cfg.chunk_size * 0.5
		return ox, oz

	def _scatter_props(self) -> None:
		"""Place trees, rocks, and optional cube city."""
		rng = np.random.default_rng(self.seed + 991)
		city_size = self.cfg.city_size
		city_ox, city_oz = self._city_origin()
		city_half = city_size * 2.2

		for _ in range(self.cfg.tree_count):
			x = rng.uniform(0.0, self.extent)
			z = rng.uniform(0.0, self.extent)
			if city_size > 0 and abs(x - city_ox) < city_half and abs(z - city_oz) < city_half:
				continue
			h = self.noise.height(x, z)
			if h < SAND_TOP or h > ROCK_TOP:
				continue
			if rng.random() > self.cfg.forest_density:
				continue
			yaw = rng.uniform(0.0, math.tau)
			scale = rng.uniform(0.8, 1.35)
			model = model_from_transform(x, h, z, yaw=yaw, sx=scale, sy=scale, sz=scale)
			self.props.append(
				PropInstance(self.tree_mesh, model, "tree", self.tree_tris)
			)

		for _ in range(self.cfg.rock_count):
			x = rng.uniform(0.0, self.extent)
			z = rng.uniform(0.0, self.extent)
			h = self.noise.height(x, z)
			if h < GRASS_TOP * 0.6:
				continue
			yaw = rng.uniform(0.0, math.tau)
			scale = rng.uniform(0.5, 1.8)
			model = model_from_transform(x, h + 0.2 * scale, z, yaw=yaw, sx=scale, sy=scale * 0.7, sz=scale)
			self.props.append(
				PropInstance(self.rock_mesh, model, "rock", self.rock_tris)
			)

		if city_size > 0:
			for gx in range(city_size):
				for gz in range(city_size):
					x = city_ox + (gx - city_size * 0.5) * 3.6
					z = city_oz + (gz - city_size * 0.5) * 3.6
					h = self.noise.height(x, z)
					height = rng.uniform(4.0, 22.0)
					width = rng.uniform(0.9, 1.6)
					# Slight color variation via kind tag only; draw uses flat tints.
					model = model_from_transform(x, h, z, sx=width, sy=height, sz=width)
					self.props.append(
						PropInstance(self.cube_mesh, model, "city", self.cube_tris)
					)

	def _build_ridge_lines(self) -> None:
		"""Sparse far-mountain contour segments (cheap)."""
		segs: list[float] = []
		step = 48.0
		contour_h = 36.0
		for z in np.arange(0.0, self.extent, step):
			for x in np.arange(0.0, self.extent, step):
				h = self.noise.height(x, z)
				if h < contour_h:
					continue
				x2 = min(x + step, self.extent)
				h2 = self.noise.height(x2, z)
				y0 = (h + h2) * 0.5
				segs.extend((x, y0, z, x2, y0, z))
		self.ridge_segments = np.asarray(segs, dtype=np.float32)

	def draw(
		self,
		engine: hyperlite.Engine,
		view_proj: np.ndarray,
		camera_pos: np.ndarray,
	) -> FrameStats:
		"""Submit culled terrain, water, props, optional ridge lines."""
		stats = FrameStats()
		planes = extract_frustum_planes(view_proj)
		draw_dist = self.cfg.draw_radius * self.cfg.chunk_size
		draw_dist_sq = draw_dist * draw_dist

		for chunk in self.chunks:
			dx = float(chunk.center[0] - camera_pos[0])
			dz = float(chunk.center[2] - camera_pos[2])
			if dx * dx + dz * dz > draw_dist_sq:
				continue
			if not aabb_visible(chunk.aabb_min, chunk.aabb_max, planes):
				continue
			engine.draw_mesh_textured(chunk.mesh_id, chunk.model, self.atlas_id)
			stats.chunks_drawn += 1
			stats.tris_submitted += chunk.tri_count

		# Water (translucent — no depth write path).
		water_min = np.array([0.0, WATER_LEVEL - 0.5, 0.0], dtype=np.float32)
		water_max = np.array([self.extent, WATER_LEVEL + 0.5, self.extent], dtype=np.float32)
		if aabb_visible(water_min, water_max, planes):
			engine.draw_mesh(
				self.water_mesh,
				mat4_identity(),
				35,
				110,
				170,
				140,
			)
			stats.tris_submitted += self.water_tris

		for prop in self.props:
			px = float(prop.model[12])
			pz = float(prop.model[14])
			dx = px - camera_pos[0]
			dz = pz - camera_pos[2]
			if dx * dx + dz * dz > draw_dist_sq:
				continue
			if prop.kind == "tree":
				engine.draw_mesh(prop.mesh_id, prop.model, 30, 110, 45, 255)
			elif prop.kind == "rock":
				engine.draw_mesh(prop.mesh_id, prop.model, 95, 92, 88, 255)
			else:
				# Cube city — warm window tones.
				engine.draw_mesh(prop.mesh_id, prop.model, 180, 150, 90, 255)
			stats.props_drawn += 1
			stats.tris_submitted += prop.tri_count

		if self.ridge_segments.size > 0:
			engine.lines_3d(self.ridge_segments, 160, 175, 195, 90, 1)

		return stats


def _draw_hud(engine: hyperlite.Engine, width: int, height: int, fps: float, stats: FrameStats, preset: str) -> None:
	"""Font-free HUD: colored bars for fps / chunks / tris / props."""
	x0 = 12
	y0 = 12
	bar_w = 140
	bar_h = 10
	gap = 18

	def bar(y: int, frac: float, r: int, g: int, b: int) -> None:
		frac_clamped = max(0.0, min(1.0, frac))
		engine.rect_fill(x0, y, bar_w, bar_h, 30, 34, 42, 220)
		engine.rect_fill(x0, y, int(bar_w * frac_clamped), bar_h, r, g, b, 255)
		engine.rect_outline(x0, y, bar_w, bar_h, 80, 90, 110, 255)

	bar(y0, fps / 60.0, 80, 220, 120)
	max_chunks = max(1, stats.chunks_drawn + 4)
	bar(y0 + gap, stats.chunks_drawn / max_chunks, 80, 180, 255)
	bar(y0 + gap * 2, min(1.0, stats.tris_submitted / 2_000_000.0), 255, 180, 60)
	bar(y0 + gap * 3, min(1.0, stats.props_drawn / 4000.0), 200, 120, 255)

	# Preset stripe.
	engine.rect_fill(0, height - 5, width, 5, 60, 120, 200, 255 if preset == "play" else 200)


def _autopilot_camera(t: float, world: ProcWorld) -> tuple[np.ndarray, np.ndarray, float, float]:
	"""Deterministic figure-eight fly path for headless benches."""
	extent = world.extent
	cx = extent * 0.5
	cz = extent * 0.5
	radius = extent * 0.38
	x = cx + math.sin(t * 0.45) * radius
	z = cz + math.sin(t * 0.9) * radius * 0.55
	h = world.noise.height(x, z)
	y = h + 28.0 + 8.0 * math.sin(t * 0.7)
	eye = np.array([x, y, z], dtype=np.float64)
	# Look slightly ahead along the path tangent.
	dx = math.cos(t * 0.45) * radius * 0.45
	dz = math.cos(t * 0.9) * radius * 0.55 * 0.9
	target = eye + np.array([dx, -4.0, dz], dtype=np.float64)
	yaw = math.degrees(math.atan2(dx, dz))
	pitch = -12.0
	return eye, target, yaw, pitch


def _parse_args(argv: Iterable[str] | None = None) -> argparse.Namespace:
	"""CLI for presets and headless timing."""
	parser = argparse.ArgumentParser(description="Hyperlite procedural continent bench/game")
	parser.add_argument("--preset", choices=sorted(PRESETS.keys()), default="play")
	parser.add_argument("--seed", type=int, default=42)
	parser.add_argument("--headless", action="store_true", help="Force headless present")
	parser.add_argument("--auto", action="store_true", help="Autopilot camera (also implied by headless)")
	parser.add_argument("--seconds", type=float, default=None, help="Auto-exit after N seconds")
	parser.add_argument("--width", type=int, default=None)
	parser.add_argument("--height", type=int, default=None)
	parser.add_argument("--vsync", action="store_true", help="Enable vsync (default off for bench)")
	return parser.parse_args(list(argv) if argv is not None else None)


def main(argv: Iterable[str] | None = None) -> int:
	"""Run procedural world bench or flyable demo."""
	args = _parse_args(argv)
	cfg = PRESETS[args.preset]
	width = args.width if args.width is not None else cfg.width
	height = args.height if args.height is not None else cfg.height
	headless = args.headless or cfg.headless
	autopilot = args.auto or headless
	run_seconds = args.seconds if args.seconds is not None else cfg.default_seconds
	present = "headless" if headless else "auto"

	engine = hyperlite.Engine(width, height, "cpu", "Hyperlite Proc World", present=present)
	engine.set_vsync(args.vsync)
	engine.enable_depth(True)
	engine.set_cull_backfaces(True)

	world = ProcWorld(cfg, args.seed, engine)

	camera_pos = np.array([world.extent * 0.35, 40.0, world.extent * 0.35], dtype=np.float64)
	yaw_deg = -135.0
	pitch_deg = -10.0
	move_speed = 38.0
	look_sensitivity = 0.18
	mouse_captured = False
	enter_was = False

	start = time.perf_counter()
	last_report = start
	frame_count = 0
	report_frames = 0
	acc_ms = 0.0
	acc_chunks = 0
	acc_tris = 0
	acc_props = 0
	total_chunks = 0
	total_tris = 0
	total_props = 0
	last_stats = FrameStats()
	last_fps = 0.0

	while engine.is_running():
		now = time.perf_counter()
		if run_seconds is not None and (now - start) >= run_seconds:
			break

		engine.poll_events()
		if not autopilot and engine.key_down(hyperlite.Keys.Escape):
			if engine.mouse_captured():
				engine.set_mouse_captured(False)
				mouse_captured = False
			else:
				break

		if not autopilot:
			enter_down = engine.key_down(hyperlite.Keys.Return)
			if enter_down and not enter_was and not engine.mouse_captured():
				engine.set_mouse_captured(True)
				mouse_captured = True
			enter_was = enter_down

			if engine.mouse_captured():
				dx, dy = engine.mouse_delta()
				yaw_deg += dx * look_sensitivity
				pitch_deg = max(-85.0, min(85.0, pitch_deg - dy * look_sensitivity))

			dt = max(engine.delta_time(), 0.0)
			step = move_speed * dt
			yaw_rad = math.radians(yaw_deg)
			fwd_x = math.sin(yaw_rad)
			fwd_z = math.cos(yaw_rad)
			right_x = math.cos(yaw_rad)
			right_z = -math.sin(yaw_rad)
			if engine.key_down(hyperlite.Keys.W):
				camera_pos[0] += fwd_x * step
				camera_pos[2] += fwd_z * step
			if engine.key_down(hyperlite.Keys.S):
				camera_pos[0] -= fwd_x * step
				camera_pos[2] -= fwd_z * step
			if engine.key_down(hyperlite.Keys.A):
				camera_pos[0] -= right_x * step
				camera_pos[2] -= right_z * step
			if engine.key_down(hyperlite.Keys.D):
				camera_pos[0] += right_x * step
				camera_pos[2] += right_z * step
			if engine.key_down(VK_SPACE):
				camera_pos[1] += step
			if engine.key_down(VK_CONTROL):
				camera_pos[1] -= step
			camera_pos[0] = max(0.0, min(world.extent, camera_pos[0]))
			camera_pos[2] = max(0.0, min(world.extent, camera_pos[2]))
			camera_pos[1] = max(2.0, min(120.0, camera_pos[1]))

		if autopilot:
			t = now - start
			camera_pos, look_target, yaw_deg, pitch_deg = _autopilot_camera(t, world)
		else:
			pitch_rad = math.radians(pitch_deg)
			yaw_rad = math.radians(yaw_deg)
			fwd = np.array(
				[
					math.sin(yaw_rad) * math.cos(pitch_rad),
					math.sin(pitch_rad),
					math.cos(yaw_rad) * math.cos(pitch_rad),
				],
				dtype=np.float64,
			)
			look_target = camera_pos + fwd

		up = np.array([0.0, 1.0, 0.0], dtype=np.float64)
		view = mat4_look_at(camera_pos, look_target, up)
		aspect = width / max(height, 1)
		proj = mat4_perspective(math.radians(70.0), aspect, 0.5, 600.0)
		view_proj = mat4_mul(proj, view)

		frame_t0 = time.perf_counter()
		engine.begin_frame()
		engine.set_view_proj(view_proj)
		engine.clear(120, 170, 220, 255)
		last_stats = world.draw(engine, view_proj, camera_pos.astype(np.float32))
		_draw_hud(engine, width, height, last_fps, last_stats, cfg.name)
		engine.tick()
		frame_ms = (time.perf_counter() - frame_t0) * 1000.0

		frame_count += 1
		report_frames += 1
		acc_ms += frame_ms
		acc_chunks += last_stats.chunks_drawn
		acc_tris += last_stats.tris_submitted
		acc_props += last_stats.props_drawn
		total_chunks += last_stats.chunks_drawn
		total_tris += last_stats.tris_submitted
		total_props += last_stats.props_drawn

		if now - last_report >= 1.0:
			elapsed = now - last_report
			last_fps = report_frames / elapsed
			avg_ms = acc_ms / max(report_frames, 1)
			avg_chunks = acc_chunks / max(report_frames, 1)
			avg_tris = acc_tris / max(report_frames, 1)
			avg_props = acc_props / max(report_frames, 1)
			print(
				f"preset={cfg.name} fps={last_fps:.1f} ms={avg_ms:.2f} "
				f"chunks={avg_chunks:.0f} tris={avg_tris:.0f} props={avg_props:.0f}"
			)
			last_report = now
			report_frames = 0
			acc_ms = 0.0
			acc_chunks = 0
			acc_tris = 0
			acc_props = 0

	total_time = time.perf_counter() - start
	avg_fps = frame_count / total_time if total_time > 0 else 0.0
	avg_ms = (total_time * 1000.0) / max(frame_count, 1)
	avg_chunks = total_chunks / max(frame_count, 1)
	avg_tris = total_tris / max(frame_count, 1)
	avg_props = total_props / max(frame_count, 1)

	summary = (
		f"preset={cfg.name} frames={frame_count} fps={avg_fps:.1f} ms={avg_ms:.2f} "
		f"chunks_drawn={avg_chunks:.0f} tris_submitted={avg_tris:.0f} props_drawn={avg_props:.0f}"
	)
	print(summary)
	return 0


if __name__ == "__main__":
	sys.exit(main())
