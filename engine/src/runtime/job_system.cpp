#include "engine/runtime/job_system.hpp"

#include <chrono>
#include <cstdint>
#include <thread>

namespace hyperlite {

void JobSystem::Start(const unsigned int worker_count) {
	if (started_) {
		return;
	}
	stop_.store(false, std::memory_order_relaxed);
	unsigned int n = worker_count;
	if (n == 0) {
		n = std::thread::hardware_concurrency();
	}
	if (n < 1U) {
		n = 1U;
	}
	worker_count_ = n;
	started_ = true;
	workers_.reserve(n);
	for (unsigned int i = 0; i < n; ++i) {
		workers_.emplace_back([this]() { WorkerLoop(); });
	}
}

void JobSystem::Shutdown() {
	if (!started_) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_.store(true, std::memory_order_relaxed);
	}
	cv_.notify_all();
	for (auto& t : workers_) {
		if (t.joinable()) {
			t.join();
		}
	}
	workers_.clear();
	started_ = false;
	worker_count_ = 0;
}

void JobSystem::Submit(const Job job) {
	if (job.fn == nullptr) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.push(job);
		outstanding_.fetch_add(1, std::memory_order_relaxed);
	}
	cv_.notify_one();
}

void JobSystem::SubmitBatch(void (*fn)(void*), void* base, const std::size_t count, const std::size_t stride) {
	if (fn == nullptr || count == 0U) {
		return;
	}
	auto* bytes = static_cast<std::uint8_t*>(base);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		for (std::size_t i = 0; i < count; ++i) {
			Job job{};
			job.fn = fn;
			job.user = bytes + i * stride;
			queue_.push(job);
		}
		outstanding_.fetch_add(static_cast<int>(count), std::memory_order_relaxed);
	}
	cv_.notify_all();
}

void JobSystem::WaitIdle() {
	while (outstanding_.load(std::memory_order_acquire) > 0) {
		if (!TryHelp()) {
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait_for(lock, std::chrono::milliseconds(1), [this]() {
				return outstanding_.load(std::memory_order_relaxed) <= 0 || !queue_.empty() ||
					stop_.load(std::memory_order_relaxed);
			});
		}
	}
}

bool JobSystem::TryHelp() {
	Job job{};
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (queue_.empty()) {
			return false;
		}
		job = queue_.front();
		queue_.pop();
	}
	if (job.fn != nullptr) {
		job.fn(job.user);
	}
	outstanding_.fetch_sub(1, std::memory_order_acq_rel);
	cv_.notify_all();
	return true;
}

void JobSystem::WorkerLoop() {
	while (true) {
		Job job{};
		{
			std::unique_lock<std::mutex> lock(mutex_);
			cv_.wait(lock, [this]() {
				return stop_.load(std::memory_order_relaxed) || !queue_.empty();
			});
			if (stop_.load(std::memory_order_relaxed) && queue_.empty()) {
				return;
			}
			job = queue_.front();
			queue_.pop();
		}
		if (job.fn != nullptr) {
			job.fn(job.user);
		}
		outstanding_.fetch_sub(1, std::memory_order_acq_rel);
		cv_.notify_all();
	}
}

} // namespace hyperlite
