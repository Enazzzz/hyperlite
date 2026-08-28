#pragma once

#include "engine/runtime/job_system.hpp"
#include "engine/runtime/math.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyperlite {

struct ChunkId {
	std::int32_t x = 0;
	std::int32_t y = 0;
	std::int32_t z = 0;

	bool operator==(const ChunkId o) const { return x == o.x && y == o.y && z == o.z; }
};

struct ChunkIdHash {
	std::size_t operator()(const ChunkId id) const {
		return (static_cast<std::size_t>(id.x) * 73856093u) ^
			(static_cast<std::size_t>(id.y) * 19349663u) ^
			(static_cast<std::size_t>(id.z) * 83492791u);
	}
};

/**
 * Native procedural generation hook. Runs on a worker thread; no Python.
 */
using ChunkGenerateFn = void (*)(const ChunkId, void* payload, void* user);

/**
 * Streaming callback (load/unload completed). Native only.
 */
using ChunkEventFn = void (*)(const ChunkId, const bool loaded, void* user);

struct ChunkPayload {
	std::vector<std::uint8_t> bytes{};
	Aabb bounds{};
	bool ready = false;
};

/**
 * Chunk streaming with memory budget and async generation via JobSystem.
 *
 * No hidden per-frame processing: call Pump() / RequestRadius() explicitly.
 */
class WorldStreamer {
public:
	void SetMemoryLimit(const std::uint64_t bytes) { memory_limit_ = bytes; }
	std::uint64_t MemoryUsed() const { return memory_used_; }
	std::uint64_t MemoryLimit() const { return memory_limit_; }

	void SetGenerateFn(ChunkGenerateFn fn, void* user) {
		generate_ = fn;
		generate_user_ = user;
	}
	void SetEventFn(ChunkEventFn fn, void* user) {
		event_ = fn;
		event_user_ = user;
	}

	void Request(const ChunkId id);
	void Unload(const ChunkId id);
	void RequestRadius(const ChunkId center, const int radius);

	const ChunkPayload* Get(const ChunkId id) const;
	bool IsLoaded(const ChunkId id) const;

	/**
	 * Collect finished jobs and enforce the memory limit (LRU unload).
	 */
	void Pump(JobSystem& jobs);

	std::size_t LoadedCount() const { return loaded_.size(); }

private:
	struct Entry {
		ChunkId id{};
		ChunkPayload payload{};
		bool loading = false;
		bool loaded = false;
		std::uint64_t last_use = 0;
	};

	std::unordered_map<ChunkId, Entry, ChunkIdHash> loaded_{};
	std::vector<ChunkId> pending_{};
	ChunkGenerateFn generate_ = nullptr;
	void* generate_user_ = nullptr;
	ChunkEventFn event_ = nullptr;
	void* event_user_ = nullptr;
	std::uint64_t memory_limit_ = 64ull * 1024ull * 1024ull;
	std::uint64_t memory_used_ = 0;
	std::uint64_t clock_ = 0;
};

} // namespace hyperlite
