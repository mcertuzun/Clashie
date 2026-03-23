#include "pebble/gfx/sprite_batch.h"
#include <algorithm>

namespace pebble::gfx {

void SpriteBatch::init(Renderer* renderer) {
    m_renderer = renderer;
}

void SpriteBatch::begin(const Camera& camera) {
    m_camera = &camera;
    m_sprite_count = 0;
    m_vertex_count = 0;
    m_index_count = 0;
    m_draw_calls = 0;
}

void SpriteBatch::draw(const Sprite& sprite) {
    if (m_sprite_count >= MAX_SPRITES_PER_FRAME) return;
    m_sprites[m_sprite_count++] = sprite;
}

// Batch boundary tracked during vertex build
struct BatchRange {
    TextureID atlas;
    uint32_t  index_start;
    uint32_t  index_count;
};

void SpriteBatch::flush() {
    if (m_sprite_count == 0 || !m_renderer || !m_camera) return;

    // Sort: layer -> atlas -> y
    std::sort(m_sprites, m_sprites + m_sprite_count,
        [](const Sprite& a, const Sprite& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            if (a.atlas != b.atlas) return a.atlas < b.atlas;
            return a.world_y < b.world_y;
        }
    );

    // Set projection
    float proj[16];
    m_camera->get_projection_matrix(proj);
    m_renderer->set_projection(proj);

    // Build vertex/index data and track batch boundaries simultaneously
    m_vertex_count = 0;
    m_index_count = 0;

    static constexpr uint32_t MAX_BATCHES = 128;
    BatchRange batches[MAX_BATCHES];
    uint32_t batch_count = 0;

    TextureID current_atlas = m_sprites[0].atlas;
    uint32_t batch_index_start = 0;

    for (uint32_t i = 0; i < m_sprite_count; ++i) {
        // Atlas changed — close current batch
        if (m_sprites[i].atlas != current_atlas) {
            uint32_t count = m_index_count - batch_index_start;
            if (count > 0 && batch_count < MAX_BATCHES) {
                batches[batch_count++] = { current_atlas, batch_index_start, count };
            }
            current_atlas = m_sprites[i].atlas;
            batch_index_start = m_index_count;
        }
        build_quad(m_sprites[i], *m_camera);
    }

    // Close last batch
    uint32_t remaining = m_index_count - batch_index_start;
    if (remaining > 0 && batch_count < MAX_BATCHES) {
        batches[batch_count++] = { current_atlas, batch_index_start, remaining };
    }

    if (m_vertex_count == 0) return;

    // Upload ALL data once
    m_renderer->upload_batch_data(m_vertices, m_vertex_count, m_indices, m_index_count);

    // Draw each batch (just offset + count, no data upload)
    for (uint32_t i = 0; i < batch_count; ++i) {
        m_renderer->draw_batch(batches[i].index_start, batches[i].index_count, batches[i].atlas);
    }
    m_draw_calls = batch_count;
}

void SpriteBatch::build_quad(const Sprite& sprite, const Camera& camera) {
    if (m_vertex_count + 4 > MAX_VERTICES || m_index_count + 6 > MAX_INDICES) return;

    float sx, sy;
    camera.world_to_screen(sprite.world_x, sprite.world_y, sx, sy);

    float zoom = camera.zoom();
    float w = sprite.width * zoom;
    float h = sprite.height * zoom;

    float ox = sprite.pivot_x * w;
    float oy = sprite.pivot_y * h;

    float x0 = sx - ox;
    float y0 = sy - oy;
    float x1 = x0 + w;
    float y1 = y0 + h;

    // Frustum cull
    float vw = static_cast<float>(camera.viewport_w());
    float vh = static_cast<float>(camera.viewport_h());
    if (x1 < 0 || x0 > vw || y1 < 0 || y0 > vh) return;

    float u0 = sprite.flip_x ? sprite.u1 : sprite.u0;
    float v0 = sprite.v0;
    float u1 = sprite.flip_x ? sprite.u0 : sprite.u1;
    float v1 = sprite.v1;

    uint32_t base = m_vertex_count;
    auto& t = sprite.tint;

    m_vertices[base + 0] = { x0, y0, u0, v0, t.r, t.g, t.b, t.a };
    m_vertices[base + 1] = { x1, y0, u1, v0, t.r, t.g, t.b, t.a };
    m_vertices[base + 2] = { x1, y1, u1, v1, t.r, t.g, t.b, t.a };
    m_vertices[base + 3] = { x0, y1, u0, v1, t.r, t.g, t.b, t.a };

    auto b16 = static_cast<uint16_t>(base);
    m_indices[m_index_count++] = b16 + 0;
    m_indices[m_index_count++] = b16 + 1;
    m_indices[m_index_count++] = b16 + 2;
    m_indices[m_index_count++] = b16 + 0;
    m_indices[m_index_count++] = b16 + 2;
    m_indices[m_index_count++] = b16 + 3;

    m_vertex_count += 4;
}

} // namespace pebble::gfx
