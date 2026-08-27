#include <cstdint>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"
#include "engine/types.hpp"

namespace {

/**
 * Assert helper for textured-mesh layer tests.
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
 * Whether a pixel matches expected RGBA.
 */
bool PixelEquals(
	const hyperlite::Color& pixel,
	const std::uint8_t r,
	const std::uint8_t g,
	const std::uint8_t b,
	const std::uint8_t a = 255) {
	return pixel.r == r && pixel.g == g && pixel.b == b && pixel.a == a;
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
 * Unit quad covering the NDC center with UVs spanning the full atlas (0..1).
 *
 * verts layout: x,y,z,u,v,_pad — BL→BR→TR→TL (OpenGL-front CCW).
 */
void FillUnitQuadVerts(std::vector<float>& verts) {
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

/**
 * Build a 2×2 RGBA atlas with four distinct opaque colors (row-major).
 *
 * (0,0)=red (1,0)=green (0,1)=blue (1,1)=yellow — matches UV nearest sampling.
 */
void FillAtlas2x2(std::vector<std::uint8_t>& rgba) {
	rgba = {
		255, 0, 0, 255, // u~0,v~0
		0, 255, 0, 255, // u~1,v~0
		0, 0, 255, 255, // u~0,v~1
		255, 255, 0, 255 // u~1,v~1
	};
}

} // namespace

/**
 * Unit tests for Layer 2.1 textured retained meshes (draw_mesh_textured), headless CPU.
 */
int main() {
	bool ok = true;

	// --- 2×2 atlas + screen-facing quad: nearest colors at UV corners ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-tex", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);
		engine.SetCullBackfaces(true);

		std::vector<std::uint8_t> atlas_px;
		FillAtlas2x2(atlas_px);
		const int atlas = engine.LoadAtlas(atlas_px.data(), atlas_px.size(), 2, 2);
		ok = Expect(atlas >= 0, "load_atlas: valid handle") && ok;

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
		engine.DrawMeshTextured(mesh, model, atlas);
		engine.EndFrame();
		engine.Present();

		// Quad spans pixel x,y in ~[16,48). After viewport Y flip, NDC +Y is small pixel y.
		// Atlas: (u0,v0)=red (u1,v0)=green (u0,v1)=blue (u1,v1)=yellow.
		ok = Expect(PixelEquals(ReadPixel(engine, 20, 20), 0, 0, 255), "tex: screen TL = UV (0,1) blue") && ok;
		ok = Expect(PixelEquals(ReadPixel(engine, 44, 20), 255, 255, 0), "tex: screen TR = UV (1,1) yellow") && ok;
		ok = Expect(PixelEquals(ReadPixel(engine, 20, 44), 255, 0, 0), "tex: screen BL = UV (0,0) red") && ok;
		ok = Expect(PixelEquals(ReadPixel(engine, 44, 44), 0, 255, 0), "tex: screen BR = UV (1,0) green") && ok;
		ok = Expect(PixelEquals(ReadPixel(engine, 2, 2), 10, 10, 10), "tex: corner clear") && ok;
	}

	// --- Flat draw_mesh still fills solid color (regression) ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-flat-reg", hyperlite::PresentMode::kHeadless);
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

		float model[16];
		FillIdentity(model);
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.DrawMesh(mesh, model, hyperlite::PackColor({255, 0, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelEquals(ReadPixel(engine, 32, 32), 255, 0, 0), "flat draw_mesh: center red") && ok;
	}

	// --- Bad atlas id is a no-op (no crash); frame stays clear ---
	{
		hyperlite::Engine engine(32, 32, hyperlite::BackendKind::kCpu, "mesh-bad-atlas", hyperlite::PresentMode::kHeadless);
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

		float model[16];
		FillIdentity(model);
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({40, 40, 40, 255})));
		engine.DrawMeshTextured(mesh, model, -1);
		engine.DrawMeshTextured(mesh, model, 9999);
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelEquals(ReadPixel(engine, 16, 16), 40, 40, 40), "bad atlas: no-op clear preserved") && ok;
	}

	// --- tick_mesh_textured fused path paints atlas ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-tick-tex", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		std::vector<std::uint8_t> atlas_px;
		FillAtlas2x2(atlas_px);
		const int atlas = engine.LoadAtlas(atlas_px.data(), atlas_px.size(), 2, 2);

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());

		float model[16];
		FillIdentity(model);
		const int drawn = engine.TickMeshTextured(
			hyperlite::PackColor({0, 0, 0, 255}),
			mesh,
			model,
			atlas);
		ok = Expect(drawn == 2, "tick_mesh_textured: two tris") && ok;
		ok = Expect(PixelEquals(ReadPixel(engine, 20, 44), 255, 0, 0), "tick_mesh_textured: screen BL red") && ok;
	}

	// --- draw_mesh_textured_many paints atlas for two instances ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "mesh-tex-many", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		std::vector<std::uint8_t> atlas_px;
		FillAtlas2x2(atlas_px);
		const int atlas = engine.LoadAtlas(atlas_px.data(), atlas_px.size(), 2, 2);

		std::vector<float> verts;
		std::vector<std::uint32_t> indices;
		FillUnitQuadVerts(verts);
		FillUnitQuadIndices(indices);
		const int mesh = engine.LoadMesh(verts.data(), verts.size(), indices.data(), indices.size());

		float models[32];
		FillIdentity(models);
		FillTranslate(models + 16, 0.35f, 0.0f, 0.0f);

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.DrawMeshTexturedMany(mesh, models, 2U, atlas);
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelEquals(ReadPixel(engine, 32, 32), 255, 0, 0), "mesh_textured_many: center red atlas") && ok;
	}

	if (!ok) {
		std::cerr << "mesh_textured_tests FAILED\n";
		return 1;
	}
	std::cout << "mesh_textured_tests=ok\n";
	return 0;
}
