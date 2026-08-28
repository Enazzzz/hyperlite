#pragma once

#include "engine/runtime/job_system.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyperlite {

enum class AssetKind : int { Unknown = 0, Mesh = 1, Texture = 2, Audio = 3, Font = 4, Raw = 5 };

struct AssetRecord {
	int handle = -1;
	AssetKind kind = AssetKind::Unknown;
	std::string path{};
	std::uint64_t bytes = 0;
	int refcount = 0;
	bool ready = false;
	bool stale = false;
};

/**
 * Handle table, cache, async load via JobSystem, unload, hot-reload flag, memory tracking.
 */
class AssetManager {
public:
	int Intern(const char* path, const AssetKind kind);
	AssetRecord* Get(const int handle);
	const AssetRecord* Get(const int handle) const;

	void AddRef(const int handle);
	void Release(const int handle);

	void RequestLoad(const int handle, JobSystem& jobs, void (*loader)(AssetRecord*, void*), void* user);
	void Unload(const int handle);

	void MarkStale(const int handle);
	void MarkStalePath(const char* path);
	int HotReloadCount() const { return hot_reload_; }

	std::uint64_t Bytes() const { return bytes_; }
	std::size_t Count() const { return records_.size(); }

private:
	std::vector<AssetRecord> records_{};
	std::unordered_map<std::string, int> by_path_{};
	std::uint64_t bytes_ = 0;
	int hot_reload_ = 0;
};

} // namespace hyperlite
