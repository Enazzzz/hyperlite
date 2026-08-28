#include "engine/runtime/transform.hpp"

#include <cstring>

namespace hyperlite {

TransformId TransformStore::Create(const Vec3 position, const Quat rotation, const Vec3 scale) {
	TransformId id = -1;
	if (!free_.empty()) {
		id = free_.back();
		free_.pop_back();
		Node& n = nodes_[static_cast<std::size_t>(id)];
		n.local.position = position;
		n.local.rotation = rotation;
		n.local.scale = scale;
		n.world = Mat4Identity();
		n.parent = -1;
		++n.generation;
		n.alive = true;
		n.dirty = true;
		return id;
	}
	id = static_cast<TransformId>(nodes_.size());
	Node n{};
	n.local.position = position;
	n.local.rotation = rotation;
	n.local.scale = scale;
	n.alive = true;
	n.dirty = true;
	nodes_.push_back(n);
	return id;
}

void TransformStore::Destroy(const TransformId id) {
	if (!Valid(id)) {
		return;
	}
	for (std::size_t i = 0; i < nodes_.size(); ++i) {
		if (nodes_[i].alive && nodes_[i].parent == id) {
			nodes_[i].parent = -1;
			nodes_[i].dirty = true;
		}
	}
	nodes_[static_cast<std::size_t>(id)].alive = false;
	nodes_[static_cast<std::size_t>(id)].parent = -1;
	free_.push_back(id);
}

bool TransformStore::Valid(const TransformId id) const {
	return id >= 0 && static_cast<std::size_t>(id) < nodes_.size() && nodes_[static_cast<std::size_t>(id)].alive;
}

void TransformStore::MarkDirty(const TransformId id) {
	if (!Valid(id)) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].dirty = true;
	for (std::size_t i = 0; i < nodes_.size(); ++i) {
		if (nodes_[i].alive && nodes_[i].parent == id) {
			MarkDirty(static_cast<TransformId>(i));
		}
	}
}

void TransformStore::SetLocalPosition(const TransformId id, const Vec3 p) {
	if (!Valid(id)) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].local.position = p;
	MarkDirty(id);
}

void TransformStore::SetLocalRotation(const TransformId id, const Quat q) {
	if (!Valid(id)) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].local.rotation = q;
	MarkDirty(id);
}

void TransformStore::SetLocalScale(const TransformId id, const Vec3 s) {
	if (!Valid(id)) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].local.scale = s;
	MarkDirty(id);
}

void TransformStore::SetLocal(const TransformId id, const TransformXform xform) {
	if (!Valid(id)) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].local = xform;
	MarkDirty(id);
}

Vec3 TransformStore::LocalPosition(const TransformId id) const {
	return Valid(id) ? nodes_[static_cast<std::size_t>(id)].local.position : Vec3{};
}

Quat TransformStore::LocalRotation(const TransformId id) const {
	return Valid(id) ? nodes_[static_cast<std::size_t>(id)].local.rotation : QuatIdentity();
}

Vec3 TransformStore::LocalScale(const TransformId id) const {
	return Valid(id) ? nodes_[static_cast<std::size_t>(id)].local.scale : Vec3{1.0f, 1.0f, 1.0f};
}

void TransformStore::SetParent(const TransformId id, const TransformId parent) {
	if (!Valid(id)) {
		return;
	}
	if (parent == id) {
		return;
	}
	nodes_[static_cast<std::size_t>(id)].parent = Valid(parent) ? parent : -1;
	MarkDirty(id);
}

TransformId TransformStore::Parent(const TransformId id) const {
	return Valid(id) ? nodes_[static_cast<std::size_t>(id)].parent : -1;
}

void TransformStore::UpdateOne(const TransformId id) {
	Node& n = nodes_[static_cast<std::size_t>(id)];
	if (!n.alive) {
		return;
	}
	if (n.parent >= 0 && nodes_[static_cast<std::size_t>(n.parent)].dirty) {
		UpdateOne(n.parent);
	}
	const Mat4 local = Mat4FromTransform(n.local);
	if (n.parent >= 0 && nodes_[static_cast<std::size_t>(n.parent)].alive) {
		n.world = Mul(nodes_[static_cast<std::size_t>(n.parent)].world, local);
	} else {
		n.world = local;
	}
	n.dirty = false;
}

void TransformStore::UpdateDirty() {
	for (std::size_t i = 0; i < nodes_.size(); ++i) {
		if (nodes_[i].alive && nodes_[i].dirty) {
			UpdateOne(static_cast<TransformId>(i));
		}
	}
}

void TransformStore::UpdateAll() {
	for (auto& n : nodes_) {
		if (n.alive) {
			n.dirty = true;
		}
	}
	UpdateDirty();
}

const Mat4& TransformStore::WorldMatrix(const TransformId id) const {
	static Mat4 identity = Mat4Identity();
	if (!Valid(id)) {
		return identity;
	}
	return nodes_[static_cast<std::size_t>(id)].world;
}

Vec3 TransformStore::WorldPosition(const TransformId id) const {
	if (!Valid(id)) {
		return {};
	}
	const Mat4& m = nodes_[static_cast<std::size_t>(id)].world;
	return {m.m[12], m.m[13], m.m[14]};
}

void TransformStore::BatchWorldMatrices(const TransformId* ids, const std::size_t count, float* out16) const {
	if (ids == nullptr || out16 == nullptr) {
		return;
	}
	for (std::size_t i = 0; i < count; ++i) {
		const Mat4& m = WorldMatrix(ids[i]);
		std::memcpy(out16 + i * 16U, m.m, sizeof(float) * 16U);
	}
}

} // namespace hyperlite
