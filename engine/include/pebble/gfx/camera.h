#pragma once

#include <cstdint>

namespace pebble::gfx {

// Isometric 2:1 camera with pan and zoom
class Camera {
public:
    void set_viewport(int32_t screen_w, int32_t screen_h);
    void set_position(float world_x, float world_y);
    void set_zoom(float zoom);
    void pan(float dx, float dy);

    // Isometric transforms (2:1 ratio, matching CoC)
    // Tile width = 64, tile height = 32 at zoom 1.0
    void world_to_screen(float wx, float wy, float& sx, float& sy) const;
    void screen_to_world(float sx, float sy, float& wx, float& wy) const;

    // Builds orthographic projection matrix for the renderer
    void get_projection_matrix(float* out_4x4) const;

    float pos_x() const { return m_pos_x; }
    float pos_y() const { return m_pos_y; }
    float zoom() const { return m_zoom; }
    int32_t viewport_w() const { return m_viewport_w; }
    int32_t viewport_h() const { return m_viewport_h; }

    static constexpr float TILE_WIDTH  = 64.0f;
    static constexpr float TILE_HEIGHT = 32.0f;
    static constexpr float MIN_ZOOM = 0.5f;
    static constexpr float MAX_ZOOM = 2.0f;

private:
    float   m_pos_x = 0.0f;    // Camera center in world coords
    float   m_pos_y = 0.0f;
    float   m_zoom  = 1.0f;
    int32_t m_viewport_w = 1280;
    int32_t m_viewport_h = 720;
};

} // namespace pebble::gfx
