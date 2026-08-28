#include "engine/runtime/macos_audio.hpp"
#include "engine/runtime/audio.hpp"

#import <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <cstring>

namespace hyperlite {
namespace {

AudioQueueRef g_queue = nullptr;
AudioSystem* g_mixer = nullptr;
std::atomic<bool> g_running{false};
constexpr int kSampleRate = 44100;
constexpr int kChannels = 2;
constexpr int kBufferCount = 3;
constexpr int kFramesPerBuffer = 512;

/**
 * AudioQueue output callback: mix the next stereo int16 buffer.
 */
void AudioQueueCallback(void* /*user*/, AudioQueueRef queue, AudioQueueBufferRef buffer) {
	if (!g_running.load(std::memory_order_acquire) || g_mixer == nullptr) {
		std::memset(buffer->mAudioData, 0, buffer->mAudioDataByteSize);
	} else {
		const int frames = static_cast<int>(buffer->mAudioDataByteSize / (kChannels * sizeof(std::int16_t)));
		g_mixer->Mix(static_cast<std::int16_t*>(buffer->mAudioData), frames);
	}
	AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

} // namespace

bool StartMacosAudioOutput(AudioSystem* mixer) {
	if (mixer == nullptr) {
		return false;
	}
	if (g_running.load(std::memory_order_acquire) && g_queue != nullptr) {
		g_mixer = mixer;
		return true;
	}
	StopMacosAudioOutput();

	AudioStreamBasicDescription desc{};
	desc.mSampleRate = kSampleRate;
	desc.mFormatID = kAudioFormatLinearPCM;
	desc.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	desc.mBytesPerPacket = static_cast<UInt32>(kChannels * sizeof(std::int16_t));
	desc.mFramesPerPacket = 1;
	desc.mBytesPerFrame = desc.mBytesPerPacket;
	desc.mChannelsPerFrame = kChannels;
	desc.mBitsPerChannel = 16;

	OSStatus status = AudioQueueNewOutput(
		&desc,
		&AudioQueueCallback,
		nullptr,
		nullptr,
		kCFRunLoopCommonModes,
		0,
		&g_queue);
	if (status != noErr || g_queue == nullptr) {
		g_queue = nullptr;
		return false;
	}

	g_mixer = mixer;
	g_running.store(true, std::memory_order_release);

	const UInt32 bytes = static_cast<UInt32>(kFramesPerBuffer * kChannels * sizeof(std::int16_t));
	for (int i = 0; i < kBufferCount; ++i) {
		AudioQueueBufferRef buffer = nullptr;
		status = AudioQueueAllocateBuffer(g_queue, bytes, &buffer);
		if (status != noErr || buffer == nullptr) {
			StopMacosAudioOutput();
			return false;
		}
		std::memset(buffer->mAudioData, 0, bytes);
		buffer->mAudioDataByteSize = bytes;
		AudioQueueEnqueueBuffer(g_queue, buffer, 0, nullptr);
	}

	status = AudioQueueStart(g_queue, nullptr);
	if (status != noErr) {
		StopMacosAudioOutput();
		return false;
	}
	return true;
}

void StopMacosAudioOutput() {
	g_running.store(false, std::memory_order_release);
	if (g_queue != nullptr) {
		AudioQueueStop(g_queue, true);
		AudioQueueDispose(g_queue, true);
		g_queue = nullptr;
	}
	g_mixer = nullptr;
}

bool MacosAudioOutputRunning() {
	return g_running.load(std::memory_order_acquire) && g_queue != nullptr;
}

} // namespace hyperlite
