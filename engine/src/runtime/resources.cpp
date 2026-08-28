#include "engine/runtime/resources.hpp"

#include <algorithm>

namespace hyperlite {

Aabb BoundsFromPositions(const float* positions, const std::size_t vertex_count) {
	Aabb b{};
	if (positions == nullptr || vertex_count == 0U) {
		return b;
	}
	b.min = b.max = {positions[0], positions[1], positions[2]};
	for (std::size_t i = 1; i < vertex_count; ++i) {
		AabbEncapsulate(b, {positions[i * 3U], positions[i * 3U + 1U], positions[i * 3U + 2U]});
	}
	return b;
}

int ResourceRegistry::RegisterMesh(const MeshResource& mesh) {
	meshes_.push_back(mesh);
	bytes_ += mesh.bytes;
	return static_cast<int>(meshes_.size()) - 1;
}

MeshResource* ResourceRegistry::GetMesh(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= meshes_.size()) {
		return nullptr;
	}
	return &meshes_[static_cast<std::size_t>(id)];
}

const MeshResource* ResourceRegistry::GetMesh(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= meshes_.size()) {
		return nullptr;
	}
	return &meshes_[static_cast<std::size_t>(id)];
}

int ResourceRegistry::RegisterTexture(const TextureResource& tex) {
	textures_.push_back(tex);
	bytes_ += tex.bytes;
	return static_cast<int>(textures_.size()) - 1;
}

TextureResource* ResourceRegistry::GetTexture(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= textures_.size()) {
		return nullptr;
	}
	return &textures_[static_cast<std::size_t>(id)];
}

const TextureResource* ResourceRegistry::GetTexture(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= textures_.size()) {
		return nullptr;
	}
	return &textures_[static_cast<std::size_t>(id)];
}

int ResourceRegistry::RegisterMaterial(const Material& mat) {
	materials_.push_back(mat);
	return static_cast<int>(materials_.size()) - 1;
}

Material* ResourceRegistry::GetMaterial(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= materials_.size()) {
		return nullptr;
	}
	return &materials_[static_cast<std::size_t>(id)];
}

const Material* ResourceRegistry::GetMaterial(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= materials_.size()) {
		return nullptr;
	}
	return &materials_[static_cast<std::size_t>(id)];
}

int ResourceRegistry::RegisterRegion(const TextureRegion& region) {
	regions_.push_back(region);
	return static_cast<int>(regions_.size()) - 1;
}

const TextureRegion* ResourceRegistry::GetRegion(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= regions_.size()) {
		return nullptr;
	}
	return &regions_[static_cast<std::size_t>(id)];
}

} // namespace hyperlite
