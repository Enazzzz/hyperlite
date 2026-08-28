#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace hyperlite {

/**
 * One unit of native work executed by the job pool.
 *
 * Must not require the Python interpreter. Callers own userdata lifetime.
 */
struct Job {
	void (*fn)(void*) = nullptr;
	void* user = nullptr;
};

/**
 * Native worker pool. Optional — Game creates one; Engine raster paths are unchanged.
 *
 * Hidden per-frame work is never submitted; programmers enqueue jobs explicitly.
 */
class JobSystem {
public:
	JobSystem() = default;
	~JobSystem() { Shutdown(); }

	JobSystem(const JobSystem&) = delete;
	JobSystem& operator=(const JobSystem&) = delete;

	/**
	 * Start worker threads (0 = hardware_concurrency, at least 1 extra worker).
	 */
	void Start(const unsigned int worker_count = 0);

	/**
	 * Drain remaining jobs and join workers.
	 */
	void Shutdown();

	/**
	 * Enqueue one job. Safe from any thread after Start().
	 */
	void Submit(const Job job);

	/**
	 * Enqueue count identical jobs with userdata = base + i * stride.
	 */
	void SubmitBatch(void (*fn)(void*), void* base, const std::size_t count, const std::size_t stride);

	/**
	 * Block until the outstanding job counter reaches zero.
	 */
	void WaitIdle();

	/**
	 * Run one job on the calling thread if the queue is non-empty (work-stealing helper).
	 */
	bool TryHelp();

	/**
	 * Worker thread count (0 before Start).
	 */
	unsigned int WorkerCount() const { return worker_count_; }

	/**
	 * Outstanding jobs (submitted minus completed).
	 */
	int Outstanding() const { return outstanding_.load(std::memory_order_relaxed); }

private:
	void WorkerLoop();

	std::mutex mutex_{};
	std::condition_variable cv_{};
	std::queue<Job> queue_{};
	std::vector<std::thread> workers_{};
	std::atomic<int> outstanding_{0};
	std::atomic<bool> stop_{false};
	unsigned int worker_count_ = 0;
	bool started_ = false;
};

} // namespace hyperlite
