#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"
#include "engine/types.hpp"

namespace {

/**
 * Assert helper for retained-mesh layer tests.
 */
bool Expect(const bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		return false;
	}
	return true;
}

/**
 * Read one packed RGBA pixel from the engine framebuffer.
 */
hyperlite::Color ReadPixel(hyperlite::Engine& engine, const int x, const int y) {
	int width = 0;
	int height = 0;
	engine.WindowSize(width, height);
	const std::uint8_t* data = engine.FramebufferPtr();
	const std::size_t idx = static_cast<std::size_t>((y * width + x) * 4);
	return {
		data[idx + 0U],
		data[idx + 1U],
		data[idx + 2U],
		data[idx + 3U]};
}

/**
 * Whether a pixel matches an expected RGB (alpha ignored).
 */
bool PixelRgbEquals(const hyperlite::Color& pixel, const std::uint8_t r, const std::uint8_t g, const std::uint8_t b) {
	return pixel.r == r && pixel.g == g && pixel.b == b;
}

/**
 * Identity column-major 4x4.
 */
void FillIdentity(float* m) {
	for (int i = 0; i < 16; ++i) {
		m[i] = 0.0f;
	}
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/**
 * Column-major translation matrix (mesh-local → world).
 */
void FillTranslate(float* m, const float tx, const float ty, const float tz) {
	FillIdentity(m);
	m[12] = tx;
	m[13] = ty;
	m[14] = tz;
}

/**
 * Unit quad in XY (z=0): two tris, CCW in NDC, with UV + pad per vertex.
 *
 * verts layout: x,y,z,u,v,_pad — 6 floats/vert.
 */
void FillUnitQuadVerts(std::vector<float>& verts) {
	// 0: BL, 1: BR, 2: TR, 3: TL — OpenGL-front CCW for front face.
	const float raw[] = {
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f,
		-0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f};
	verts.assign(raw, raw + 24);
}

/**
 * Two triangles indexing the unit quad.
 */
void FillUnitQuadIndices(std::vector<std::uint32_t>& indices) {
	indices = {0U, 1U, 2U, 0U, 2U, 3U};
}

} // namespace

/**
 * Unit tests for Layer 2 retained meshes (load_mesh / draw_mesh), headless CPU.
 */
int main() {
	bool ok = true;

	// --- Indexed unit quad + identity model + identity view-proj fills center ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-quad", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);
		engine.SetCullBackfaces(true);

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());
		ok = Expect(mesh >= 0, "load_mesh: valid handle") && ok;

		float model[16];
		FillIdentity(model);
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({10, 10, 10, 255})));
		engine.DrawMesh(mesh, model, hyperlite::PackColor({255, 0, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 255, 0, 0), "quad: center red") && ok;
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 2, 2), 10, 10, 10), "quad: corner clear") && ok;
	}

	// --- Triangle-list load (no indices) still draws ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-trilist", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		// One CCW triangle as a triangle list (3 verts × 6 floats).
		const float verts[] = {
			-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
			0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f};
		const int mesh = engine.LoadMesh(verts, 18U, nullptr, 0U);
		ok = Expect(mesh >= 0, "triangle list: valid handle") && ok;

		float model[16];
		FillIdentity(model);
		engine.TickMesh(
			hyperlite::PackColor({0, 0, 0, 255}),
			mesh,
			model,
			hyperlite::PackColor({0, 255, 0, 255}));
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 0, 255, 0), "triangle list: center green") && ok;
	}

	// --- Two draw_mesh with different translations; depth keeps nearer on top ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-depth", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());

		float far_model[16];
		float near_model[16];
		// In identity clip, +Z is farther (window depth = ndc*0.5+0.5).
		FillTranslate(far_model, 0.0f, 0.0f, 0.4f);
		FillTranslate(near_model, 0.0f, 0.0f, -0.4f);

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.DrawMesh(mesh, far_model, hyperlite::PackColor({0, 0, 255, 255}));
		engine.DrawMesh(mesh, near_model, hyperlite::PackColor({255, 255, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 255, 255, 0), "depth: nearer yellow wins") && ok;
		ok = Expect(engine.DepthAt(32, 32) < 0.5f, "depth: window z nearer than mid") && ok;
	}

	// --- Invalid handle is a no-op (no crash); frame stays clear ---
	{
		hyperlite::Engine engine(32, 32, hyperlite::BackendKind::kCpu, "mesh-invalid", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);
		float model[16];
		FillIdentity(model);

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({40, 40, 40, 255})));
		engine.DrawMesh(-1, model, hyperlite::PackColor({255, 0, 0, 255}));
		engine.DrawMesh(9999, model, hyperlite::PackColor({255, 0, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 16, 16), 40, 40, 40), "invalid handle: no-op clear preserved") && ok;
	}

	// --- Bad uploads return -1 ---
	{
		hyperlite::Engine engine(16, 16, hyperlite::BackendKind::kCpu, "mesh-bad", hyperlite::PresentMode::kHeadless);
		const float bad_verts[] = {0.0f, 0.0f, 0.0f}; // not multiple of 6
		ok = Expect(engine.LoadMesh(bad_verts, 3U, nullptr, 0U) < 0, "bad verts: reject") && ok;
		const float one_vert[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
		ok = Expect(engine.LoadMesh(one_vert, 6U, nullptr, 0U) < 0, "non-tri-list count: reject") && ok;
		const std::uint32_t oob[] = {0U, 1U, 99U};
		const float tri[] = {
			0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
		ok = Expect(engine.LoadMesh(tri, 18U, oob, 3U) < 0, "oob index: reject") && ok;
	}

	// --- draw_mesh_many N=2 depth ordering (same color batch) ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-many-depth", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());

		float models[32];
		FillTranslate(models, 0.0f, 0.0f, 0.4f);
		FillTranslate(models + 16, 0.0f, 0.0f, -0.4f);

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.DrawMeshMany(mesh, models, 2U, hyperlite::PackColor({255, 0, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 255, 0, 0), "mesh_many: nearer instance wins depth") && ok;
		ok = Expect(engine.DepthAt(32, 32) < 0.5f, "mesh_many: window z nearer than mid") && ok;
	}

	// --- draw_mesh_many N=1 matches draw_mesh pixels ---
	{
		hyperlite::Engine engine_single(64, 64, hyperlite::BackendKind::kCpu, "mesh-one", hyperlite::PresentMode::kHeadless);
		hyperlite::Engine engine_many(64, 64, hyperlite::BackendKind::kCpu, "mesh-one-many", hyperlite::PresentMode::kHeadless);
		for (hyperlite::Engine* eng : {&engine_single, &engine_many}) {
			eng->SetVsync(false);
			eng->EnableDepth(true);
			float vp[16];
			FillIdentity(vp);
			eng->SetViewProj(vp);
		}

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh_single = engine_single.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());
		const int mesh_many = engine_many.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());

		float model[16];
		FillIdentity(model);

		engine_single.BeginFrame();
		engine_single.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({10, 10, 10, 255})));
		engine_single.DrawMesh(mesh_single, model, hyperlite::PackColor({0, 200, 50, 255}));
		engine_single.EndFrame();
		engine_single.Present();

		engine_many.BeginFrame();
		engine_many.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({10, 10, 10, 255})));
		engine_many.DrawMeshMany(mesh_many, model, 1U, hyperlite::PackColor({0, 200, 50, 255}));
		engine_many.EndFrame();
		engine_many.Present();

		const hyperlite::Color single_px = ReadPixel(engine_single, 32, 32);
		const hyperlite::Color many_px = ReadPixel(engine_many, 32, 32);
		ok = Expect(PixelRgbEquals(single_px, many_px.r, many_px.g, many_px.b), "mesh_many N=1: same center pixel as draw_mesh") && ok;
	}

	if (!ok) {
		std::cerr << "mesh_layer_tests FAILED\n";
		return 1;
	}
	std::cout << "mesh_layer_tests=ok\n";
	return 0;
}
