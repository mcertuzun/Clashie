// Audio stub for web builds — all operations are no-ops.
// Audio support for Emscripten can be added later via Web Audio API or
// miniaudio's Emscripten backend.

#include "pebble/audio/audio.h"

namespace pebble::audio {

bool AudioEngine::init() {
    return true; // Silently succeed — game continues without sound
}

void AudioEngine::shutdown() {
}

void AudioEngine::play(SoundID, float) {
}

void AudioEngine::set_master_volume(float) {
}

void AudioEngine::set_sfx_volume(float) {
}

bool AudioEngine::is_initialized() const {
    return false;
}

} // namespace pebble::audio
