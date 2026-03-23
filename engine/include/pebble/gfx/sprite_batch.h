#pragma once

#include "pebble/gfx/renderer.h"
#include "pebble/gfx/camera.h"
#include "pebble/core/types.h"
#include <cstdint>

namespace pebble::gfx {

// Render layers (back to front, painter's algorithm)
enum RenderLayer : uint8_t {
    LAYER_TERRAIN    = 0,
    LAYER_SHADOWS    = 1,
    LAYER_BUILDINGS  = 2,
    LAYER_TROOPS     = 3,
    LAYER_PROJECTILES = 4,
    LAYER_EFFECTS    = 5,
    LAYER_UI         = 6,
    LAYER_COUNT      = 7
};

// A sprite to be drawn this frame
struct Sprite {
    float    world_x, world_y;   // World position
    float    width, height;      // Size in pixels (at zoom 1.0)
    float    pivot_x, pivot_y;   // Pivot offset (0-1 range, 0.5/1.0 = bottom center)
    float    u0, v0, u1, v1;     // UV coordinates in atlas
    Color    tint;               // Color tint
    TextureID atlas;             // Which texture atlas
    uint8_t  layer;              // RenderLayer
    bool     flip_x;             // Horizontal flip
};

// Maximum sprites per frame
static constexpr uint32_t MAX_SPRITES_PER_FRAME = 4096;
static constexpr uint32_t VERTICES_PER_SPRITE = 4;
static constexpr uint32_t INDICES_PER_SPRITE  = 6;
static constexpr uint32_t MAX_VERTICES = MAX_SPRITES_PER_FRAME * VERTICES_PER_SPRITE;
static constexpr uint32_t MAX_INDICES  = MAX_SPRITES_PER_FRAME * INDICES_PER_SPRITE;

class SpriteBatch {
public:
    void init(Renderer* renderer);

    // Call at start of frame
    void begin(const Camera& camera);

    // Queue a sprite for drawing
    void draw(const Sprite& sprite);

    // Sort, batch, and submit all queued sprites
    void flush();

    uint32_t sprite_count() const { return m_sprite_count; }
    uint32_t draw_call_count() const { return m_draw_calls; }

private:
    void build_quad(const Sprite& sprite, const Camera& camera);

    Renderer*    m_renderer = nullptr;
    const Camera* m_camera  = nullptr;

    // Sprite queue (sorted before submission)
    Sprite       m_sprites[MAX_SPRITES_PER_FRAME];
    uint32_t     m_sprite_count = 0;

    // Vertex/index buffers (CPU side, uploaded per batch)
    SpriteVertex m_vertices[MAX_VERTICES];
    uint16_t     m_indices[MAX_INDICES];
    uint32_t     m_vertex_count = 0;
    uint32_t     m_index_count  = 0;

    uint32_t     m_draw_calls = 0;
};

} // namespace pebble::gfx
