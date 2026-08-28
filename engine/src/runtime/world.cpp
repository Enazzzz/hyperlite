#include "engine/runtime/world.hpp"

namespace hyperlite {

World::World() {
	slots_.push_back(Slot{}); // index 0 unused so {0,0} stays invalid
}

Entity World::Create() {
	std::uint32_t index = 0;
	if (!free_.empty()) {
		index = free_.back();
		free_.pop_back();
		Slot& s = slots_[index];
		++s.generation;
		s.alive = true;
		s.enabled = true;
		s.parent = {};
		s.transform = transforms_.Create();
		s.mesh = -1;
		s.material = -1;
		s.layer = 1;
		s.tags = 0;
		s.name.clear();
	} else {
		index = static_cast<std::uint32_t>(slots_.size());
		Slot s{};
		s.generation = 1;
		s.alive = true;
		s.enabled = true;
		s.transform = transforms_.Create();
		slots_.push_back(s);
	}
	live_.push_back(index);
	return {index, slots_[index].generation};
}

void World::Destroy(const Entity e) {
	if (!Alive(e)) {
		return;
	}
	Slot& s = slots_[e.index];
	if (s.transform >= 0) {
		transforms_.Destroy(s.transform);
	}
	s.alive = false;
	s.transform = -1;
	free_.push_back(e.index);
	for (std::size_t i = 0; i < live_.size(); ++i) {
		if (live_[i] == e.index) {
			live_[i] = live_.back();
			live_.pop_back();
			break;
		}
	}
}

bool World::Alive(const Entity e) const {
	return e.index > 0 && e.index < slots_.size() && slots_[e.index].alive &&
		slots_[e.index].generation == e.generation;
}

void World::SetEnabled(const Entity e, const bool enabled) {
	if (Alive(e)) {
		slots_[e.index].enabled = enabled;
	}
}

bool World::Enabled(const Entity e) const {
	return Alive(e) && slots_[e.index].enabled;
}

void World::SetParent(const Entity e, const Entity parent) {
	if (!Alive(e)) {
		return;
	}
	slots_[e.index].parent = Alive(parent) ? parent : Entity{};
	const TransformId child_t = slots_[e.index].transform;
	const TransformId parent_t = Alive(parent) ? slots_[parent.index].transform : -1;
	transforms_.SetParent(child_t, parent_t);
}

Entity World::Parent(const Entity e) const {
	return Alive(e) ? slots_[e.index].parent : Entity{};
}

void World::SetName(const Entity e, std::string name) {
	if (Alive(e)) {
		slots_[e.index].name = std::move(name);
	}
}

const char* World::Name(const Entity e) const {
	return Alive(e) ? slots_[e.index].name.c_str() : "";
}

void World::SetLayer(const Entity e, const std::uint32_t layer) {
	if (Alive(e)) {
		slots_[e.index].layer = layer;
	}
}

std::uint32_t World::Layer(const Entity e) const {
	return Alive(e) ? slots_[e.index].layer : 0;
}

void World::SetTags(const Entity e, const std::uint32_t tags) {
	if (Alive(e)) {
		slots_[e.index].tags = tags;
	}
}

std::uint32_t World::Tags(const Entity e) const {
	return Alive(e) ? slots_[e.index].tags : 0;
}

void World::SetTransform(const Entity e, const TransformId id) {
	if (Alive(e)) {
		slots_[e.index].transform = id;
	}
}

TransformId World::GetTransform(const Entity e) const {
	return Alive(e) ? slots_[e.index].transform : -1;
}

void World::SetMesh(const Entity e, const int mesh_id) {
	if (Alive(e)) {
		slots_[e.index].mesh = mesh_id;
	}
}

int World::Mesh(const Entity e) const {
	return Alive(e) ? slots_[e.index].mesh : -1;
}

void World::SetMaterial(const Entity e, const int material_id) {
	if (Alive(e)) {
		slots_[e.index].material = material_id;
	}
}

int World::Material(const Entity e) const {
	return Alive(e) ? slots_[e.index].material : -1;
}

Entity World::FromIndex(const std::uint32_t index) const {
	if (index == 0 || index >= slots_.size() || !slots_[index].alive) {
		return {};
	}
	return {index, slots_[index].generation};
}

void World::Clear() {
	for (const std::uint32_t idx : live_) {
		if (slots_[idx].transform >= 0) {
			transforms_.Destroy(slots_[idx].transform);
		}
		slots_[idx].alive = false;
	}
	live_.clear();
	free_.clear();
	for (std::uint32_t i = 1; i < slots_.size(); ++i) {
		if (!slots_[i].alive) {
			free_.push_back(i);
		}
	}
}

} // namespace hyperlite
