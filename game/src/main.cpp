#include "pebble/platform/platform.h"
#include "pebble/core/fixed_point.h"
#include "pebble/core/memory.h"

using namespace pebble;
using namespace pebble::platform;

// Fixed timestep: 20 ticks/second (matching CoC's simulation rate)
static constexpr int64_t FIXED_DT_NS     = 50'000'000;  // 50ms = 1/20s
static constexpr fp32    FIXED_DT_FP     = PFP_ONE / 20; // 1/20 in fixed-point
// static constexpr int64_t TARGET_FRAME_NS = 16'666'667;  // ~60 FPS (used when vsync is off)

struct GameState {
    uint32_t tick = 0;
    bool     running = true;
    bool     paused = false;
};

static void game_simulation_tick(GameState& state, fp32 /*dt*/) {
    state.tick++;
}

int main() {
    // Create window
    WindowConfig wc;
    wc.width  = 1280;
    wc.height = 720;
    wc.title  = "MiniClash — Pebble Engine";
    wc.vsync  = true;

    WindowHandle window = window_create(wc);
    if (!window) {
        log_error("Failed to create window");
        return 1;
    }

    // Initialize allocators
    LinearAllocator frame_alloc;
    frame_alloc.init(4 * 1024 * 1024);  // 4 MB frame scratch

    GameState state;
    int64_t accumulator = 0;
    int64_t last_time = time_now_ns();

    log_info("Pebble Engine v0.1.0 — MiniClash");
    log_info("Simulation tick rate: 20 Hz");
    log_info("Render target: 60 FPS");

    // --- Main Loop ---
    while (state.running && !window_should_close(window)) {
        int64_t current_time = time_now_ns();
        int64_t frame_delta = current_time - last_time;
        last_time = current_time;

        // Clamp frame delta to prevent spiral of death (e.g., breakpoint resume)
        if (frame_delta > 250'000'000) {
            frame_delta = 250'000'000; // Max 250ms
        }

        // 1. Platform input polling
        window_poll_events(window);

        // 2. Fixed timestep game logic (deterministic)
        if (!state.paused) {
            accumulator += frame_delta;
            while (accumulator >= FIXED_DT_NS) {
                // replay_record_frame_inputs(); // TODO: Week 9-10
                game_simulation_tick(state, FIXED_DT_FP);
                accumulator -= FIXED_DT_NS;
            }
        }

        // 3. Interpolated rendering (variable rate)
        // float alpha = static_cast<float>(accumulator) / FIXED_DT_NS;
        // render_interpolated(alpha); // TODO: Week 3-4

        // 4. UI overlay
        // ui_render(); // TODO: Week 5-6

        // 5. Present
        window_swap_buffers(window);

        // 6. Frame profiling
        frame_alloc.reset();

        // TODO: profiler_end_frame();
    }

    log_info("Shutting down — %u simulation ticks completed", state.tick);
    window_destroy(window);
    return 0;
}
