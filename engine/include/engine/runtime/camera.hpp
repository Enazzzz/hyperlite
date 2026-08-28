#pragma once

#include "engine/runtime/math.hpp"

namespace hyperlite {

/**
 * Native camera: projection + view, world↔screen, frustum.
 *
 * Does not auto-bind to Engine; call Apply(Engine&) when you want the rasterizer to use it.
 */
class Camera {
public:
	enum class Projection { Perspective, Orthographic };

	void SetPerspective(const float fovy_rad, const float aspect, const float znear, const float zfar);
	void SetOrthographic(const float left, const float right, const float bottom, const float top, const float znear, const float zfar);

	void LookAt(const Vec3 eye, const Vec3 target, const Vec3 up);
	void SetView(const Mat4 view);
	void SetEye(const Vec3 eye);

	const Mat4& View() const { return view_; }
	const Mat4& ProjectionMatrix() const { return proj_; }
	Mat4 ViewProj() const { return Mul(proj_, view_); }

	Vec3 Eye() const { return eye_; }
	float Aspect() const { return aspect_; }
	float Near() const { return znear_; }
	float Far() const { return zfar_; }
	Projection Kind() const { return kind_; }

	Frustum MakeFrustum() const { return FrustumFromViewProj(ViewProj()); }

	/**
	 * Project a world point to pixel coordinates (origin top-left, y down).
	 */
	Vec2 WorldToScreen(const Vec3 world, const int width, const int height) const;

	/**
	 * Unproject a pixel + depth in [0,1] (0=near) to a world-space ray.
	 */
	Ray ScreenToRay(const float px, const float py, const int width, const int height) const;

private:
	Projection kind_ = Projection::Perspective;
	Mat4 view_ = Mat4Identity();
	Mat4 proj_ = Mat4Identity();
	Vec3 eye_{0.0f, 0.0f, 3.0f};
	float fovy_ = 1.04719755f;
	float aspect_ = 16.0f / 9.0f;
	float znear_ = 0.1f;
	float zfar_ = 100.0f;
	float ortho_left_ = -1.0f;
	float ortho_right_ = 1.0f;
	float ortho_bottom_ = -1.0f;
	float ortho_top_ = 1.0f;
};

} // namespace hyperlite
