#include "engine/runtime/assets.hpp"

namespace hyperlite {

int AssetManager::Intern(const char* path, const AssetKind kind) {
	if (path == nullptr) {
		return -1;
	}
	auto it = by_path_.find(path);
	if (it != by_path_.end()) {
		return it->second;
	}
	AssetRecord rec{};
	rec.handle = static_cast<int>(records_.size());
	rec.kind = kind;
	rec.path = path;
	records_.push_back(rec);
	by_path_[path] = rec.handle;
	return rec.handle;
}

AssetRecord* AssetManager::Get(const int handle) {
	if (handle < 0 || static_cast<std::size_t>(handle) >= records_.size()) {
		return nullptr;
	}
	return &records_[static_cast<std::size_t>(handle)];
}

const AssetRecord* AssetManager::Get(const int handle) const {
	if (handle < 0 || static_cast<std::size_t>(handle) >= records_.size()) {
		return nullptr;
	}
	return &records_[static_cast<std::size_t>(handle)];
}

void AssetManager::AddRef(const int handle) {
	AssetRecord* r = Get(handle);
	if (r != nullptr) {
		++r->refcount;
	}
}

void AssetManager::Release(const int handle) {
	AssetRecord* r = Get(handle);
	if (r != nullptr && r->refcount > 0) {
		--r->refcount;
		if (r->refcount == 0) {
			Unload(handle);
		}
	}
}

void AssetManager::RequestLoad(const int handle, JobSystem& jobs, void (*loader)(AssetRecord*, void*), void* user) {
	AssetRecord* r = Get(handle);
	if (r == nullptr || loader == nullptr) {
		return;
	}
	struct Job {
		void (*loader)(AssetRecord*, void*);
		AssetRecord* rec;
		void* user;
	};
	// Run inline if the pool is not started; otherwise still run inline because
	// Job userdata must outlive the job. Callers who need async wrap this themselves.
	(void)jobs;
	loader(r, user);
	r->ready = true;
	bytes_ += r->bytes;
}

void AssetManager::Unload(const int handle) {
	AssetRecord* r = Get(handle);
	if (r == nullptr) {
		return;
	}
	if (bytes_ >= r->bytes) {
		bytes_ -= r->bytes;
	}
	r->ready = false;
	r->bytes = 0;
}

void AssetManager::MarkStale(const int handle) {
	AssetRecord* r = Get(handle);
	if (r != nullptr) {
		r->stale = true;
		++hot_reload_;
	}
}

void AssetManager::MarkStalePath(const char* path) {
	if (path == nullptr) {
		return;
	}
	auto it = by_path_.find(path);
	if (it != by_path_.end()) {
		MarkStale(it->second);
	}
}

} // namespace hyperlite
