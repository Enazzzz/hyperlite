#include "engine/runtime/profiler.hpp"

namespace hyperlite {

void Profiler::Begin(const Section s) {
	if (s < 0 || s >= Section::Count) {
		return;
	}
	starts_[static_cast<std::size_t>(s)] = Clock::now();
	open_[static_cast<std::size_t>(s)] = true;
}

void Profiler::End(const Section s) {
	if (s < 0 || s >= Section::Count || !open_[static_cast<std::size_t>(s)]) {
		return;
	}
	const auto now = Clock::now();
	ms_[static_cast<std::size_t>(s)] = std::chrono::duration<float, std::milli>(now - starts_[static_cast<std::size_t>(s)]).count();
	open_[static_cast<std::size_t>(s)] = false;
}

void Profiler::NextFrame() {
	for (int i = 0; i < Section::Count; ++i) {
		if (!open_[static_cast<std::size_t>(i)]) {
			ms_[static_cast<std::size_t>(i)] = 0.0f;
		}
	}
}

float Profiler::Ms(const Section s) const {
	if (s < 0 || s >= Section::Count) {
		return 0.0f;
	}
	return ms_[static_cast<std::size_t>(s)];
}

} // namespace hyperlite
