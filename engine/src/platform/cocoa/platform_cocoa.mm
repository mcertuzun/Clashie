#include "pebble/platform/platform.h"
#include <cstdio>
#include <cstdarg>
#include <unistd.h>
#include <mach/mach_time.h>

namespace pebble::platform {

static mach_timebase_info_data_t s_timebase;
static bool s_timebase_initialized = false;

int64_t time_now_ns() {
    if (!s_timebase_initialized) {
        mach_timebase_info(&s_timebase);
        s_timebase_initialized = true;
    }
    uint64_t ticks = mach_absolute_time();
    return static_cast<int64_t>(ticks * s_timebase.numer / s_timebase.denom);
}

void time_sleep_ms(int32_t ms) {
    usleep(static_cast<useconds_t>(ms) * 1000);
}

ThermalState thermal_get_state() {
    // macOS: NSProcessInfo.thermalState — simplified for now
    return ThermalState::NOMINAL;
}

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[INFO] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void log_warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[WARN] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

} // namespace pebble::platform
