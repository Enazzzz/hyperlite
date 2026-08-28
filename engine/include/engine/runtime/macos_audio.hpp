#pragma once

namespace hyperlite {

class AudioSystem;

/**
 * Start a Core Audio / AudioQueue output that pulls stereo int16 from mixer.
 *
 * The queue callback calls AudioSystem::Mix on a realtime thread; Mix is
 * mutex-protected. Returns false when the device cannot be opened.
 */
bool StartMacosAudioOutput(AudioSystem* mixer);

/** Stop the output queue and wait for in-flight callbacks to finish. */
void StopMacosAudioOutput();

/** True while the macOS output queue is running. */
bool MacosAudioOutputRunning();

} // namespace hyperlite
