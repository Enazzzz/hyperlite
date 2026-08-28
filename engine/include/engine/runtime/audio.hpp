#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * PCM clip (mono or stereo int16). Loaded from WAV or created from samples.
 */
struct AudioClip {
	std::vector<std::int16_t> samples{};
	int sample_rate = 44100;
	int channels = 1;
	std::string path{};
};

/**
 * Mixer channel.
 */
struct AudioChannel {
	int clip = -1;
	int cursor = 0;
	float volume = 1.0f;
	float pan = 0.0f;
	bool loop = false;
	bool playing = false;
	bool spatial = false;
	Vec3 position{};
};

/**
 * Software mixer + spatial listener. Mix() produces interleaved int16 for the host.
 *
 * Mix and channel mutations are mutex-protected so a platform output callback
 * (Core Audio on macOS) can pull samples from another thread.
 *
 * StartOutput() opens a platform device when one exists (Core Audio on macOS).
 * Other platforms keep Mix()-only output until a host backend is added.
 */
class AudioSystem {
public:
	AudioSystem() = default;
	~AudioSystem();

	AudioSystem(const AudioSystem&) = delete;
	AudioSystem& operator=(const AudioSystem&) = delete;
	int LoadWav(const char* path);
	int CreateClip(const std::int16_t* samples, const int count, const int sample_rate, const int channels);
	const AudioClip* GetClip(const int id) const;

	int Play(const int clip_id, const float volume = 1.0f, const bool loop = false);
	int PlaySpatial(const int clip_id, const Vec3 position, const float volume = 1.0f, const bool loop = false);
	void Stop(const int channel);
	void SetVolume(const int channel, const float volume);
	void SetLoop(const int channel, const bool loop);

	void SetListener(const Vec3 position, const Vec3 forward, const Vec3 up);
	Vec3 ListenerPosition() const { return listener_pos_; }

	/**
	 * Mix `frames` of stereo int16 into out (frames * 2 samples).
	 *
	 * Thread-safe with Play/Stop and with a platform output callback.
	 */
	void Mix(std::int16_t* out, const int frames);

	/**
	 * Open the platform output device (Core Audio on macOS). No-op elsewhere.
	 *
	 * Safe to call more than once. Returns true when a device is running.
	 */
	bool StartOutput();

	/** Stop platform output. Mix() remains available. */
	void StopOutput();

	/** True while a platform output device is pulling Mix(). */
	bool OutputRunning() const;

	int PlayingCount() const;

private:
	/** Mix while mix_mutex_ is already held. */
	void MixLocked(std::int16_t* out, const int frames);

	std::vector<AudioClip> clips_{};
	std::vector<AudioChannel> channels_{};
	Vec3 listener_pos_{};
	Vec3 listener_fwd_{0.0f, 0.0f, -1.0f};
	Vec3 listener_up_{0.0f, 1.0f, 0.0f};
	mutable std::mutex mix_mutex_{};
	bool output_running_ = false;
};

} // namespace hyperlite
