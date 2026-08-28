#include "engine/runtime/audio.hpp"

#ifdef __APPLE__
#include "engine/runtime/macos_audio.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hyperlite {

AudioSystem::~AudioSystem() {
	StopOutput();
}

int AudioSystem::LoadWav(const char* path) {
	if (path == nullptr) {
		return -1;
	}
	FILE* f = std::fopen(path, "rb");
	if (f == nullptr) {
		return -1;
	}
	char riff[12];
	if (std::fread(riff, 1, 12, f) != 12 || std::memcmp(riff, "RIFF", 4) != 0) {
		std::fclose(f);
		return -1;
	}
	std::uint16_t audio_format = 0;
	std::uint16_t channels = 0;
	std::uint32_t sample_rate = 0;
	std::uint16_t bits = 0;
	std::vector<std::int16_t> pcm;
	while (!std::feof(f)) {
		char id[4];
		std::uint32_t size = 0;
		if (std::fread(id, 1, 4, f) != 4 || std::fread(&size, 4, 1, f) != 1) {
			break;
		}
		if (std::memcmp(id, "fmt ", 4) == 0) {
			std::uint32_t byte_rate = 0;
			std::uint16_t block_align = 0;
			if (std::fread(&audio_format, 2, 1, f) != 1 ||
				std::fread(&channels, 2, 1, f) != 1 ||
				std::fread(&sample_rate, 4, 1, f) != 1 ||
				std::fread(&byte_rate, 4, 1, f) != 1 ||
				std::fread(&block_align, 2, 1, f) != 1 ||
				std::fread(&bits, 2, 1, f) != 1) {
				std::fclose(f);
				return -1;
			}
			if (size > 16) {
				std::fseek(f, static_cast<long>(size - 16), SEEK_CUR);
			}
		} else if (std::memcmp(id, "data", 4) == 0) {
			std::vector<std::uint8_t> raw(size);
			if (size > 0 && std::fread(raw.data(), 1, size, f) != size) {
				std::fclose(f);
				return -1;
			}
			if (bits == 16) {
				pcm.resize(size / 2);
				std::memcpy(pcm.data(), raw.data(), size);
			} else if (bits == 8) {
				pcm.resize(size);
				for (std::uint32_t i = 0; i < size; ++i) {
					pcm[i] = static_cast<std::int16_t>((static_cast<int>(raw[i]) - 128) * 256);
				}
			}
			break;
		} else {
			std::fseek(f, static_cast<long>(size), SEEK_CUR);
		}
	}
	std::fclose(f);
	if (pcm.empty() || audio_format != 1) {
		return -1;
	}
	AudioClip clip{};
	clip.samples = std::move(pcm);
	clip.sample_rate = static_cast<int>(sample_rate);
	clip.channels = static_cast<int>(channels);
	clip.path = path;
	std::lock_guard<std::mutex> lock(mix_mutex_);
	clips_.push_back(std::move(clip));
	return static_cast<int>(clips_.size()) - 1;
}

int AudioSystem::CreateClip(
	const std::int16_t* samples,
	const int count,
	const int sample_rate,
	const int channels) {
	if (samples == nullptr || count <= 0) {
		return -1;
	}
	AudioClip clip{};
	clip.samples.assign(samples, samples + count);
	clip.sample_rate = sample_rate;
	clip.channels = std::max(1, channels);
	std::lock_guard<std::mutex> lock(mix_mutex_);
	clips_.push_back(std::move(clip));
	return static_cast<int>(clips_.size()) - 1;
}

const AudioClip* AudioSystem::GetClip(const int id) const {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	if (id < 0 || static_cast<std::size_t>(id) >= clips_.size()) {
		return nullptr;
	}
	return &clips_[static_cast<std::size_t>(id)];
}

int AudioSystem::Play(const int clip_id, const float volume, const bool loop) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	if (clip_id < 0 || static_cast<std::size_t>(clip_id) >= clips_.size()) {
		return -1;
	}
	AudioChannel ch{};
	ch.clip = clip_id;
	ch.volume = volume;
	ch.loop = loop;
	ch.playing = true;
	channels_.push_back(ch);
	return static_cast<int>(channels_.size()) - 1;
}

int AudioSystem::PlaySpatial(const int clip_id, const Vec3 position, const float volume, const bool loop) {
	const int id = Play(clip_id, volume, loop);
	if (id >= 0) {
		std::lock_guard<std::mutex> lock(mix_mutex_);
		if (static_cast<std::size_t>(id) < channels_.size()) {
			channels_[static_cast<std::size_t>(id)].spatial = true;
			channels_[static_cast<std::size_t>(id)].position = position;
		}
	}
	return id;
}

void AudioSystem::Stop(const int channel) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	if (channel >= 0 && static_cast<std::size_t>(channel) < channels_.size()) {
		channels_[static_cast<std::size_t>(channel)].playing = false;
	}
}

void AudioSystem::SetVolume(const int channel, const float volume) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	if (channel >= 0 && static_cast<std::size_t>(channel) < channels_.size()) {
		channels_[static_cast<std::size_t>(channel)].volume = volume;
	}
}

void AudioSystem::SetLoop(const int channel, const bool loop) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	if (channel >= 0 && static_cast<std::size_t>(channel) < channels_.size()) {
		channels_[static_cast<std::size_t>(channel)].loop = loop;
	}
}

void AudioSystem::SetListener(const Vec3 position, const Vec3 forward, const Vec3 up) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	listener_pos_ = position;
	listener_fwd_ = Normalize(forward);
	listener_up_ = Normalize(up);
}

int AudioSystem::PlayingCount() const {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	int n = 0;
	for (const auto& c : channels_) {
		if (c.playing) {
			++n;
		}
	}
	return n;
}

bool AudioSystem::StartOutput() {
#ifdef __APPLE__
	{
		std::lock_guard<std::mutex> lock(mix_mutex_);
		if (output_running_) {
			return true;
		}
	}
	// Do not hold mix_mutex_ here: the AudioQueue callback calls Mix() immediately.
	const bool ok = StartMacosAudioOutput(this);
	std::lock_guard<std::mutex> lock(mix_mutex_);
	output_running_ = ok;
	return ok;
#else
	return false;
#endif
}

void AudioSystem::StopOutput() {
#ifdef __APPLE__
	StopMacosAudioOutput();
	std::lock_guard<std::mutex> lock(mix_mutex_);
	output_running_ = false;
#else
	output_running_ = false;
#endif
}

bool AudioSystem::OutputRunning() const {
	std::lock_guard<std::mutex> lock(mix_mutex_);
#ifdef __APPLE__
	return output_running_ && MacosAudioOutputRunning();
#else
	return false;
#endif
}

void AudioSystem::Mix(std::int16_t* out, const int frames) {
	std::lock_guard<std::mutex> lock(mix_mutex_);
	MixLocked(out, frames);
}

void AudioSystem::MixLocked(std::int16_t* out, const int frames) {
	if (out == nullptr || frames <= 0) {
		return;
	}
	std::fill(out, out + frames * 2, static_cast<std::int16_t>(0));
	for (AudioChannel& ch : channels_) {
		if (!ch.playing) {
			continue;
		}
		if (ch.clip < 0 || static_cast<std::size_t>(ch.clip) >= clips_.size()) {
			ch.playing = false;
			continue;
		}
		const AudioClip* clip = &clips_[static_cast<std::size_t>(ch.clip)];
		if (clip->samples.empty()) {
			ch.playing = false;
			continue;
		}
		float gain_l = ch.volume;
		float gain_r = ch.volume;
		if (ch.spatial) {
			const Vec3 to = ch.position - listener_pos_;
			const float dist = std::max(0.5f, Length(to));
			const float atten = Clamp(1.0f / dist, 0.0f, 1.0f);
			const Vec3 right = Normalize(Cross(listener_fwd_, listener_up_));
			const float pan = Clamp(Dot(Normalize(to), right), -1.0f, 1.0f);
			gain_l = ch.volume * atten * (1.0f - pan) * 0.5f;
			gain_r = ch.volume * atten * (1.0f + pan) * 0.5f;
		}
		const int nch = std::max(1, clip->channels);
		for (int f = 0; f < frames; ++f) {
			if (ch.cursor >= static_cast<int>(clip->samples.size())) {
				if (ch.loop) {
					ch.cursor = 0;
				} else {
					ch.playing = false;
					break;
				}
			}
			std::int16_t sl = clip->samples[static_cast<std::size_t>(ch.cursor)];
			std::int16_t sr = sl;
			if (nch > 1 && ch.cursor + 1 < static_cast<int>(clip->samples.size())) {
				sr = clip->samples[static_cast<std::size_t>(ch.cursor + 1)];
			}
			int ol = static_cast<int>(out[f * 2]) + static_cast<int>(static_cast<float>(sl) * gain_l);
			int orr = static_cast<int>(out[f * 2 + 1]) + static_cast<int>(static_cast<float>(sr) * gain_r);
			out[f * 2] = static_cast<std::int16_t>(Clamp(static_cast<float>(ol), -32768.0f, 32767.0f));
			out[f * 2 + 1] = static_cast<std::int16_t>(Clamp(static_cast<float>(orr), -32768.0f, 32767.0f));
			ch.cursor += nch;
		}
	}
}

} // namespace hyperlite
