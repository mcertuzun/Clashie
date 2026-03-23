#define MA_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio.h"

#include "pebble/audio/audio.h"
#include "pebble/platform/platform.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <new>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pebble::audio {

// --- Procedural sound generation helpers ---

static constexpr int SAMPLE_RATE = 44100;
static constexpr int MAX_SIMULTANEOUS = 16;

struct SoundBuffer {
    int16_t* data    = nullptr;
    uint32_t frames  = 0; // number of samples (mono)
};

// Simple envelope: linear attack then exponential decay
static float envelope(float t, float total, float attack = 0.005f) {
    if (t < attack) return t / attack;
    float decay_t = (t - attack) / (total - attack);
    return (1.0f - decay_t) * std::exp(-3.0f * decay_t);
}

// White noise [-1, 1]
static float white_noise(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u; // LCG
    return ((float)(seed >> 8) / (float)(1 << 24)) * 2.0f - 1.0f;
}

// Clamp and convert float [-1,1] to int16
static int16_t to_s16(float s) {
    if (s > 1.0f)  s = 1.0f;
    if (s < -1.0f) s = -1.0f;
    return (int16_t)(s * 32767.0f);
}

static SoundBuffer generate_sound(SoundID id) {
    SoundBuffer buf;
    uint32_t rng_seed = 42;

    float duration = 0.1f; // seconds, default
    auto alloc = [&](float dur) {
        duration = dur;
        buf.frames = (uint32_t)(dur * SAMPLE_RATE);
        buf.data = (int16_t*)std::calloc(buf.frames, sizeof(int16_t));
    };

    switch (id) {
        case SoundID::CANNON_FIRE: {
            // Short low-frequency noise burst (100ms, low pitch)
            alloc(0.1f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.002f);
                float noise = white_noise(rng_seed);
                float tone = std::sin(2.0f * (float)M_PI * 80.0f * t);
                buf.data[i] = to_s16((noise * 0.6f + tone * 0.4f) * env * 0.9f);
            }
            break;
        }

        case SoundID::ARROW_SHOOT: {
            // Quick high-frequency chirp (50ms, high to low sweep)
            alloc(0.05f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.001f);
                float freq = 3000.0f - 2000.0f * (t / duration); // sweep 3000->1000
                float phase = 2.0f * (float)M_PI * freq * t;
                buf.data[i] = to_s16(std::sin(phase) * env * 0.5f);
            }
            break;
        }

        case SoundID::MORTAR_LAUNCH: {
            // Deep thud + rising whistle (200ms)
            alloc(0.2f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.003f);
                // Low thud
                float thud = std::sin(2.0f * (float)M_PI * 50.0f * t) *
                             std::exp(-t * 20.0f);
                // Rising whistle
                float freq = 200.0f + 800.0f * (t / duration);
                float whistle = std::sin(2.0f * (float)M_PI * freq * t) *
                                (t / duration) * 0.5f;
                buf.data[i] = to_s16((thud * 0.6f + whistle * 0.4f) * env * 0.8f);
            }
            break;
        }

        case SoundID::BUILDING_DESTROYED: {
            // Longer noise burst with decay (300ms, mid freq)
            alloc(0.3f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.005f);
                float noise = white_noise(rng_seed);
                float rumble = std::sin(2.0f * (float)M_PI * 120.0f * t);
                float crackle = white_noise(rng_seed) * std::exp(-t * 8.0f);
                buf.data[i] = to_s16((noise * 0.3f + rumble * 0.3f + crackle * 0.4f) * env * 0.85f);
            }
            break;
        }

        case SoundID::WALL_BREAK: {
            // Short crunch (100ms, noise)
            alloc(0.1f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.001f);
                float noise = white_noise(rng_seed);
                // Add some low rumble
                float lo = std::sin(2.0f * (float)M_PI * 150.0f * t);
                buf.data[i] = to_s16((noise * 0.7f + lo * 0.3f) * env * 0.7f);
            }
            break;
        }

        case SoundID::TROOP_DEATH: {
            // Quick descending tone (100ms)
            alloc(0.1f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.002f);
                float freq = 600.0f - 400.0f * (t / duration); // 600->200 Hz
                buf.data[i] = to_s16(std::sin(2.0f * (float)M_PI * freq * t) * env * 0.5f);
            }
            break;
        }

        case SoundID::STAR_EARNED: {
            // Rising chime (200ms, ascending notes)
            alloc(0.2f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.003f);
                // Three ascending notes blended
                float note1 = std::sin(2.0f * (float)M_PI * 880.0f * t) *
                              (t < 0.07f ? 1.0f : 0.0f);
                float note2 = std::sin(2.0f * (float)M_PI * 1100.0f * t) *
                              (t >= 0.07f && t < 0.14f ? 1.0f : 0.0f);
                float note3 = std::sin(2.0f * (float)M_PI * 1320.0f * t) *
                              (t >= 0.14f ? 1.0f : 0.0f);
                buf.data[i] = to_s16((note1 + note2 + note3) * env * 0.6f);
            }
            break;
        }

        case SoundID::TROOP_DEPLOY: {
            // Whoosh (100ms, noise sweep)
            alloc(0.1f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.005f);
                float noise = white_noise(rng_seed);
                // Bandpass-like effect via modulation
                float mod_freq = 400.0f + 1200.0f * (t / duration);
                float mod = std::sin(2.0f * (float)M_PI * mod_freq * t);
                buf.data[i] = to_s16(noise * mod * env * 0.5f);
            }
            break;
        }

        case SoundID::BUTTON_CLICK: {
            // Very short tick (30ms)
            alloc(0.03f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.001f);
                float tone = std::sin(2.0f * (float)M_PI * 1000.0f * t);
                buf.data[i] = to_s16(tone * env * 0.4f);
            }
            break;
        }

        case SoundID::VICTORY: {
            // Happy ascending tones (500ms)
            alloc(0.5f);
            // C5 - E5 - G5 - C6
            const float freqs[] = { 523.25f, 659.25f, 783.99f, 1046.50f };
            float note_dur = duration / 4.0f;
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                int note_idx = (int)(t / note_dur);
                if (note_idx > 3) note_idx = 3;
                float note_t = t - note_idx * note_dur;
                float note_env = std::exp(-note_t * 6.0f);
                float freq = freqs[note_idx];
                float val = std::sin(2.0f * (float)M_PI * freq * t);
                // Add harmonics for richness
                val += 0.3f * std::sin(2.0f * (float)M_PI * freq * 2.0f * t);
                val += 0.1f * std::sin(2.0f * (float)M_PI * freq * 3.0f * t);
                float master_env = 1.0f - (t / duration) * 0.3f;
                buf.data[i] = to_s16(val * note_env * master_env * 0.4f);
            }
            break;
        }

        case SoundID::DEFEAT: {
            // Sad descending tones (400ms)
            alloc(0.4f);
            // Cm: C5 - Eb5 - G4 - C4
            const float freqs[] = { 523.25f, 311.13f, 261.63f, 196.00f };
            float note_dur = duration / 4.0f;
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                int note_idx = (int)(t / note_dur);
                if (note_idx > 3) note_idx = 3;
                float note_t = t - note_idx * note_dur;
                float note_env = std::exp(-note_t * 5.0f);
                float freq = freqs[note_idx];
                float val = std::sin(2.0f * (float)M_PI * freq * t);
                val += 0.2f * std::sin(2.0f * (float)M_PI * freq * 2.0f * t);
                float master_env = 1.0f - (t / duration) * 0.4f;
                buf.data[i] = to_s16(val * note_env * master_env * 0.4f);
            }
            break;
        }

        case SoundID::ATTACK_START: {
            // Horn/fanfare (300ms)
            alloc(0.3f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                float t = (float)i / SAMPLE_RATE;
                float env = envelope(t, duration, 0.02f);
                // Brassy tone: fundamental + strong odd harmonics
                float freq = 440.0f;
                float val = std::sin(2.0f * (float)M_PI * freq * t);
                val += 0.5f * std::sin(2.0f * (float)M_PI * freq * 3.0f * t);
                val += 0.25f * std::sin(2.0f * (float)M_PI * freq * 5.0f * t);
                val += 0.12f * std::sin(2.0f * (float)M_PI * freq * 7.0f * t);
                buf.data[i] = to_s16(val * env * 0.35f);
            }
            break;
        }

        default:
            alloc(0.03f);
            for (uint32_t i = 0; i < buf.frames; ++i) {
                buf.data[i] = 0;
            }
            break;
    }

    return buf;
}

// --- Active sound tracking ---

struct ActiveSound {
    ma_sound sound;
    bool     in_use = false;
};

// --- Impl ---

struct AudioEngine::Impl {
    ma_engine          engine;
    bool               engine_ready = false;

    SoundBuffer        buffers[(int)SoundID::COUNT];
    ma_audio_buffer    audio_buffers[(int)SoundID::COUNT];
    bool               buffer_ready[(int)SoundID::COUNT] = {};

    ActiveSound        active[MAX_SIMULTANEOUS];
    int                active_next = 0; // Round-robin index

    float              master_volume = 1.0f;
    float              sfx_volume    = 1.0f;
};

bool AudioEngine::init() {
    if (m_impl) return true; // Already initialized

    m_impl = new (std::nothrow) Impl();
    if (!m_impl) {
        pebble::platform::log_error("[Audio] Failed to allocate audio impl");
        return false;
    }

    // Initialize miniaudio engine
    ma_engine_config config = ma_engine_config_init();
    config.channels = 1;
    config.sampleRate = SAMPLE_RATE;
    config.noDevice = MA_FALSE;

    ma_result result = ma_engine_init(&config, &m_impl->engine);
    if (result != MA_SUCCESS) {
        pebble::platform::log_error("[Audio] Failed to init ma_engine (error %d)", (int)result);
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    m_impl->engine_ready = true;

    // Generate all procedural sounds and create audio buffers
    for (int i = 0; i < (int)SoundID::COUNT; ++i) {
        SoundID sid = (SoundID)i;
        m_impl->buffers[i] = generate_sound(sid);

        if (!m_impl->buffers[i].data || m_impl->buffers[i].frames == 0) {
            pebble::platform::log_error("[Audio] Failed to generate sound %d", i);
            continue;
        }

        ma_audio_buffer_config buf_cfg = ma_audio_buffer_config_init(
            ma_format_s16, 1, m_impl->buffers[i].frames, m_impl->buffers[i].data, nullptr);
        buf_cfg.sampleRate = SAMPLE_RATE;

        result = ma_audio_buffer_init(&buf_cfg, &m_impl->audio_buffers[i]);
        if (result != MA_SUCCESS) {
            pebble::platform::log_error("[Audio] Failed to create audio buffer for sound %d (error %d)", i, (int)result);
            continue;
        }
        m_impl->buffer_ready[i] = true;
    }

    pebble::platform::log_info("[Audio] Initialized with %d procedural sounds", (int)SoundID::COUNT);
    return true;
}

void AudioEngine::shutdown() {
    if (!m_impl) return;

    // Stop and uninit all active sounds
    for (int i = 0; i < MAX_SIMULTANEOUS; ++i) {
        if (m_impl->active[i].in_use) {
            ma_sound_uninit(&m_impl->active[i].sound);
            m_impl->active[i].in_use = false;
        }
    }

    // Uninit audio buffers
    for (int i = 0; i < (int)SoundID::COUNT; ++i) {
        if (m_impl->buffer_ready[i]) {
            ma_audio_buffer_uninit(&m_impl->audio_buffers[i]);
            m_impl->buffer_ready[i] = false;
        }
        if (m_impl->buffers[i].data) {
            std::free(m_impl->buffers[i].data);
            m_impl->buffers[i].data = nullptr;
        }
    }

    // Uninit engine
    if (m_impl->engine_ready) {
        ma_engine_uninit(&m_impl->engine);
        m_impl->engine_ready = false;
    }

    delete m_impl;
    m_impl = nullptr;

    pebble::platform::log_info("[Audio] Shutdown complete");
}

void AudioEngine::play(SoundID sound, float volume) {
    if (!m_impl || !m_impl->engine_ready) return;

    int idx = (int)sound;
    if (idx < 0 || idx >= (int)SoundID::COUNT) return;
    if (!m_impl->buffer_ready[idx]) return;

    // Find a slot — round-robin, stop oldest if full
    int slot = m_impl->active_next;
    m_impl->active_next = (m_impl->active_next + 1) % MAX_SIMULTANEOUS;

    ActiveSound& as = m_impl->active[slot];

    // If slot is in use, stop and uninit previous sound
    if (as.in_use) {
        ma_sound_uninit(&as.sound);
        as.in_use = false;
    }

    // Seek audio buffer back to start for re-use
    ma_audio_buffer_seek_to_pcm_frame(&m_impl->audio_buffers[idx], 0);

    // Init sound from data source (audio buffer)
    ma_result result = ma_sound_init_from_data_source(
        &m_impl->engine,
        &m_impl->audio_buffers[idx],
        0,     // flags
        nullptr, // group
        &as.sound);

    if (result != MA_SUCCESS) return;

    as.in_use = true;

    float final_volume = volume * m_impl->sfx_volume * m_impl->master_volume;
    ma_sound_set_volume(&as.sound, final_volume);
    ma_sound_start(&as.sound);
}

void AudioEngine::set_master_volume(float vol) {
    if (!m_impl) return;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    m_impl->master_volume = vol;
}

void AudioEngine::set_sfx_volume(float vol) {
    if (!m_impl) return;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;
    m_impl->sfx_volume = vol;
}

bool AudioEngine::is_initialized() const {
    return m_impl && m_impl->engine_ready;
}

} // namespace pebble::audio
