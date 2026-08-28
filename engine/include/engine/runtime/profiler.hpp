#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Named CPU section timer. Begin/End pairs; never runs unless the programmer calls them.
 */
class Profiler {
public:
	enum Section : int {
		Frame = 0,
		Input,
		Transform,
		Culling,
		Physics,
		Ai,
		Animation,
		Particles,
		Render,
		Streaming,
		Audio,
		Ui,
		Count
	};

	void Begin(const Section s);
	void End(const Section s);
	void NextFrame();

	float Ms(const Section s) const;
	float FrameMs() const { return Ms(Frame); }

	struct Counters {
		std::uint64_t triangles = 0;
		std::uint64_t draw_calls = 0;
		std::uint64_t visible_meshes = 0;
		std::uint64_t culled_meshes = 0;
		std::uint64_t hiz_rejects = 0;
		std::uint64_t tiles_touched = 0;
		float raster_ms = 0.0f;
	};

	Counters& Stats() { return stats_; }
	const Counters& Stats() const { return stats_; }
	void ResetStats() { stats_ = {}; }

private:
	using Clock = std::chrono::steady_clock;
	std::array<Clock::time_point, Section::Count> starts_{};
	std::array<float, Section::Count> ms_{};
	std::array<bool, Section::Count> open_{};
	Counters stats_{};
};

} // namespace hyperlite
