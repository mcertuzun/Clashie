// Platform stub — used when building for unsupported platforms or testing core-only
#include "pebble/platform/platform.h"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <thread>

namespace pebble::platform {

WindowHandle window_create(const WindowConfig&) { return 0; }
void window_destroy(WindowHandle) {}
bool window_should_close(WindowHandle) { return false; }
void window_get_size(WindowHandle, int32_t* w, int32_t* h) { *w = 1280; *h = 720; }
void window_poll_events(WindowHandle) {}
void window_swap_buffers(WindowHandle) {}

int64_t time_now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

void time_sleep_ms(int32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

ThermalState thermal_get_state() { return ThermalState::NOMINAL; }

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
