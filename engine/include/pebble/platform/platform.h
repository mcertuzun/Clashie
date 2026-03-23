#pragma once

#include "pebble/core/types.h"
#include <cstdint>

namespace pebble::platform {

struct WindowConfig {
    int32_t width       = 1280;
    int32_t height      = 720;
    const char* title   = "MiniClash";
    bool fullscreen     = false;
    bool vsync          = true;
};

enum class ThermalState : uint8_t {
    NOMINAL  = 0,
    FAIR     = 1,
    SERIOUS  = 2,
    CRITICAL = 3
};

// Window
WindowHandle  window_create(const WindowConfig& config);
void          window_destroy(WindowHandle handle);
bool          window_should_close(WindowHandle handle);
void          window_poll_events(WindowHandle handle);
void          window_swap_buffers(WindowHandle handle);

// Window size (logical points, not pixels — for UI/input)
void          window_get_size(WindowHandle handle, int32_t* w, int32_t* h);

// Timing
int64_t       time_now_ns();
void          time_sleep_ms(int32_t ms);

// Thermal (mobile only, no-op on desktop)
ThermalState  thermal_get_state();

// Logging
void          log_info(const char* fmt, ...);
void          log_warn(const char* fmt, ...);
void          log_error(const char* fmt, ...);

} // namespace pebble::platform
