// Emscripten/WebGL platform implementation
#ifdef PEBBLE_PLATFORM_WEB

#include "pebble/platform/platform.h"
#include "pebble/core/input.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

// --- Key mapping: DOM key code string → ASCII-based key index ---

static uint8_t dom_key_to_ascii(const char* code) {
    // Letter keys — DOM uses "KeyA", "KeyW", etc.
    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y' && code[3] != '\0' && code[4] == '\0') {
        // Single letter: "KeyA" → 'a', "KeyW" → 'w'
        char ch = code[3];
        if (ch >= 'A' && ch <= 'Z') return (uint8_t)(ch - 'A' + 'a');
    }
    // Digit keys — "Digit1" through "Digit9", "Digit0"
    if (std::strncmp(code, "Digit", 5) == 0 && code[5] != '\0' && code[6] == '\0') {
        return (uint8_t)code[5]; // '0'-'9' ASCII
    }
    // Special keys
    if (std::strcmp(code, "Space") == 0)       return 32;
    if (std::strcmp(code, "Escape") == 0)      return 27;
    if (std::strcmp(code, "F1") == 0)          return 128;
    return 0;
}

// --- WebGL context state ---

struct WebWindowData {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl_context;
    int32_t width;
    int32_t height;
};

static WebWindowData s_web_window;

// --- Emscripten input callbacks ---

static EM_BOOL on_keydown(int event_type, const EmscriptenKeyboardEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    uint8_t key = dom_key_to_ascii(e->code);
    if (key > 0) {
        auto& input = pebble::get_input_state_mut();
        if (!input.keys[key]) {
            input.keys[key] = true;
            input.keys_pressed[key] = true;
        }
    }
    // Prevent default browser behavior for game keys
    return (key > 0) ? EM_TRUE : EM_FALSE;
}

static EM_BOOL on_keyup(int event_type, const EmscriptenKeyboardEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    uint8_t key = dom_key_to_ascii(e->code);
    if (key > 0) {
        auto& input = pebble::get_input_state_mut();
        input.keys[key] = false;
    }
    return (key > 0) ? EM_TRUE : EM_FALSE;
}

static EM_BOOL on_mousemove(int event_type, const EmscriptenMouseEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    auto& input = pebble::get_input_state_mut();
    float new_x = (float)e->targetX;
    float new_y = (float)e->targetY;
    input.mouse_dx += new_x - input.mouse_x;
    input.mouse_dy += new_y - input.mouse_y;
    input.mouse_x = new_x;
    input.mouse_y = new_y;
    return EM_TRUE;
}

static EM_BOOL on_mousedown(int event_type, const EmscriptenMouseEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    auto& input = pebble::get_input_state_mut();
    if (e->button == 0) { // Left
        input.mouse_left_down = true;
        input.mouse_left_pressed = true;
    } else if (e->button == 2) { // Right
        input.mouse_right_down = true;
        input.mouse_right_pressed = true;
    } else if (e->button == 1) { // Middle
        input.mouse_middle_down = true;
    }
    return EM_TRUE;
}

static EM_BOOL on_mouseup(int event_type, const EmscriptenMouseEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    auto& input = pebble::get_input_state_mut();
    if (e->button == 0) {
        input.mouse_left_down = false;
        input.mouse_left_released = true;
    } else if (e->button == 2) {
        input.mouse_right_down = false;
        input.mouse_right_released = true;
    } else if (e->button == 1) {
        input.mouse_middle_down = false;
    }
    return EM_TRUE;
}

static EM_BOOL on_wheel(int event_type, const EmscriptenWheelEvent* e, void* user_data) {
    (void)event_type; (void)user_data;
    auto& input = pebble::get_input_state_mut();
    // Normalize: DOM deltaY is positive for scroll-down
    input.scroll_dy -= (float)e->deltaY * 0.01f;
    return EM_TRUE;
}

namespace pebble::platform {

// --- Window ---

WindowHandle window_create(const WindowConfig& config) {
    s_web_window.width = config.width;
    s_web_window.height = config.height;

    // Set canvas size
    emscripten_set_canvas_element_size("#canvas", config.width, config.height);

    // Create WebGL 2.0 context (= OpenGL ES 3.0)
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = 0;
    attrs.depth = 1;
    attrs.stencil = 0;
    attrs.antialias = 0;
    attrs.premultipliedAlpha = 0;
    attrs.preserveDrawingBuffer = 0;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        log_error("WebGL 2.0 context creation failed (error %d)", ctx);
        return 0;
    }

    EMSCRIPTEN_RESULT res = emscripten_webgl_make_context_current(ctx);
    if (res != EMSCRIPTEN_RESULT_SUCCESS) {
        log_error("Failed to make WebGL context current (error %d)", res);
        return 0;
    }

    s_web_window.gl_context = ctx;

    // Register input callbacks on the canvas
    const char* target = "#canvas";
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_keydown);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_keyup);
    emscripten_set_mousemove_callback(target, nullptr, EM_TRUE, on_mousemove);
    emscripten_set_mousedown_callback(target, nullptr, EM_TRUE, on_mousedown);
    emscripten_set_mouseup_callback(target, nullptr, EM_TRUE, on_mouseup);
    emscripten_set_wheel_callback(target, nullptr, EM_TRUE, on_wheel);

    log_info("WebGL 2.0 window created: %dx%d", config.width, config.height);

    // Return a non-zero handle (use address of static data)
    return reinterpret_cast<WindowHandle>(&s_web_window);
}

void window_destroy(WindowHandle handle) {
    if (!handle) return;
    auto* data = reinterpret_cast<WebWindowData*>(handle);
    if (data->gl_context > 0) {
        emscripten_webgl_destroy_context(data->gl_context);
        data->gl_context = 0;
    }
}

bool window_should_close(WindowHandle) {
    // Browser manages lifetime — the loop runs until the page is closed.
    // With ASYNCIFY, returning false keeps the while loop spinning.
    return false;
}

void window_poll_events(WindowHandle) {
    // Emscripten delivers events via callbacks — nothing to poll.
    // begin_frame() on the input state is called by the game loop.
}

void window_swap_buffers(WindowHandle) {
    // Yield to browser event loop — this is essential with ASYNCIFY.
    // Without this, the infinite while-loop never returns control to the browser
    // and the canvas never gets updated.
    emscripten_sleep(0);
}

void window_get_size(WindowHandle handle, int32_t* w, int32_t* h) {
    if (!handle) { *w = 0; *h = 0; return; }
    int cw = 0, ch = 0;
    emscripten_get_canvas_element_size("#canvas", &cw, &ch);
    *w = static_cast<int32_t>(cw);
    *h = static_cast<int32_t>(ch);
}

// --- Timing ---

int64_t time_now_ns() {
    // emscripten_get_now() returns milliseconds as a double
    double ms = emscripten_get_now();
    return static_cast<int64_t>(ms * 1000000.0);
}

void time_sleep_ms(int32_t ms) {
    // With ASYNCIFY, emscripten_sleep yields to the browser event loop
    emscripten_sleep(static_cast<unsigned int>(ms));
}

// --- Thermal ---

ThermalState thermal_get_state() {
    return ThermalState::NOMINAL;
}

// --- Logging ---

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("[INFO] ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::printf("[WARN] ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::fprintf(stderr, "[ERROR] ");
    std::vfprintf(stderr, fmt, args);
    std::fprintf(stderr, "\n");
    va_end(args);
}

} // namespace pebble::platform

#endif // PEBBLE_PLATFORM_WEB
