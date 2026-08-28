#include "engine/runtime/streaming.hpp"

namespace hyperlite {

void WorldStreamer::Request(const ChunkId id) {
	auto it = loaded_.find(id);
	if (it != loaded_.end() && (it->second.loaded || it->second.loading)) {
		it->second.last_use = ++clock_;
		return;
	}
	Entry e{};
	e.id = id;
	e.loading = true;
	e.last_use = ++clock_;
	loaded_[id] = e;
	pending_.push_back(id);
}

void WorldStreamer::Unload(const ChunkId id) {
	auto it = loaded_.find(id);
	if (it == loaded_.end()) {
		return;
	}
	memory_used_ -= it->second.payload.bytes.size();
	if (event_ != nullptr) {
		event_(id, false, event_user_);
	}
	loaded_.erase(it);
}

void WorldStreamer::RequestRadius(const ChunkId center, const int radius) {
	for (int z = -radius; z <= radius; ++z) {
		for (int y = -radius; y <= radius; ++y) {
			for (int x = -radius; x <= radius; ++x) {
				Request({center.x + x, center.y + y, center.z + z});
			}
		}
	}
}

const ChunkPayload* WorldStreamer::Get(const ChunkId id) const {
	auto it = loaded_.find(id);
	if (it == loaded_.end() || !it->second.loaded) {
		return nullptr;
	}
	return &it->second.payload;
}

bool WorldStreamer::IsLoaded(const ChunkId id) const {
	auto it = loaded_.find(id);
	return it != loaded_.end() && it->second.loaded;
}

namespace {

struct ChunkJob {
	ChunkGenerateFn fn = nullptr;
	void* user = nullptr;
	ChunkId id{};
	ChunkPayload* payload = nullptr;
};

void RunChunkJob(void* user) {
	auto* job = static_cast<ChunkJob*>(user);
	if (job->fn != nullptr) {
		job->fn(job->id, job->payload, job->user);
	}
	if (job->payload != nullptr) {
		job->payload->ready = true;
	}
}

} // namespace

void WorldStreamer::Pump(JobSystem& jobs) {
	std::vector<ChunkJob> job_data;
	job_data.reserve(pending_.size());
	for (const ChunkId id : pending_) {
		auto it = loaded_.find(id);
		if (it == loaded_.end()) {
			continue;
		}
		ChunkJob job{};
		job.fn = generate_;
		job.user = generate_user_;
		job.id = id;
		job.payload = &it->second.payload;
		job_data.push_back(job);
	}
	if (!jobs.WorkerCount()) {
		for (auto& job : job_data) {
			RunChunkJob(&job);
		}
	} else {
		for (auto& job : job_data) {
			jobs.Submit(Job{&RunChunkJob, &job});
		}
		jobs.WaitIdle();
	}
	for (const ChunkId id : pending_) {
		auto it = loaded_.find(id);
		if (it == loaded_.end()) {
			continue;
		}
		it->second.loading = false;
		it->second.loaded = true;
		it->second.payload.ready = true;
		memory_used_ += it->second.payload.bytes.size();
		if (event_ != nullptr) {
			event_(id, true, event_user_);
		}
	}
	pending_.clear();
	while (memory_used_ > memory_limit_ && !loaded_.empty()) {
		ChunkId oldest = loaded_.begin()->first;
		std::uint64_t oldest_use = loaded_.begin()->second.last_use;
		for (const auto& kv : loaded_) {
			if (kv.second.last_use < oldest_use) {
				oldest_use = kv.second.last_use;
				oldest = kv.first;
			}
		}
		Unload(oldest);
	}
}

} // namespace hyperlite
