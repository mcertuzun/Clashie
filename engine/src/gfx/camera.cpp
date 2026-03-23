#include "pebble/gfx/camera.h"
#include <cmath>
#include <algorithm>

namespace pebble::gfx {

void Camera::set_viewport(int32_t screen_w, int32_t screen_h) {
    m_viewport_w = screen_w;
    m_viewport_h = screen_h;
}

void Camera::set_position(float world_x, float world_y) {
    m_pos_x = world_x;
    m_pos_y = world_y;
}

void Camera::set_zoom(float zoom) {
    m_zoom = std::clamp(zoom, MIN_ZOOM, MAX_ZOOM);
}

void Camera::pan(float dx, float dy) {
    m_pos_x += dx / m_zoom;
    m_pos_y += dy / m_zoom;
}

void Camera::world_to_screen(float wx, float wy, float& sx, float& sy) const {
    // Isometric 2:1 projection
    float iso_x = (wx - wy) * (TILE_WIDTH / 2.0f);
    float iso_y = (wx + wy) * (TILE_HEIGHT / 2.0f);

    // Apply camera offset and zoom, center on screen
    sx = (iso_x - m_pos_x) * m_zoom + m_viewport_w * 0.5f;
    sy = (iso_y - m_pos_y) * m_zoom + m_viewport_h * 0.5f;
}

void Camera::screen_to_world(float sx, float sy, float& wx, float& wy) const {
    // Reverse: screen → iso → world
    float iso_x = (sx - m_viewport_w * 0.5f) / m_zoom + m_pos_x;
    float iso_y = (sy - m_viewport_h * 0.5f) / m_zoom + m_pos_y;

    // Inverse isometric
    wx = (iso_x / (TILE_WIDTH / 2.0f) + iso_y / (TILE_HEIGHT / 2.0f)) / 2.0f;
    wy = (iso_y / (TILE_HEIGHT / 2.0f) - iso_x / (TILE_WIDTH / 2.0f)) / 2.0f;
}

void Camera::get_projection_matrix(float* m) const {
    // Orthographic projection matrix
    // Maps screen pixel coordinates to NDC [-1, 1]
    float l = 0.0f;
    float r = static_cast<float>(m_viewport_w);
    float b = static_cast<float>(m_viewport_h);
    float t = 0.0f;
    float n = -1.0f;
    float f = 1.0f;

    // Column-major 4x4 orthographic matrix
    m[0]  =  2.0f / (r - l);  m[1]  = 0.0f;              m[2]  = 0.0f;              m[3]  = 0.0f;
    m[4]  =  0.0f;             m[5]  = 2.0f / (t - b);    m[6]  = 0.0f;              m[7]  = 0.0f;
    m[8]  =  0.0f;             m[9]  = 0.0f;              m[10] = -2.0f / (f - n);    m[11] = 0.0f;
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n); m[15] = 1.0f;
}

} // namespace pebble::gfx
