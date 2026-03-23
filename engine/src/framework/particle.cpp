#include "pebble/framework/particle.h"
#include <cmath>
#include <cstdlib>

namespace pebble {

// ---------------------------------------------------------------------------
// Simple deterministic pseudo-random (visual only, no gameplay impact)
// ---------------------------------------------------------------------------

static uint32_t s_particle_seed = 42;

static float particle_randf() {
    s_particle_seed = s_particle_seed * 1103515245u + 12345u;
    return (float)((s_particle_seed >> 16) & 0x7FFF) / 32767.0f; // 0..1
}

static float particle_randf_range(float lo, float hi) {
    return lo + particle_randf() * (hi - lo);
}

// ---------------------------------------------------------------------------
// ParticleSystem
// ---------------------------------------------------------------------------

void ParticleSystem::add(const Particle& p) {
    if (m_count >= MAX_PARTICLES) return;
    m_particles[m_count++] = p;
}

void ParticleSystem::update(float dt) {
    static constexpr float GRAVITY = 50.0f; // world-units/s^2

    int i = 0;
    while (i < m_count) {
        Particle& p = m_particles[i];

        // Reduce life
        p.life -= dt;
        if (p.life <= 0.0f) {
            // Remove by swap-with-last
            m_particles[i] = m_particles[m_count - 1];
            --m_count;
            continue;
        }

        // Physics
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.has_gravity) {
            p.vy += GRAVITY * dt;
        }

        // Lerp factor (0 = just born, 1 = about to die)
        float t = 1.0f - (p.life / p.max_life);

        // Size interpolation
        p.size = p.size_start + (p.size_end - p.size_start) * t;

        // Color interpolation
        p.r = (uint8_t)(p.r + (int)(((int)p.r_end - (int)p.r) * t));
        p.g = (uint8_t)(p.g + (int)(((int)p.g_end - (int)p.g) * t));
        p.b = (uint8_t)(p.b + (int)(((int)p.b_end - (int)p.b) * t));
        p.a = (uint8_t)(p.a + (int)(((int)p.a_end - (int)p.a) * t));

        ++i;
    }
}

// ---------------------------------------------------------------------------
// Emitters
// ---------------------------------------------------------------------------

void ParticleSystem::emit_building_destroyed(float x, float y) {
    // 20 brown/gray debris particles flying outward + upward with gravity
    for (int i = 0; i < 20; ++i) {
        Particle p{};
        p.x = x + particle_randf_range(-0.3f, 0.3f);
        p.y = y + particle_randf_range(-0.3f, 0.3f);

        float angle = particle_randf() * 6.2831853f; // 0..2PI
        float speed = particle_randf_range(1.0f, 4.0f);
        p.vx = std::cos(angle) * speed;
        p.vy = -particle_randf_range(2.0f, 6.0f); // upward in screen (negative Y)

        p.life = p.max_life = particle_randf_range(0.6f, 1.2f);

        p.size_start = particle_randf_range(0.15f, 0.35f);
        p.size_end = 0.05f;
        p.size = p.size_start;

        // Brown / gray color
        bool gray = (i % 3 == 0);
        if (gray) {
            p.r = 160; p.g = 160; p.b = 160; p.a = 255;
        } else {
            p.r = 160; p.g = 110; p.b = 60; p.a = 255;
        }
        p.r_end = 80; p.g_end = 80; p.b_end = 80; p.a_end = 0;

        p.has_gravity = true;

        add(p);
    }
}

void ParticleSystem::emit_troop_killed(float x, float y) {
    // 8 small red particles, quick fade
    for (int i = 0; i < 8; ++i) {
        Particle p{};
        p.x = x + particle_randf_range(-0.15f, 0.15f);
        p.y = y + particle_randf_range(-0.15f, 0.15f);

        float angle = particle_randf() * 6.2831853f;
        float speed = particle_randf_range(0.5f, 2.0f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed - 1.0f; // slight upward bias

        p.life = p.max_life = particle_randf_range(0.3f, 0.6f);

        p.size_start = particle_randf_range(0.08f, 0.18f);
        p.size_end = 0.02f;
        p.size = p.size_start;

        p.r = 255; p.g = 50; p.b = 30; p.a = 255;
        p.r_end = 180; p.g_end = 20; p.b_end = 20; p.a_end = 0;

        p.has_gravity = false;

        add(p);
    }
}

void ParticleSystem::emit_attack_hit(float x, float y) {
    // 4 white/yellow sparks, fast, small
    for (int i = 0; i < 4; ++i) {
        Particle p{};
        p.x = x + particle_randf_range(-0.1f, 0.1f);
        p.y = y + particle_randf_range(-0.1f, 0.1f);

        float angle = particle_randf() * 6.2831853f;
        float speed = particle_randf_range(1.5f, 3.5f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;

        p.life = p.max_life = particle_randf_range(0.1f, 0.25f);

        p.size_start = particle_randf_range(0.06f, 0.12f);
        p.size_end = 0.01f;
        p.size = p.size_start;

        p.r = 255; p.g = 255; p.b = 200; p.a = 255;
        p.r_end = 255; p.g_end = 200; p.b_end = 50; p.a_end = 0;

        p.has_gravity = false;

        add(p);
    }
}

void ParticleSystem::emit_defense_fire(float x, float y, float tx, float ty) {
    // 3 orange particles along the line from source to target (trail effect)
    for (int i = 0; i < 3; ++i) {
        float t = (float)i / 2.0f; // 0, 0.5, 1.0 along the line
        Particle p{};
        p.x = x + (tx - x) * t + particle_randf_range(-0.1f, 0.1f);
        p.y = y + (ty - y) * t + particle_randf_range(-0.1f, 0.1f);

        // Slight spread perpendicular to travel direction
        p.vx = particle_randf_range(-0.3f, 0.3f);
        p.vy = particle_randf_range(-0.3f, 0.3f);

        p.life = p.max_life = particle_randf_range(0.15f, 0.35f);

        p.size_start = particle_randf_range(0.08f, 0.15f);
        p.size_end = 0.03f;
        p.size = p.size_start;

        p.r = 255; p.g = 160; p.b = 40; p.a = 230;
        p.r_end = 255; p.g_end = 80; p.b_end = 10; p.a_end = 0;

        p.has_gravity = false;

        add(p);
    }
}

void ParticleSystem::emit_star_earned() {
    // 15 gold particles burst from screen center (world coords ~20,20), no gravity
    float cx = 20.0f;
    float cy = 20.0f;

    for (int i = 0; i < 15; ++i) {
        Particle p{};
        p.x = cx + particle_randf_range(-0.5f, 0.5f);
        p.y = cy + particle_randf_range(-0.5f, 0.5f);

        float angle = particle_randf() * 6.2831853f;
        float speed = particle_randf_range(2.0f, 6.0f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;

        p.life = p.max_life = particle_randf_range(0.8f, 1.5f);

        p.size_start = particle_randf_range(0.15f, 0.3f);
        p.size_end = 0.05f;
        p.size = p.size_start;

        p.r = 255; p.g = 220; p.b = 50; p.a = 255;
        p.r_end = 255; p.g_end = 180; p.b_end = 0; p.a_end = 0;

        p.has_gravity = false;

        add(p);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void ParticleSystem::draw(gfx::SpriteBatch& batch, TextureID particle_tex) {
    for (int i = 0; i < m_count; ++i) {
        const Particle& p = m_particles[i];

        gfx::Sprite s;
        s.world_x = p.x;
        s.world_y = p.y;
        s.width  = gfx::Camera::TILE_WIDTH * p.size;
        s.height = gfx::Camera::TILE_WIDTH * p.size; // Square aspect for circle
        s.pivot_x = 0.5f;
        s.pivot_y = 0.5f;
        s.u0 = 0.0f; s.v0 = 0.0f;
        s.u1 = 1.0f; s.v1 = 1.0f;
        s.tint = { p.r, p.g, p.b, p.a };
        s.atlas = particle_tex;
        s.layer = gfx::LAYER_EFFECTS;
        s.flip_x = false;

        batch.draw(s);
    }
}

} // namespace pebble
