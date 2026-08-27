#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * CPU-resident mesh: tightly packed positions + UVs, optional index buffer.
 *
 * Vertex layout (v1): 6 float32 per vertex — x, y, z, u, v, _pad.
 * UVs are 0..1 over the full atlas for draw_mesh_textured; flat draw_mesh ignores them.
 * Empty indices means a triangle list (vertex_count must be a multiple of 3).
 */
struct MeshEntry {
	/** Packed xyz positions (3 floats per vertex). */
	std::vector<float> positions{};
	/** Packed u,v (2 floats per vertex); pad from the upload is discarded. */
	std::vector<float> uvs{};
	/** Triangle indices (3 uint32 per tri). Empty = sequential triangle list. */
	std::vector<std::uint32_t> indices{};
	/** Number of vertices. */
	std::size_t vertex_count = 0U;
	/** Number of triangles after indexing / triangle-list expand. */
	std::size_t triangle_count = 0U;
};

/**
 * CPU-resident mesh storage keyed by integer handle (like AtlasStore).
 *
 * Meshes stay resident until the Engine dies. No GPU upload — CPU raster is default.
 */
class MeshStore {
public:
	/**
	 * Load a mesh from interleaved verts (6 floats/vert) and optional indices.
	 *
	 * @param verts float32 buffer; length must be vertex_count * 6
	 * @param vert_floats number of float32 values in verts
	 * @param indices uint32 triangle indices, or nullptr / empty for triangle list
	 * @param index_count number of uint32 indices (multiple of 3, or 0)
	 * @return handle >= 0 on success, -1 on invalid input
	 */
	int Load(
		const float* verts,
		std::size_t vert_floats,
		const std::uint32_t* indices,
		std::size_t index_count);

	/**
	 * Read one mesh entry; returns nullptr when handle is invalid.
	 */
	const MeshEntry* Get(int handle) const;

	/**
	 * Number of loaded meshes.
	 */
	std::size_t Count() const { return entries_.size(); }

private:
	std::vector<MeshEntry> entries_{};
};

} // namespace hyperlite
