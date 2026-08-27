#include "engine/mesh_store.hpp"

namespace hyperlite {

int MeshStore::Load(
	const float* verts,
	const std::size_t vert_floats,
	const std::uint32_t* indices,
	const std::size_t index_count) {
	// v1 layout: 6 floats per vertex (x,y,z,u,v,_pad).
	constexpr std::size_t kFloatsPerVert = 6U;
	if (verts == nullptr || vert_floats < kFloatsPerVert || (vert_floats % kFloatsPerVert) != 0U) {
		return -1;
	}
	const std::size_t vertex_count = vert_floats / kFloatsPerVert;
	if (index_count > 0U) {
		if (indices == nullptr || (index_count % 3U) != 0U) {
			return -1;
		}
		for (std::size_t i = 0U; i < index_count; ++i) {
			if (indices[i] >= vertex_count) {
				return -1;
			}
		}
	} else if ((vertex_count % 3U) != 0U) {
		// Triangle list requires a multiple of 3 vertices.
		return -1;
	}

	MeshEntry entry{};
	entry.vertex_count = vertex_count;
	entry.positions.resize(vertex_count * 3U);
	entry.uvs.resize(vertex_count * 2U);
	for (std::size_t v = 0U; v < vertex_count; ++v) {
		const float* src = verts + v * kFloatsPerVert;
		entry.positions[v * 3U + 0U] = src[0];
		entry.positions[v * 3U + 1U] = src[1];
		entry.positions[v * 3U + 2U] = src[2];
		entry.uvs[v * 2U + 0U] = src[3];
		entry.uvs[v * 2U + 1U] = src[4];
		// src[5] pad intentionally ignored
	}
	if (index_count > 0U) {
		entry.indices.assign(indices, indices + index_count);
		entry.triangle_count = index_count / 3U;
	} else {
		entry.triangle_count = vertex_count / 3U;
	}

	const int handle = static_cast<int>(entries_.size());
	entries_.push_back(std::move(entry));
	return handle;
}

const MeshEntry* MeshStore::Get(const int handle) const {
	if (handle < 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
		return nullptr;
	}
	return &entries_[static_cast<std::size_t>(handle)];
}

} // namespace hyperlite
