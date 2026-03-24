#pragma once

#include <cstdint>
#include <cstring>

namespace pebble {

struct InputState {
    // Mouse
    float mouse_x = 0, mouse_y = 0;          // Screen position
    float mouse_dx = 0, mouse_dy = 0;        // Delta since last frame
    float scroll_dy = 0;                      // Scroll wheel delta
    bool  mouse_left_down = false;
    bool  mouse_left_pressed = false;         // Just pressed this frame
    bool  mouse_left_released = false;        // Just released this frame
    bool  mouse_right_down = false;
    bool  mouse_right_pressed = false;
    bool  mouse_right_released = false;
    bool  mouse_middle_down = false;

    // Keyboard
    bool keys[256];                  // Currently held
    bool keys_pressed[256];          // Just pressed

    InputState() {
        std::memset(keys, 0, sizeof(keys));
        std::memset(keys_pressed, 0, sizeof(keys_pressed));
    }

    void begin_frame();              // Reset per-frame states
};

// Key codes (ASCII-based for simplicity)
enum Key : uint8_t {
    KEY_ESCAPE = 27,
    KEY_SPACE  = 32,
    KEY_1 = 49, KEY_2 = 50, KEY_3 = 51, KEY_4 = 52,
    KEY_5 = 53, KEY_6 = 54, KEY_7 = 55, KEY_8 = 56,
    KEY_A = 97, KEY_C = 99, KEY_D = 100, KEY_E = 101,
    KEY_P = 112, KEY_R = 114, KEY_S = 115, KEY_W = 119,
    KEY_F1 = 128,
};

// Global input state accessor (set by platform layer)
const InputState& get_input_state();

// Called by platform to get a mutable reference (internal use)
InputState& get_input_state_mut();

} // namespace pebble
