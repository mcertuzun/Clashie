#include "pebble/framework/ui.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace pebble::ui {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static constexpr uint32_t MAX_UI_VERTICES = 8192;
static constexpr uint32_t MAX_UI_INDICES  = 16384;

static gfx::SpriteVertex s_vertices[MAX_UI_VERTICES];
static uint16_t           s_indices[MAX_UI_INDICES];
static uint32_t           s_vertex_count = 0;
static uint32_t           s_index_count  = 0;

static void flush_ui(UIContext& ctx) {
    if (s_index_count == 0) return;
    ctx.renderer->upload_batch_data(s_vertices, s_vertex_count,
                                    s_indices, s_index_count);
    ctx.renderer->draw_batch(0, s_index_count, ctx.white_tex);
    s_vertex_count = 0;
    s_index_count  = 0;
}

static void push_quad(UIContext& ctx,
                      float x0, float y0, float x1, float y1,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (s_vertex_count + 4 > MAX_UI_VERTICES || s_index_count + 6 > MAX_UI_INDICES) {
        flush_ui(ctx);
    }

    auto base = static_cast<uint16_t>(s_vertex_count);

    // All UVs map to the center of the 1x1 white texture (0.5, 0.5)
    s_vertices[s_vertex_count++] = { x0, y0, 0.5f, 0.5f, r, g, b, a };
    s_vertices[s_vertex_count++] = { x1, y0, 0.5f, 0.5f, r, g, b, a };
    s_vertices[s_vertex_count++] = { x1, y1, 0.5f, 0.5f, r, g, b, a };
    s_vertices[s_vertex_count++] = { x0, y1, 0.5f, 0.5f, r, g, b, a };

    s_indices[s_index_count++] = base + 0;
    s_indices[s_index_count++] = base + 1;
    s_indices[s_index_count++] = base + 2;
    s_indices[s_index_count++] = base + 0;
    s_indices[s_index_count++] = base + 2;
    s_indices[s_index_count++] = base + 3;
}

static void push_triangle(UIContext& ctx,
                           float x0, float y0,
                           float x1, float y1,
                           float x2, float y2,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (s_vertex_count + 3 > MAX_UI_VERTICES || s_index_count + 3 > MAX_UI_INDICES) {
        flush_ui(ctx);
    }

    auto base = static_cast<uint16_t>(s_vertex_count);

    s_vertices[s_vertex_count++] = { x0, y0, 0.5f, 0.5f, r, g, b, a };
    s_vertices[s_vertex_count++] = { x1, y1, 0.5f, 0.5f, r, g, b, a };
    s_vertices[s_vertex_count++] = { x2, y2, 0.5f, 0.5f, r, g, b, a };

    s_indices[s_index_count++] = base + 0;
    s_indices[s_index_count++] = base + 1;
    s_indices[s_index_count++] = base + 2;
}

static bool point_in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

// ---------------------------------------------------------------------------
// 7-segment digit definitions
// Each digit is 7 segments: top, top-right, bottom-right, bottom,
//                           bottom-left, top-left, middle
// Encoded as a bitmask: bit 0=top, 1=top-right, 2=bot-right, 3=bot,
//                       4=bot-left, 5=top-left, 6=middle
// ---------------------------------------------------------------------------

static constexpr uint8_t DIGIT_SEGMENTS[10] = {
    0b0111111, // 0: all except middle
    0b0000110, // 1: top-right, bottom-right
    0b1011011, // 2: top, top-right, middle, bottom-left, bottom
    0b1001111, // 3: top, top-right, middle, bottom-right, bottom
    0b1100110, // 4: top-left, top-right, middle, bottom-right
    0b1101101, // 5: top, top-left, middle, bottom-right, bottom
    0b1111101, // 6: top, top-left, middle, bottom-left, bottom-right, bottom
    0b0000111, // 7: top, top-right, bottom-right
    0b1111111, // 8: all segments
    0b1101111, // 9: top, top-left, top-right, middle, bottom-right, bottom
};

// Draw a single 7-segment digit at position (dx, dy) with height dh.
// Returns the width consumed (for advancing cursor).
static float draw_single_digit(UIContext& ctx, float dx, float dy, float dh,
                                int digit, uint8_t r, uint8_t g, uint8_t b) {
    if (digit < 0 || digit > 9) digit = 0;

    uint8_t segs = DIGIT_SEGMENTS[digit];

    // Digit proportions: width = 0.6 * height, segment thickness = 0.12 * height
    float dw = dh * 0.6f;
    float t  = dh * 0.12f;
    float half_h = dh * 0.5f;

    // Segment 0: top (horizontal)
    if (segs & 0x01)
        push_quad(ctx, dx + t, dy, dx + dw - t, dy + t, r, g, b, 255);

    // Segment 1: top-right (vertical)
    if (segs & 0x02)
        push_quad(ctx, dx + dw - t, dy + t, dx + dw, dy + half_h, r, g, b, 255);

    // Segment 2: bottom-right (vertical)
    if (segs & 0x04)
        push_quad(ctx, dx + dw - t, dy + half_h, dx + dw, dy + dh - t, r, g, b, 255);

    // Segment 3: bottom (horizontal)
    if (segs & 0x08)
        push_quad(ctx, dx + t, dy + dh - t, dx + dw - t, dy + dh, r, g, b, 255);

    // Segment 4: bottom-left (vertical)
    if (segs & 0x10)
        push_quad(ctx, dx, dy + half_h, dx + t, dy + dh - t, r, g, b, 255);

    // Segment 5: top-left (vertical)
    if (segs & 0x20)
        push_quad(ctx, dx, dy + t, dx + t, dy + half_h, r, g, b, 255);

    // Segment 6: middle (horizontal)
    if (segs & 0x40)
        push_quad(ctx, dx + t, dy + half_h - t * 0.5f,
                  dx + dw - t, dy + half_h + t * 0.5f, r, g, b, 255);

    return dw;
}

// Draw a colon (two small squares) for the timer
static float draw_colon(UIContext& ctx, float dx, float dy, float dh,
                         uint8_t r, uint8_t g, uint8_t b) {
    float dot_size = dh * 0.12f;
    float col_w    = dh * 0.3f;
    float cx       = dx + col_w * 0.5f - dot_size * 0.5f;

    // Upper dot
    float uy = dy + dh * 0.28f;
    push_quad(ctx, cx, uy, cx + dot_size, uy + dot_size, r, g, b, 255);

    // Lower dot
    float ly = dy + dh * 0.60f;
    push_quad(ctx, cx, ly, cx + dot_size, ly + dot_size, r, g, b, 255);

    return col_w;
}

// ---------------------------------------------------------------------------
// Star rendering helpers
// ---------------------------------------------------------------------------

// Generate the 10 points of a 5-pointed star (outer + inner)
// centered at (cx, cy) with given outer_radius and inner_radius.
static void star_points(float cx, float cy, float outer_r, float inner_r,
                         float out_x[10], float out_y[10]) {
    // First outer point at top (-90 degrees)
    constexpr float PI = 3.14159265358979f;
    for (int i = 0; i < 5; ++i) {
        float angle_outer = -PI / 2.0f + i * (2.0f * PI / 5.0f);
        float angle_inner = -PI / 2.0f + (i + 0.5f) * (2.0f * PI / 5.0f);
        out_x[i * 2]     = cx + std::cos(angle_outer) * outer_r;
        out_y[i * 2]     = cy + std::sin(angle_outer) * outer_r;
        out_x[i * 2 + 1] = cx + std::cos(angle_inner) * inner_r;
        out_y[i * 2 + 1] = cy + std::sin(angle_inner) * inner_r;
    }
}

// Draw a filled 5-pointed star using a triangle fan from the center
static void draw_star_filled(UIContext& ctx, float cx, float cy, float size,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float outer_r = size * 0.5f;
    float inner_r = outer_r * 0.38f; // inner radius ratio for a nice star
    float px[10], py[10];
    star_points(cx, cy, outer_r, inner_r, px, py);

    // Triangle fan from center
    for (int i = 0; i < 10; ++i) {
        int next = (i + 1) % 10;
        push_triangle(ctx, cx, cy, px[i], py[i], px[next], py[next], r, g, b, a);
    }
}

// Draw an outlined star using thin quads along each edge
static void draw_star_outline(UIContext& ctx, float cx, float cy, float size,
                               float thickness,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float outer_r = size * 0.5f;
    float inner_r = outer_r * 0.38f;
    float px[10], py[10];
    star_points(cx, cy, outer_r, inner_r, px, py);

    // Draw each edge as a thin quad
    for (int i = 0; i < 10; ++i) {
        int next = (i + 1) % 10;
        float x0 = px[i], y0 = py[i];
        float x1 = px[next], y1 = py[next];

        // Perpendicular direction for thickness
        float dx = x1 - x0;
        float dy = y1 - y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) continue;
        float nx = -dy / len * thickness * 0.5f;
        float ny =  dx / len * thickness * 0.5f;

        push_triangle(ctx,
                      x0 + nx, y0 + ny,
                      x1 + nx, y1 + ny,
                      x1 - nx, y1 - ny,
                      r, g, b, a);
        push_triangle(ctx,
                      x0 + nx, y0 + ny,
                      x1 - nx, y1 - ny,
                      x0 - nx, y0 - ny,
                      r, g, b, a);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ui_begin(UIContext& ctx, gfx::Renderer* renderer, int32_t w, int32_t h) {
    ctx.renderer = renderer;
    ctx.screen_w = w;
    ctx.screen_h = h;

    // Create 1x1 white texture on first use
    if (ctx.white_tex == 0) {
        uint8_t white_pixel[4] = { 255, 255, 255, 255 };
        ctx.white_tex = renderer->texture_create(1, 1, white_pixel);
    }

    // Set orthographic projection for screen-space UI (top-left origin)
    float l = 0.0f;
    float r = static_cast<float>(w);
    float b = static_cast<float>(h);
    float t = 0.0f;
    float n = -1.0f;
    float f = 1.0f;

    float proj[16];
    proj[0]  =  2.0f / (r - l);   proj[1]  = 0.0f;             proj[2]  = 0.0f;             proj[3]  = 0.0f;
    proj[4]  =  0.0f;              proj[5]  = 2.0f / (t - b);   proj[6]  = 0.0f;             proj[7]  = 0.0f;
    proj[8]  =  0.0f;              proj[9]  = 0.0f;             proj[10] = -2.0f / (f - n);   proj[11] = 0.0f;
    proj[12] = -(r + l) / (r - l); proj[13] = -(t + b) / (t - b); proj[14] = -(f + n) / (f - n); proj[15] = 1.0f;

    renderer->set_projection(proj);

    // Reset vertex/index buffers
    s_vertex_count = 0;
    s_index_count  = 0;
}

void draw_rect(UIContext& ctx, float x, float y, float w, float h,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    push_quad(ctx, x, y, x + w, y + h, r, g, b, a);
}

void draw_rect_outline(UIContext& ctx, float x, float y, float w, float h,
                       float thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float t = thickness;
    // Top edge
    push_quad(ctx, x, y, x + w, y + t, r, g, b, a);
    // Bottom edge
    push_quad(ctx, x, y + h - t, x + w, y + h, r, g, b, a);
    // Left edge
    push_quad(ctx, x, y + t, x + t, y + h - t, r, g, b, a);
    // Right edge
    push_quad(ctx, x + w - t, y + t, x + w, y + h - t, r, g, b, a);
}

void draw_health_bar(UIContext& ctx, float x, float y, float w, float h,
                     float hp_ratio) {
    hp_ratio = std::clamp(hp_ratio, 0.0f, 1.0f);

    // Dark background
    push_quad(ctx, x, y, x + w, y + h, 30, 30, 30, 200);

    // Foreground color based on health ratio
    uint8_t r, g, b;
    if (hp_ratio > 0.66f) {
        // Green
        r = 50; g = 200; b = 50;
    } else if (hp_ratio > 0.33f) {
        // Yellow
        r = 220; g = 200; b = 30;
    } else {
        // Red
        r = 220; g = 40; b = 40;
    }

    float fill_w = w * hp_ratio;
    if (fill_w > 0.5f) {
        push_quad(ctx, x, y, x + fill_w, y + h, r, g, b, 255);
    }

    // Thin outline
    draw_rect_outline(ctx, x, y, w, h, 1.0f, 180, 180, 180, 200);
}

void draw_stars(UIContext& ctx, float x, float y, float size, int earned, int total) {
    float spacing = size * 1.2f;

    for (int i = 0; i < total; ++i) {
        float cx = x + i * spacing + size * 0.5f;
        float cy = y + size * 0.5f;

        if (i < earned) {
            // Filled star: gold
            draw_star_filled(ctx, cx, cy, size, 255, 200, 50, 255);
        } else {
            // Empty star: gray outline
            draw_star_outline(ctx, cx, cy, size, size * 0.08f, 120, 120, 120, 200);
        }
    }
}

void draw_progress_bar(UIContext& ctx, float x, float y, float w, float h,
                       float ratio, uint8_t r, uint8_t g, uint8_t b) {
    ratio = std::clamp(ratio, 0.0f, 1.0f);

    // Background
    push_quad(ctx, x, y, x + w, y + h, 40, 40, 40, 200);

    // Fill
    float fill_w = w * ratio;
    if (fill_w > 0.5f) {
        push_quad(ctx, x, y, x + fill_w, y + h, r, g, b, 255);
    }

    // Border
    draw_rect_outline(ctx, x, y, w, h, 1.0f, 100, 100, 100, 200);
}

bool draw_button(UIContext& ctx, float x, float y, float w, float h,
                 uint8_t r, uint8_t g, uint8_t b, bool selected) {
    bool hovered = point_in_rect(ctx.mouse_x, ctx.mouse_y, x, y, w, h);
    bool clicked = hovered && ctx.mouse_pressed;

    // Base color with hover/select modifiers
    uint8_t dr = r, dg = g, db = b;
    if (selected) {
        // Brighter for selected state
        dr = static_cast<uint8_t>(std::min(255, r + 60));
        dg = static_cast<uint8_t>(std::min(255, g + 60));
        db = static_cast<uint8_t>(std::min(255, b + 60));
    } else if (hovered) {
        // Slightly brighter on hover
        dr = static_cast<uint8_t>(std::min(255, r + 30));
        dg = static_cast<uint8_t>(std::min(255, g + 30));
        db = static_cast<uint8_t>(std::min(255, b + 30));
    }

    // Button body
    push_quad(ctx, x, y, x + w, y + h, dr, dg, db, 255);

    // Pressed visual: darker inner shadow
    if (hovered && ctx.mouse_down) {
        push_quad(ctx, x, y, x + w, y + h, 0, 0, 0, 50);
    }

    // Border
    uint8_t br = selected ? (uint8_t)255 : (uint8_t)180;
    uint8_t bg_c = selected ? (uint8_t)220 : (uint8_t)180;
    uint8_t bb = selected ? (uint8_t)100 : (uint8_t)180;
    draw_rect_outline(ctx, x, y, w, h, 2.0f, br, bg_c, bb, 255);

    return clicked;
}

void draw_number(UIContext& ctx, float x, float y, float digit_h, int number,
                 uint8_t r, uint8_t g, uint8_t b) {
    // Handle negative numbers
    bool negative = number < 0;
    if (negative) number = -number;

    // Extract digits
    int digits[16];
    int count = 0;
    if (number == 0) {
        digits[count++] = 0;
    } else {
        while (number > 0 && count < 16) {
            digits[count++] = number % 10;
            number /= 10;
        }
        // Reverse
        for (int i = 0; i < count / 2; ++i) {
            int tmp = digits[i];
            digits[i] = digits[count - 1 - i];
            digits[count - 1 - i] = tmp;
        }
    }

    float cursor_x = x;
    float spacing = digit_h * 0.15f;

    // Draw minus sign
    if (negative) {
        float t = digit_h * 0.12f;
        float mw = digit_h * 0.35f;
        push_quad(ctx, cursor_x, y + digit_h * 0.5f - t * 0.5f,
                  cursor_x + mw, y + digit_h * 0.5f + t * 0.5f, r, g, b, 255);
        cursor_x += mw + spacing;
    }

    // Draw each digit
    for (int i = 0; i < count; ++i) {
        float dw = draw_single_digit(ctx, cursor_x, y, digit_h, digits[i], r, g, b);
        cursor_x += dw + spacing;
    }
}

void draw_timer(UIContext& ctx, float x, float y, float h, int seconds_remaining,
                uint8_t r, uint8_t g, uint8_t b) {
    if (seconds_remaining < 0) seconds_remaining = 0;

    int minutes = seconds_remaining / 60;
    int seconds = seconds_remaining % 60;

    float cursor_x = x;
    float digit_spacing = h * 0.15f;

    // Minutes — tens digit
    draw_single_digit(ctx, cursor_x, y, h, minutes / 10, r, g, b);
    cursor_x += h * 0.6f + digit_spacing;

    // Minutes — units digit
    draw_single_digit(ctx, cursor_x, y, h, minutes % 10, r, g, b);
    cursor_x += h * 0.6f + digit_spacing;

    // Colon
    cursor_x += draw_colon(ctx, cursor_x, y, h, r, g, b);
    cursor_x += digit_spacing;

    // Seconds — tens digit
    draw_single_digit(ctx, cursor_x, y, h, seconds / 10, r, g, b);
    cursor_x += h * 0.6f + digit_spacing;

    // Seconds — units digit
    draw_single_digit(ctx, cursor_x, y, h, seconds % 10, r, g, b);
}

// ---------------------------------------------------------------------------
// 5x7 Bitmap Font
// Each character is 5 columns x 7 rows, stored as 7 bytes (1 byte per row,
// bits 4..0 = columns left to right). Supports A-Z, 0-9, and a few symbols.
// ---------------------------------------------------------------------------

// clang-format off
static const uint8_t FONT_5X7[128][7] = {
    // 0-31: control chars (blank)
    ['0'] = { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E },
    ['1'] = { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E },
    ['2'] = { 0x0E,0x11,0x01,0x06,0x08,0x10,0x1F },
    ['3'] = { 0x0E,0x11,0x01,0x06,0x01,0x11,0x0E },
    ['4'] = { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 },
    ['5'] = { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E },
    ['6'] = { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E },
    ['7'] = { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 },
    ['8'] = { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E },
    ['9'] = { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C },
    ['A'] = { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 },
    ['B'] = { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E },
    ['C'] = { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E },
    ['D'] = { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E },
    ['E'] = { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F },
    ['F'] = { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x10 },
    ['G'] = { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0E },
    ['H'] = { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 },
    ['I'] = { 0x0E,0x04,0x04,0x04,0x04,0x04,0x0E },
    ['J'] = { 0x07,0x02,0x02,0x02,0x02,0x12,0x0C },
    ['K'] = { 0x11,0x12,0x14,0x18,0x14,0x12,0x11 },
    ['L'] = { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F },
    ['M'] = { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 },
    ['N'] = { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 },
    ['O'] = { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E },
    ['P'] = { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 },
    ['Q'] = { 0x0E,0x11,0x11,0x11,0x15,0x12,0x0D },
    ['R'] = { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 },
    ['S'] = { 0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E },
    ['T'] = { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 },
    ['U'] = { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E },
    ['V'] = { 0x11,0x11,0x11,0x11,0x0A,0x0A,0x04 },
    ['W'] = { 0x11,0x11,0x11,0x15,0x15,0x1B,0x11 },
    ['X'] = { 0x11,0x11,0x0A,0x04,0x0A,0x11,0x11 },
    ['Y'] = { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 },
    ['Z'] = { 0x1F,0x01,0x02,0x04,0x08,0x10,0x1F },
    [':'] = { 0x00,0x04,0x04,0x00,0x04,0x04,0x00 },
    ['/'] = { 0x01,0x01,0x02,0x04,0x08,0x10,0x10 },
    ['%'] = { 0x18,0x19,0x02,0x04,0x08,0x13,0x03 },
    ['.'] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x04 },
    ['-'] = { 0x00,0x00,0x00,0x0E,0x00,0x00,0x00 },
    ['!'] = { 0x04,0x04,0x04,0x04,0x04,0x00,0x04 },
    ['('] = { 0x02,0x04,0x08,0x08,0x08,0x04,0x02 },
    [')'] = { 0x08,0x04,0x02,0x02,0x02,0x04,0x08 },
    [' '] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    ['['] = { 0x0E,0x08,0x08,0x08,0x08,0x08,0x0E },
    [']'] = { 0x0E,0x02,0x02,0x02,0x02,0x02,0x0E },
};
// clang-format on

static float draw_char(UIContext& ctx, float cx, float cy, float ch,
                        char c, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t idx = static_cast<uint8_t>(c);
    if (idx >= 128) return 0;

    // Lowercase -> uppercase
    if (c >= 'a' && c <= 'z') idx = static_cast<uint8_t>(c - 32);

    const uint8_t* glyph = FONT_5X7[idx];
    float px = ch / 7.0f; // Pixel size

    for (int row = 0; row < 7; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (0x10 >> col)) {
                push_quad(ctx,
                    cx + col * px, cy + row * px,
                    cx + (col + 1) * px, cy + (row + 1) * px,
                    r, g, b, a);
            }
        }
    }
    return 5.0f * px + px; // char width + 1px spacing
}

float draw_text(UIContext& ctx, float x, float y, float char_h, const char* text,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    float cursor = x;
    while (*text) {
        cursor += draw_char(ctx, cursor, y, char_h, *text, r, g, b, a);
        ++text;
    }
    return cursor - x;
}

bool draw_text_button(UIContext& ctx, float x, float y, float w, float h,
                      const char* label,
                      uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                      uint8_t text_r, uint8_t text_g, uint8_t text_b,
                      bool selected) {
    bool hovered = point_in_rect(ctx.mouse_x, ctx.mouse_y, x, y, w, h);
    bool clicked = hovered && ctx.mouse_pressed;

    uint8_t dr = bg_r, dg = bg_g, db = bg_b;
    if (selected) {
        dr = (uint8_t)std::min(255, bg_r + 50);
        dg = (uint8_t)std::min(255, bg_g + 50);
        db = (uint8_t)std::min(255, bg_b + 50);
    } else if (hovered) {
        dr = (uint8_t)std::min(255, bg_r + 25);
        dg = (uint8_t)std::min(255, bg_g + 25);
        db = (uint8_t)std::min(255, bg_b + 25);
    }

    push_quad(ctx, x, y, x + w, y + h, dr, dg, db, 255);
    if (hovered && ctx.mouse_down) {
        push_quad(ctx, x, y, x + w, y + h, 0, 0, 0, 60);
    }
    draw_rect_outline(ctx, x, y, w, h, 2.0f,
                      selected ? (uint8_t)255 : (uint8_t)160,
                      selected ? (uint8_t)220 : (uint8_t)160,
                      selected ? (uint8_t)100 : (uint8_t)160, 255);

    // Center text
    float font_h = h * 0.45f;
    float px = font_h / 7.0f;
    int len = 0;
    for (const char* p = label; *p; ++p) ++len;
    float text_w = len * 6.0f * px;
    float tx = x + (w - text_w) / 2.0f;
    float ty = y + (h - font_h) / 2.0f;
    draw_text(ctx, tx, ty, font_h, label, text_r, text_g, text_b, 255);

    return clicked;
}

void ui_end(UIContext& ctx) {
    flush_ui(ctx);
}

} // namespace pebble::ui
