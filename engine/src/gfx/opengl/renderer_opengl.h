#pragma once

#include "pebble/gfx/renderer.h"

#if !defined(PEBBLE_PLATFORM_MACOS) && !defined(PEBBLE_PLATFORM_IOS)

namespace pebble::gfx {

class OpenGLRenderer : public Renderer {
public:
    ~OpenGLRenderer() override;

    bool init(const RendererConfig& config) override;
    void shutdown() override;

    void begin_frame(float clear_r, float clear_g, float clear_b, float clear_a) override;
    void end_frame() override;

    TextureID texture_create(int32_t width, int32_t height, const uint8_t* rgba_data) override;
    void texture_destroy(TextureID id) override;

    void upload_batch_data(const SpriteVertex* vertices, uint32_t vertex_count,
                           const uint16_t* indices, uint32_t index_count) override;
    void draw_batch(uint32_t index_offset, uint32_t index_count, TextureID texture) override;

    void set_projection(const float* ortho_matrix_4x4) override;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace pebble::gfx

#endif
