#pragma once

#include "engine/runtime/math.hpp"
#include "engine/runtime/transform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Opaque entity handle (index + generation). Invalid = {0,0} is never issued.
 */
struct Entity {
	std::uint32_t index = 0;
	std::uint32_t generation = 0;

	bool operator==(const Entity o) const { return index == o.index && generation == o.generation; }
	bool operator!=(const Entity o) const { return !(*this == o); }
};

inline constexpr Entity kInvalidEntity{};

/**
 * Optional native entity table. Not an ECS mandate — just handles + SoA fields.
 *
 * Programmers who want raw arrays can ignore this and use TransformStore / meshes directly.
 */
class World {
public:
	World();

	Entity Create();
	void Destroy(const Entity e);
	bool Alive(const Entity e) const;

	void SetEnabled(const Entity e, const bool enabled);
	bool Enabled(const Entity e) const;

	void SetParent(const Entity e, const Entity parent);
	Entity Parent(const Entity e) const;

	void SetName(const Entity e, std::string name);
	const char* Name(const Entity e) const;

	void SetLayer(const Entity e, const std::uint32_t layer);
	std::uint32_t Layer(const Entity e) const;
	void SetTags(const Entity e, const std::uint32_t tags);
	std::uint32_t Tags(const Entity e) const;

	void SetTransform(const Entity e, const TransformId id);
	TransformId GetTransform(const Entity e) const;

	void SetMesh(const Entity e, const int mesh_id);
	int Mesh(const Entity e) const;
	void SetMaterial(const Entity e, const int material_id);
	int Material(const Entity e) const;

	TransformStore& Transforms() { return transforms_; }
	const TransformStore& Transforms() const { return transforms_; }

	/** Dense list of live entity indices for batch iteration (C++ only). */
	const std::vector<std::uint32_t>& Live() const { return live_; }

	std::size_t LiveCount() const { return live_.size(); }

	Entity FromIndex(const std::uint32_t index) const;

	void Clear();

private:
	struct Slot {
		std::uint32_t generation = 1;
		bool alive = false;
		bool enabled = true;
		Entity parent{};
		TransformId transform = -1;
		int mesh = -1;
		int material = -1;
		std::uint32_t layer = 1;
		std::uint32_t tags = 0;
		std::string name{};
	};

	std::vector<Slot> slots_{};
	std::vector<std::uint32_t> free_{};
	std::vector<std::uint32_t> live_{};
	TransformStore transforms_{};
};

/**
 * Named world container. Loading/unloading is explicit — no hidden ticks.
 */
class Scene {
public:
	explicit Scene(std::string name = "scene") : name_(std::move(name)) {}

	World& GetWorld() { return world_; }
	const World& GetWorld() const { return world_; }
	const std::string& Name() const { return name_; }

	void Unload() { world_.Clear(); }

private:
	std::string name_{};
	World world_{};
};

} // namespace hyperlite
