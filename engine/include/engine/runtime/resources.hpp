#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

class Engine;
class JobSystem;

/**
 * Submesh range into a parent mesh's index/vertex buffer.
 */
struct Submesh {
	std::uint32_t index_offset = 0;
	std::uint32_t index_count = 0;
	Aabb bounds{};
};

/**
 * CPU mesh extras (bounds, submeshes, dynamic flag) keyed by Engine mesh handle.
 */
struct MeshResource {
	int engine_mesh = -1;
	Aabb bounds{};
	Sphere sphere{};
	std::vector<Submesh> submeshes{};
	bool dynamic = false;
	std::uint64_t bytes = 0;
};

/**
 * Texture region inside an atlas (pixel coords).
 */
struct TextureRegion {
	int atlas_id = -1;
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

/**
 * Material: CPU-side state consumed by Game draw helpers (not a GPU pipeline).
 */
struct Material {
	int atlas_id = -1;
	std::uint32_t color = 0xFFFFFFFFu;
	Vec2 uv_offset{0.0f, 0.0f};
	Vec2 uv_scale{1.0f, 1.0f};
	bool transparent = false;
	bool depth_test = true;
	bool depth_write = true;
	bool cull_backfaces = true;
	int filter = 0; // 0 = nearest
	int shader = 0; // 0 = unlit, 1 = lambert, 2 = bytecode
	int bytecode_id = -1;
};

/**
 * Texture/atlas bookkeeping on top of Engine::LoadAtlas.
 */
struct TextureResource {
	int atlas_id = -1;
	int width = 0;
	int height = 0;
	std::uint64_t bytes = 0;
	std::string path{};
};

/**
 * Mesh/texture/material tables with lifetime tracking. Optional convenience.
 */
class ResourceRegistry {
public:
	int RegisterMesh(const MeshResource& mesh);
	MeshResource* GetMesh(const int id);
	const MeshResource* GetMesh(const int id) const;

	int RegisterTexture(const TextureResource& tex);
	TextureResource* GetTexture(const int id);
	const TextureResource* GetTexture(const int id) const;

	int RegisterMaterial(const Material& mat);
	Material* GetMaterial(const int id);
	const Material* GetMaterial(const int id) const;

	int RegisterRegion(const TextureRegion& region);
	const TextureRegion* GetRegion(const int id) const;

	std::uint64_t BytesTracked() const { return bytes_; }

private:
	std::vector<MeshResource> meshes_{};
	std::vector<TextureResource> textures_{};
	std::vector<Material> materials_{};
	std::vector<TextureRegion> regions_{};
	std::uint64_t bytes_ = 0;
};

/**
 * Compute an AABB from packed xyz positions.
 */
Aabb BoundsFromPositions(const float* positions, const std::size_t vertex_count);

} // namespace hyperlite
