#include "pebble/core/input.h"

namespace pebble {

static InputState s_input_state;

void InputState::begin_frame() {
    mouse_dx = 0;
    mouse_dy = 0;
    scroll_dy = 0;
    mouse_left_pressed = false;
    mouse_left_released = false;
    mouse_right_pressed = false;
    mouse_right_released = false;
    std::memset(keys_pressed, 0, sizeof(keys_pressed));
}

const InputState& get_input_state() {
    return s_input_state;
}

InputState& get_input_state_mut() {
    return s_input_state;
}

} // namespace pebble
