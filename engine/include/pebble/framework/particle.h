#pragma once

#include "pebble/gfx/sprite_batch.h"
#include <cstdint>

namespace pebble {

struct Particle {
    float x, y;           // World position
    float vx, vy;         // Velocity
    float life, max_life; // Remaining / total life (seconds)
    float size;           // Current size (world units)
    float size_start, size_end; // Size over lifetime
    uint8_t r, g, b, a;  // Current color
    uint8_t r_end, g_end, b_end, a_end; // End color (lerp over lifetime)
    uint8_t r_start, g_start, b_start, a_start; // Start color (for correct interpolation)
    bool has_gravity;     // Whether gravity applies
};

class ParticleSystem {
public:
    static constexpr int MAX_PARTICLES = 512;

    void update(float dt);

    // Effect emitters (map to SimEvent types)
    void emit_building_destroyed(float x, float y);
    void emit_troop_killed(float x, float y);
    void emit_attack_hit(float x, float y);
    void emit_defense_fire(float x, float y, float tx, float ty);
    void emit_star_earned();

    // Draw all active particles as sprites into the batch
    void draw(gfx::SpriteBatch& batch, TextureID particle_tex);

    int active_count() const { return m_count; }

private:
    void add(const Particle& p);

    Particle m_particles[MAX_PARTICLES];
    int m_count = 0;
};

} // namespace pebble
