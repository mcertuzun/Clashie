#pragma once

#include "pebble/core/types.h"
#include <cstdint>

namespace pebble::gfx {

struct RendererConfig {
    WindowHandle window = 0;
    int32_t width  = 1280;
    int32_t height = 720;
};

struct Color {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};

// Vertex for 2D sprite rendering
struct SpriteVertex {
    float x, y;         // Screen position
    float u, v;         // Texture coordinates
    uint8_t r, g, b, a; // Vertex color / tint
};

// Platform-agnostic renderer interface
class Renderer {
public:
    virtual ~Renderer() = default;

    virtual bool init(const RendererConfig& config) = 0;
    virtual void shutdown() = 0;

    virtual void begin_frame(float clear_r, float clear_g, float clear_b, float clear_a) = 0;
    virtual void end_frame() = 0;

    // Texture management
    virtual TextureID texture_create(int32_t width, int32_t height, const uint8_t* rgba_data) = 0;
    virtual void texture_destroy(TextureID id) = 0;

    // Upload all vertex/index data ONCE per frame
    virtual void upload_batch_data(const SpriteVertex* vertices, uint32_t vertex_count,
                                   const uint16_t* indices, uint32_t index_count) = 0;

    // Draw a range of indices from the uploaded data
    virtual void draw_batch(uint32_t index_offset, uint32_t index_count, TextureID texture) = 0;

    // Viewport / projection
    virtual void set_projection(const float* ortho_matrix_4x4) = 0;

    int32_t width() const { return m_width; }
    int32_t height() const { return m_height; }

protected:
    int32_t m_width = 0;
    int32_t m_height = 0;
};

Renderer* create_renderer();

} // namespace pebble::gfx
