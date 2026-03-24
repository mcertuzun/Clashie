#pragma once

#include "pebble/gfx/renderer.h"
#include <cstdint>

namespace pebble::ui {

struct UIContext {
    gfx::Renderer* renderer = nullptr;
    int32_t screen_w = 0, screen_h = 0;
    // Mouse state for button interaction
    float mouse_x = 0.0f, mouse_y = 0.0f;
    bool mouse_down = false, mouse_pressed = false;
    // Internal: 1x1 white texture for solid color rendering
    TextureID white_tex = 0;
};

void ui_begin(UIContext& ctx, gfx::Renderer* renderer, int32_t w, int32_t h);

// Drawing primitives (screen space, top-left origin)
void draw_rect(UIContext& ctx, float x, float y, float w, float h,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
void draw_rect_outline(UIContext& ctx, float x, float y, float w, float h,
                       float thickness, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

// Health bar
void draw_health_bar(UIContext& ctx, float x, float y, float w, float h,
                     float hp_ratio); // 0.0 to 1.0, red->yellow->green

// Star display (filled vs empty)
void draw_stars(UIContext& ctx, float x, float y, float size, int earned, int total);

// Progress bar
void draw_progress_bar(UIContext& ctx, float x, float y, float w, float h,
                       float ratio, uint8_t r, uint8_t g, uint8_t b);

// Simple button — returns true if clicked
bool draw_button(UIContext& ctx, float x, float y, float w, float h,
                 uint8_t r, uint8_t g, uint8_t b, bool selected = false);

// Number display (using colored blocks to approximate digits)
void draw_number(UIContext& ctx, float x, float y, float digit_h, int number,
                 uint8_t r, uint8_t g, uint8_t b);

// Timer display (MM:SS format using block digits)
void draw_timer(UIContext& ctx, float x, float y, float h, int seconds_remaining,
                uint8_t r, uint8_t g, uint8_t b);

// Text rendering (5x7 bitmap font, uppercase + digits + basic punctuation)
// Returns width consumed in pixels
float draw_text(UIContext& ctx, float x, float y, float char_h, const char* text,
                uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

// Labeled button with text — returns true if clicked
bool draw_text_button(UIContext& ctx, float x, float y, float w, float h,
                      const char* label, uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                      uint8_t text_r = 255, uint8_t text_g = 255, uint8_t text_b = 255,
                      bool selected = false);

void ui_end(UIContext& ctx);

} // namespace pebble::ui
