#pragma once
#include <cstdint>

namespace pebble::audio {

enum class SoundID : uint8_t {
    TROOP_DEPLOY = 0,
    CANNON_FIRE,
    ARROW_SHOOT,
    MORTAR_LAUNCH,
    BUILDING_DESTROYED,
    WALL_BREAK,
    TROOP_DEATH,
    STAR_EARNED,
    ATTACK_START,
    VICTORY,
    DEFEAT,
    BUTTON_CLICK,
    COUNT
};

class AudioEngine {
public:
    bool init();
    void shutdown();

    // Play a procedurally generated sound effect
    void play(SoundID sound, float volume = 1.0f);

    // Volume control
    void set_master_volume(float vol); // 0.0 - 1.0
    void set_sfx_volume(float vol);

    bool is_initialized() const;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace pebble::audio
