#include "engine/runtime/camera.hpp"

#include <cmath>

namespace hyperlite {

void Camera::SetPerspective(const float fovy_rad, const float aspect, const float znear, const float zfar) {
	kind_ = Projection::Perspective;
	fovy_ = fovy_rad;
	aspect_ = aspect;
	znear_ = znear;
	zfar_ = zfar;
	proj_ = Mat4Perspective(fovy_rad, aspect, znear, zfar);
}

void Camera::SetOrthographic(
	const float left,
	const float right,
	const float bottom,
	const float top,
	const float znear,
	const float zfar) {
	kind_ = Projection::Orthographic;
	ortho_left_ = left;
	ortho_right_ = right;
	ortho_bottom_ = bottom;
	ortho_top_ = top;
	znear_ = znear;
	zfar_ = zfar;
	proj_ = Mat4Ortho(left, right, bottom, top, znear, zfar);
}

void Camera::LookAt(const Vec3 eye, const Vec3 target, const Vec3 up) {
	eye_ = eye;
	view_ = Mat4LookAt(eye, target, up);
}

void Camera::SetView(const Mat4 view) {
	view_ = view;
	eye_ = {view.m[12], view.m[13], view.m[14]};
}

void Camera::SetEye(const Vec3 eye) {
	eye_ = eye;
}

Vec2 Camera::WorldToScreen(const Vec3 world, const int width, const int height) const {
	const Mat4 vp = ViewProj();
	const float x = vp.m[0] * world.x + vp.m[4] * world.y + vp.m[8] * world.z + vp.m[12];
	const float y = vp.m[1] * world.x + vp.m[5] * world.y + vp.m[9] * world.z + vp.m[13];
	const float w = vp.m[3] * world.x + vp.m[7] * world.y + vp.m[11] * world.z + vp.m[15];
	if (std::fabs(w) < 1.0e-12f) {
		return {};
	}
	const float ndc_x = x / w;
	const float ndc_y = y / w;
	return {
		(ndc_x * 0.5f + 0.5f) * static_cast<float>(width),
		(1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(height)};
}

Ray Camera::ScreenToRay(const float px, const float py, const int width, const int height) const {
	const float ndc_x = (px / static_cast<float>(width)) * 2.0f - 1.0f;
	const float ndc_y = 1.0f - (py / static_cast<float>(height)) * 2.0f;
	const Mat4 inv = Mat4AffineInverse(ViewProj());
	const Vec3 near_p = TransformPoint(inv, {ndc_x, ndc_y, -1.0f});
	const Vec3 far_p = TransformPoint(inv, {ndc_x, ndc_y, 1.0f});
	Ray r{};
	r.origin = near_p;
	r.direction = Normalize(far_p - near_p);
	return r;
}

} // namespace hyperlite
