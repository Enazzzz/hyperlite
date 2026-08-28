#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hyperlite {

/**
 * 2D vector used by UI, input, and 2D game math.
 */
struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;

	/** Component-wise add. */
	Vec2 operator+(const Vec2 o) const { return {x + o.x, y + o.y}; }
	/** Component-wise subtract. */
	Vec2 operator-(const Vec2 o) const { return {x - o.x, y - o.y}; }
	/** Scale by a scalar. */
	Vec2 operator*(const float s) const { return {x * s, y * s}; }
	/** Component-wise multiply. */
	Vec2 operator*(const Vec2 o) const { return {x * o.x, y * o.y}; }
	/** Negate. */
	Vec2 operator-() const { return {-x, -y}; }
};

/**
 * 3D vector used by transforms, collision, and rendering.
 */
struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	/** Component-wise add. */
	Vec3 operator+(const Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
	/** Component-wise subtract. */
	Vec3 operator-(const Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
	/** Scale by a scalar. */
	Vec3 operator*(const float s) const { return {x * s, y * s, z * s}; }
	/** Component-wise multiply. */
	Vec3 operator*(const Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
	/** Negate. */
	Vec3 operator-() const { return {-x, -y, -z}; }
};

/**
 * Homogeneous 4D vector (clip space / colors as xyzw).
 */
struct Vec4 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 0.0f;

	/** Component-wise add. */
	Vec4 operator+(const Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
	/** Scale by a scalar. */
	Vec4 operator*(const float s) const { return {x * s, y * s, z * s, w * s}; }
};

/**
 * Unit quaternion (x, y, z, w) for 3D rotation.
 */
struct Quat {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
	float w = 1.0f;
};

/**
 * Column-major 4x4 matrix matching Engine::SetViewProj.
 */
struct Mat4 {
	float m[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
};

/**
 * TRS transform (local space).
 */
struct TransformXform {
	Vec3 position{0.0f, 0.0f, 0.0f};
	Quat rotation{};
	Vec3 scale{1.0f, 1.0f, 1.0f};
};

/**
 * Ray with origin + direction (direction need not be unit).
 */
struct Ray {
	Vec3 origin{};
	Vec3 direction{0.0f, 0.0f, 1.0f};
};

/**
 * Plane: n·x + d = 0 with n approximately unit.
 */
struct Plane {
	Vec3 normal{0.0f, 1.0f, 0.0f};
	float d = 0.0f;
};

/**
 * Axis-aligned bounding box.
 */
struct Aabb {
	Vec3 min{0.0f, 0.0f, 0.0f};
	Vec3 max{0.0f, 0.0f, 0.0f};
};

/**
 * Bounding sphere.
 */
struct Sphere {
	Vec3 center{};
	float radius = 0.0f;
};

/**
 * Capsule (segment + radius) used by physics and character controller.
 */
struct Capsule {
	Vec3 a{};
	Vec3 b{0.0f, 1.0f, 0.0f};
	float radius = 0.5f;
};

/**
 * View frustum as six planes (left, right, bottom, top, near, far).
 */
struct Frustum {
	Plane planes[6]{};
};

/** Vector length. */
inline float Length(const Vec2 v) {
	return std::sqrt(v.x * v.x + v.y * v.y);
}

/** Vector length. */
inline float Length(const Vec3 v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/** Squared length (avoids sqrt). */
inline float LengthSq(const Vec3 v) {
	return v.x * v.x + v.y * v.y + v.z * v.z;
}

/** Dot product. */
inline float Dot(const Vec2 a, const Vec2 b) {
	return a.x * b.x + a.y * b.y;
}

/** Dot product. */
inline float Dot(const Vec3 a, const Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/** Cross product. */
inline Vec3 Cross(const Vec3 a, const Vec3 b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/** Normalize; returns zero vector when length is ~0. */
inline Vec2 Normalize(const Vec2 v) {
	const float len = Length(v);
	return len > 1.0e-12f ? Vec2{v.x / len, v.y / len} : Vec2{};
}

/** Normalize; returns zero vector when length is ~0. */
inline Vec3 Normalize(const Vec3 v) {
	const float len = Length(v);
	return len > 1.0e-12f ? Vec3{v.x / len, v.y / len, v.z / len} : Vec3{};
}

/** Linear interpolate. */
inline float Lerp(const float a, const float b, const float t) {
	return a + (b - a) * t;
}

/** Linear interpolate vectors. */
inline Vec3 Lerp(const Vec3 a, const Vec3 b, const float t) {
	return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}

/** Clamp to [lo, hi]. */
inline float Clamp(const float v, const float lo, const float hi) {
	return std::min(hi, std::max(lo, v));
}

/** Identity quaternion. */
inline Quat QuatIdentity() {
	return {0.0f, 0.0f, 0.0f, 1.0f};
}

/** Quaternion from axis-angle (axis should be unit, angle in radians). */
inline Quat QuatFromAxisAngle(const Vec3 axis, const float radians) {
	const float half = radians * 0.5f;
	const float s = std::sin(half);
	return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

/** Quaternion multiply (apply b then a). */
inline Quat Mul(const Quat a, const Quat b) {
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

/** Rotate a vector by a quaternion. */
inline Vec3 Rotate(const Quat q, const Vec3 v) {
	const Vec3 u{q.x, q.y, q.z};
	const Vec3 t = Cross(u, v) * 2.0f;
	return v + t * q.w + Cross(u, t);
}

/** Normalize a quaternion. */
inline Quat Normalize(const Quat q) {
	const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
	if (len <= 1.0e-12f) {
		return QuatIdentity();
	}
	return {q.x / len, q.y / len, q.z / len, q.w / len};
}

/** Nlerp (good enough for pose blending). */
inline Quat Nlerp(Quat a, const Quat b, const float t) {
	if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
		a = {-a.x, -a.y, -a.z, -a.w};
	}
	return Normalize(Quat{
		Lerp(a.x, b.x, t),
		Lerp(a.y, b.y, t),
		Lerp(a.z, b.z, t),
		Lerp(a.w, b.w, t)});
}

/** Identity matrix. */
inline Mat4 Mat4Identity() {
	return {};
}

/** Translation matrix. */
inline Mat4 Mat4Translate(const Vec3 t) {
	Mat4 out = Mat4Identity();
	out.m[12] = t.x;
	out.m[13] = t.y;
	out.m[14] = t.z;
	return out;
}

/** Non-uniform scale matrix. */
inline Mat4 Mat4Scale(const Vec3 s) {
	Mat4 out{};
	out.m[0] = s.x;
	out.m[5] = s.y;
	out.m[10] = s.z;
	out.m[15] = 1.0f;
	return out;
}

/** Rotation matrix from quaternion. */
inline Mat4 Mat4FromQuat(const Quat q) {
	const Quat n = Normalize(q);
	const float xx = n.x * n.x;
	const float yy = n.y * n.y;
	const float zz = n.z * n.z;
	const float xy = n.x * n.y;
	const float xz = n.x * n.z;
	const float yz = n.y * n.z;
	const float wx = n.w * n.x;
	const float wy = n.w * n.y;
	const float wz = n.w * n.z;
	Mat4 out{};
	out.m[0] = 1.0f - 2.0f * (yy + zz);
	out.m[1] = 2.0f * (xy + wz);
	out.m[2] = 2.0f * (xz - wy);
	out.m[4] = 2.0f * (xy - wz);
	out.m[5] = 1.0f - 2.0f * (xx + zz);
	out.m[6] = 2.0f * (yz + wx);
	out.m[8] = 2.0f * (xz + wy);
	out.m[9] = 2.0f * (yz - wx);
	out.m[10] = 1.0f - 2.0f * (xx + yy);
	out.m[15] = 1.0f;
	return out;
}

/** Multiply column-major 4x4: out = a * b. */
inline Mat4 Mul(const Mat4 a, const Mat4 b) {
	Mat4 out{};
	for (int col = 0; col < 4; ++col) {
		for (int row = 0; row < 4; ++row) {
			out.m[col * 4 + row] =
				a.m[0 * 4 + row] * b.m[col * 4 + 0] +
				a.m[1 * 4 + row] * b.m[col * 4 + 1] +
				a.m[2 * 4 + row] * b.m[col * 4 + 2] +
				a.m[3 * 4 + row] * b.m[col * 4 + 3];
		}
	}
	return out;
}

/** Transform a point (w=1). */
inline Vec3 TransformPoint(const Mat4 m, const Vec3 p) {
	return {
		m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
		m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
		m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

/** Transform a direction (w=0). */
inline Vec3 TransformDir(const Mat4 m, const Vec3 d) {
	return {
		m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z,
		m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
		m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z};
}

/** Build TRS matrix: T * R * S. */
inline Mat4 Mat4FromTransform(const TransformXform t) {
	return Mul(Mul(Mat4Translate(t.position), Mat4FromQuat(t.rotation)), Mat4Scale(t.scale));
}

/** OpenGL-style perspective (column-major, -Z forward). */
inline Mat4 Mat4Perspective(const float fovy_rad, const float aspect, const float znear, const float zfar) {
	Mat4 out{};
	for (int i = 0; i < 16; ++i) {
		out.m[i] = 0.0f;
	}
	const float f = 1.0f / std::tan(fovy_rad * 0.5f);
	out.m[0] = f / aspect;
	out.m[5] = f;
	out.m[10] = (zfar + znear) / (znear - zfar);
	out.m[11] = -1.0f;
	out.m[14] = (2.0f * zfar * znear) / (znear - zfar);
	return out;
}

/** Orthographic projection (OpenGL-style). */
inline Mat4 Mat4Ortho(const float left, const float right, const float bottom, const float top, const float znear, const float zfar) {
	Mat4 out{};
	out.m[0] = 2.0f / (right - left);
	out.m[5] = 2.0f / (top - bottom);
	out.m[10] = -2.0f / (zfar - znear);
	out.m[12] = -(right + left) / (right - left);
	out.m[13] = -(top + bottom) / (top - bottom);
	out.m[14] = -(zfar + znear) / (zfar - znear);
	out.m[15] = 1.0f;
	return out;
}

/** Look-at view matrix (eye → target, -Z forward). */
inline Mat4 Mat4LookAt(const Vec3 eye, const Vec3 target, const Vec3 up) {
	const Vec3 f = Normalize(target - eye);
	const Vec3 s = Normalize(Cross(f, up));
	const Vec3 u = Cross(s, f);
	Mat4 out = Mat4Identity();
	out.m[0] = s.x;
	out.m[4] = s.y;
	out.m[8] = s.z;
	out.m[1] = u.x;
	out.m[5] = u.y;
	out.m[9] = u.z;
	out.m[2] = -f.x;
	out.m[6] = -f.y;
	out.m[10] = -f.z;
	out.m[12] = -Dot(s, eye);
	out.m[13] = -Dot(u, eye);
	out.m[14] = Dot(f, eye);
	return out;
}

/** Invert an affine TRS-style matrix (rotation+translation+uniform-ish scale). */
inline Mat4 Mat4AffineInverse(const Mat4 m) {
	const float a = m.m[0];
	const float b = m.m[1];
	const float c = m.m[2];
	const float d = m.m[4];
	const float e = m.m[5];
	const float f = m.m[6];
	const float g = m.m[8];
	const float h = m.m[9];
	const float i = m.m[10];
	const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
	Mat4 out = Mat4Identity();
	if (std::fabs(det) < 1.0e-12f) {
		return out;
	}
	const float inv = 1.0f / det;
	out.m[0] = (e * i - f * h) * inv;
	out.m[1] = (c * h - b * i) * inv;
	out.m[2] = (b * f - c * e) * inv;
	out.m[4] = (f * g - d * i) * inv;
	out.m[5] = (a * i - c * g) * inv;
	out.m[6] = (c * d - a * f) * inv;
	out.m[8] = (d * h - e * g) * inv;
	out.m[9] = (b * g - a * h) * inv;
	out.m[10] = (a * e - b * d) * inv;
	const Vec3 t{m.m[12], m.m[13], m.m[14]};
	out.m[12] = -(out.m[0] * t.x + out.m[4] * t.y + out.m[8] * t.z);
	out.m[13] = -(out.m[1] * t.x + out.m[5] * t.y + out.m[9] * t.z);
	out.m[14] = -(out.m[2] * t.x + out.m[6] * t.y + out.m[10] * t.z);
	return out;
}

/** Plane from a unit normal and a point on the plane. */
inline Plane PlaneFromPointNormal(const Vec3 point, const Vec3 normal) {
	const Vec3 n = Normalize(normal);
	return {n, -Dot(n, point)};
}

/** Signed distance from a point to a plane. */
inline float PlaneDistance(const Plane p, const Vec3 point) {
	return Dot(p.normal, point) + p.d;
}

/** AABB center. */
inline Vec3 AabbCenter(const Aabb b) {
	return {(b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f, (b.min.z + b.max.z) * 0.5f};
}

/** AABB half-extents. */
inline Vec3 AabbExtents(const Aabb b) {
	return {(b.max.x - b.min.x) * 0.5f, (b.max.y - b.min.y) * 0.5f, (b.max.z - b.min.z) * 0.5f};
}

/** Expand an AABB to include a point. */
inline void AabbEncapsulate(Aabb& b, const Vec3 p) {
	b.min.x = std::min(b.min.x, p.x);
	b.min.y = std::min(b.min.y, p.y);
	b.min.z = std::min(b.min.z, p.z);
	b.max.x = std::max(b.max.x, p.x);
	b.max.y = std::max(b.max.y, p.y);
	b.max.z = std::max(b.max.z, p.z);
}

/** Transform an AABB by a matrix (conservative 8-corner). */
inline Aabb TransformAabb(const Mat4 m, const Aabb b) {
	Aabb out{};
	bool first = true;
	for (int i = 0; i < 8; ++i) {
		const Vec3 p{
			(i & 1) ? b.max.x : b.min.x,
			(i & 2) ? b.max.y : b.min.y,
			(i & 4) ? b.max.z : b.min.z};
		const Vec3 w = TransformPoint(m, p);
		if (first) {
			out.min = out.max = w;
			first = false;
		} else {
			AabbEncapsulate(out, w);
		}
	}
	return out;
}

/** AABB vs AABB overlap. */
inline bool AabbOverlap(const Aabb a, const Aabb b) {
	return a.min.x <= b.max.x && a.max.x >= b.min.x &&
		a.min.y <= b.max.y && a.max.y >= b.min.y &&
		a.min.z <= b.max.z && a.max.z >= b.min.z;
}

/** Sphere vs sphere overlap. */
inline bool SphereOverlap(const Sphere a, const Sphere b) {
	const float r = a.radius + b.radius;
	return LengthSq(a.center - b.center) <= r * r;
}

/** Closest point on AABB to a point. */
inline Vec3 ClosestPointAabb(const Aabb b, const Vec3 p) {
	return {
		Clamp(p.x, b.min.x, b.max.x),
		Clamp(p.y, b.min.y, b.max.y),
		Clamp(p.z, b.min.z, b.max.z)};
}

/** Closest point on a segment to a point. */
inline Vec3 ClosestPointSegment(const Vec3 a, const Vec3 b, const Vec3 p) {
	const Vec3 ab = b - a;
	const float denom = LengthSq(ab);
	if (denom < 1.0e-12f) {
		return a;
	}
	const float t = Clamp(Dot(p - a, ab) / denom, 0.0f, 1.0f);
	return a + ab * t;
}

/** Ray vs AABB. Returns true and writes hit t when the ray intersects. */
inline bool RayAabb(const Ray ray, const Aabb box, float& t_hit) {
	float tmin = 0.0f;
	float tmax = 1.0e30f;
	const float* orig = &ray.origin.x;
	const float* dir = &ray.direction.x;
	const float* bmin = &box.min.x;
	const float* bmax = &box.max.x;
	for (int i = 0; i < 3; ++i) {
		if (std::fabs(dir[i]) < 1.0e-12f) {
			if (orig[i] < bmin[i] || orig[i] > bmax[i]) {
				return false;
			}
			continue;
		}
		float t0 = (bmin[i] - orig[i]) / dir[i];
		float t1 = (bmax[i] - orig[i]) / dir[i];
		if (t0 > t1) {
			const float tmp = t0;
			t0 = t1;
			t1 = tmp;
		}
		tmin = std::max(tmin, t0);
		tmax = std::min(tmax, t1);
		if (tmax < tmin) {
			return false;
		}
	}
	t_hit = tmin;
	return true;
}

/** Ray vs sphere. */
inline bool RaySphere(const Ray ray, const Sphere s, float& t_hit) {
	const Vec3 oc = ray.origin - s.center;
	const float a = LengthSq(ray.direction);
	const float b = 2.0f * Dot(oc, ray.direction);
	const float c = LengthSq(oc) - s.radius * s.radius;
	const float disc = b * b - 4.0f * a * c;
	if (disc < 0.0f || a < 1.0e-12f) {
		return false;
	}
	const float sqrt_d = std::sqrt(disc);
	float t = (-b - sqrt_d) / (2.0f * a);
	if (t < 0.0f) {
		t = (-b + sqrt_d) / (2.0f * a);
	}
	if (t < 0.0f) {
		return false;
	}
	t_hit = t;
	return true;
}

/** Ray vs plane (one-sided, t>=0). */
inline bool RayPlane(const Ray ray, const Plane plane, float& t_hit) {
	const float denom = Dot(plane.normal, ray.direction);
	if (std::fabs(denom) < 1.0e-12f) {
		return false;
	}
	const float t = -(Dot(plane.normal, ray.origin) + plane.d) / denom;
	if (t < 0.0f) {
		return false;
	}
	t_hit = t;
	return true;
}

/** Möller–Trumbore ray vs triangle. */
inline bool RayTriangle(
	const Ray ray,
	const Vec3 v0,
	const Vec3 v1,
	const Vec3 v2,
	float& t_hit) {
	const Vec3 e1 = v1 - v0;
	const Vec3 e2 = v2 - v0;
	const Vec3 pvec = Cross(ray.direction, e2);
	const float det = Dot(e1, pvec);
	if (std::fabs(det) < 1.0e-12f) {
		return false;
	}
	const float inv = 1.0f / det;
	const Vec3 tvec = ray.origin - v0;
	const float u = Dot(tvec, pvec) * inv;
	if (u < 0.0f || u > 1.0f) {
		return false;
	}
	const Vec3 qvec = Cross(tvec, e1);
	const float v = Dot(ray.direction, qvec) * inv;
	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}
	const float t = Dot(e2, qvec) * inv;
	if (t < 0.0f) {
		return false;
	}
	t_hit = t;
	return true;
}

/** Extract frustum planes from a column-major view-projection matrix. */
inline Frustum FrustumFromViewProj(const Mat4 vp) {
	Frustum f{};
	const auto make = [](float a, float b, float c, float d) {
		const float len = std::sqrt(a * a + b * b + c * c);
		const float inv = len > 1.0e-12f ? 1.0f / len : 1.0f;
		return Plane{{a * inv, b * inv, c * inv}, d * inv};
	};
	// left, right, bottom, top, near, far
	f.planes[0] = make(vp.m[3] + vp.m[0], vp.m[7] + vp.m[4], vp.m[11] + vp.m[8], vp.m[15] + vp.m[12]);
	f.planes[1] = make(vp.m[3] - vp.m[0], vp.m[7] - vp.m[4], vp.m[11] - vp.m[8], vp.m[15] - vp.m[12]);
	f.planes[2] = make(vp.m[3] + vp.m[1], vp.m[7] + vp.m[5], vp.m[11] + vp.m[9], vp.m[15] + vp.m[13]);
	f.planes[3] = make(vp.m[3] - vp.m[1], vp.m[7] - vp.m[5], vp.m[11] - vp.m[9], vp.m[15] - vp.m[13]);
	f.planes[4] = make(vp.m[3] + vp.m[2], vp.m[7] + vp.m[6], vp.m[11] + vp.m[10], vp.m[15] + vp.m[14]);
	f.planes[5] = make(vp.m[3] - vp.m[2], vp.m[7] - vp.m[6], vp.m[11] - vp.m[10], vp.m[15] - vp.m[14]);
	return f;
}

/** True when the AABB is at least partially inside the frustum. */
inline bool FrustumAabb(const Frustum& f, const Aabb box) {
	const Vec3 c = AabbCenter(box);
	const Vec3 e = AabbExtents(box);
	for (int i = 0; i < 6; ++i) {
		const Plane p = f.planes[i];
		const float r = e.x * std::fabs(p.normal.x) + e.y * std::fabs(p.normal.y) + e.z * std::fabs(p.normal.z);
		if (PlaneDistance(p, c) < -r) {
			return false;
		}
	}
	return true;
}

/** True when the sphere is at least partially inside the frustum. */
inline bool FrustumSphere(const Frustum& f, const Sphere s) {
	for (int i = 0; i < 6; ++i) {
		if (PlaneDistance(f.planes[i], s.center) < -s.radius) {
			return false;
		}
	}
	return true;
}

} // namespace hyperlite
