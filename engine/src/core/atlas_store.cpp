#include "engine/atlas_store.hpp"

#include <cstring>

namespace hyperlite {

int AtlasStore::Load(const std::uint8_t* src, const std::size_t bytes, const int width, const int height) {
	if (src == nullptr || width <= 0 || height <= 0) {
		return -1;
	}
	const std::size_t required = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
	if (bytes < required) {
		return -1;
	}
	for (std::size_t i = 0U; i < entries_.size(); ++i) {
		const AtlasEntry& existing = entries_[i];
		if (existing.width == width && existing.height == height && existing.pixels.size() == required &&
			std::memcmp(existing.pixels.data(), src, required) == 0) {
			return static_cast<int>(i);
		}
	}
	AtlasEntry entry{};
	entry.width = width;
	entry.height = height;
	entry.pixels.assign(src, src + required);
	const int handle = static_cast<int>(entries_.size());
	entries_.push_back(std::move(entry));
	return handle;
}

const AtlasEntry* AtlasStore::Get(const int handle) const {
	if (handle < 0 || static_cast<std::size_t>(handle) >= entries_.size()) {
		return nullptr;
	}
	return &entries_[static_cast<std::size_t>(handle)];
}

} // namespace hyperlite
