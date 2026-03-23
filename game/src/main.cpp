#include "pebble/platform/platform.h"
#include "pebble/core/fixed_point.h"
#include "pebble/core/memory.h"
#include "pebble/core/input.h"
#include "pebble/gfx/renderer.h"
#include "pebble/gfx/camera.h"
#include "pebble/gfx/sprite_batch.h"
#include "pebble/framework/ui.h"
#include "pebble/framework/particle.h"
#include "pebble/audio/audio.h"
#include "miniclash/simulation.h"
#include "miniclash/replay.h"
#include <cmath>
#include <algorithm>
#include <sys/stat.h>
#include <memory>

using namespace pebble;
using namespace pebble::platform;
using namespace pebble::gfx;
using namespace miniclash;

static constexpr int64_t FIXED_DT_NS = 50'000'000;

// --- Game States ---

enum GameMode {
    MODE_READY,
    MODE_EDITING,
    MODE_ATTACKING,
    MODE_RESULT,
    MODE_REPLAY
};

// --- Army Tracking ---

struct ArmyInventory {
    int barbarians;
    int archers;
    int giants;
    int wall_breakers;

    void reset() {
        barbarians   = 15;
        archers      = 8;
        giants       = 3;
        wall_breakers = 2;
    }

    int count_for(TroopType type) const {
        switch (type) {
            case TroopType::BARBARIAN:    return barbarians;
            case TroopType::ARCHER:       return archers;
            case TroopType::GIANT:        return giants;
            case TroopType::WALL_BREAKER: return wall_breakers;
            default: return 0;
        }
    }

    bool try_deploy(TroopType type) {
        switch (type) {
            case TroopType::BARBARIAN:    if (barbarians > 0)    { --barbarians;    return true; } break;
            case TroopType::ARCHER:       if (archers > 0)       { --archers;       return true; } break;
            case TroopType::GIANT:        if (giants > 0)        { --giants;        return true; } break;
            case TroopType::WALL_BREAKER: if (wall_breakers > 0) { --wall_breakers; return true; } break;
            default: break;
        }
        return false;
    }
};

// --- Editor State ---

struct EditorState {
    BuildingType selected_building = BuildingType::TOWN_HALL;
    bool dragging = false;
    int ghost_x = -1, ghost_y = -1;  // Grid position of ghost building
    TileGrid editor_grid;             // Editor's own grid (separate from sim)
    BaseLayout current_layout;        // Current base layout being edited
    int building_count = 0;

    void clear() {
        editor_grid.clear();
        current_layout = BaseLayout{};
        building_count = 0;
    }

    void load_from_layout(const BaseLayout& layout) {
        clear();
        for (uint16_t i = 0; i < layout.building_count; ++i) {
            const auto& bp = layout.buildings[i];
            BuildingType bt = static_cast<BuildingType>(bp.type);
            const auto& data = get_building_data(bt);
            int sz = data.size;
            if (editor_grid.can_place(bp.grid_x, bp.grid_y, sz, sz)) {
                editor_grid.place(bp.grid_x, bp.grid_y, sz, sz,
                                  (uint16_t)(building_count + 1));
                current_layout.buildings[building_count] = bp;
                building_count++;
                current_layout.building_count = (uint16_t)building_count;
            }
        }
    }

    bool try_place(BuildingType type, int gx, int gy) {
        if (building_count >= 64) return false;
        const auto& data = get_building_data(type);
        int sz = data.size;
        if (!editor_grid.can_place(gx, gy, sz, sz)) return false;
        editor_grid.place(gx, gy, sz, sz, (uint16_t)(building_count + 1));
        auto& bp = current_layout.buildings[building_count];
        bp.type = static_cast<uint8_t>(type);
        bp.level = 1;
        bp.grid_x = (uint8_t)gx;
        bp.grid_y = (uint8_t)gy;
        building_count++;
        current_layout.building_count = (uint16_t)building_count;
        return true;
    }

    bool try_remove_at(int gx, int gy) {
        if (!editor_grid.in_bounds(gx, gy)) return false;
        uint16_t bid = editor_grid.building_id[gy][gx];
        if (bid == 0) return false;
        int idx = (int)bid - 1;
        if (idx < 0 || idx >= building_count) return false;
        // Remove from grid
        const auto& bp = current_layout.buildings[idx];
        BuildingType bt = static_cast<BuildingType>(bp.type);
        const auto& data = get_building_data(bt);
        int sz = data.size;
        editor_grid.remove(bp.grid_x, bp.grid_y, sz, sz);
        // Compact the layout
        for (int i = idx; i < building_count - 1; ++i) {
            current_layout.buildings[i] = current_layout.buildings[i + 1];
        }
        building_count--;
        current_layout.building_count = (uint16_t)building_count;
        // Rebuild grid ids
        editor_grid.clear();
        for (int i = 0; i < building_count; ++i) {
            const auto& b = current_layout.buildings[i];
            BuildingType bt2 = static_cast<BuildingType>(b.type);
            const auto& d2 = get_building_data(bt2);
            int s2 = d2.size;
            editor_grid.place(b.grid_x, b.grid_y, s2, s2, (uint16_t)(i + 1));
        }
        return true;
    }
};

static const char* building_type_name(BuildingType type) {
    switch (type) {
        case BuildingType::TOWN_HALL:      return "Town Hall";
        case BuildingType::CANNON:         return "Cannon";
        case BuildingType::ARCHER_TOWER:   return "Archer Tower";
        case BuildingType::MORTAR:         return "Mortar";
        case BuildingType::GOLD_STORAGE:   return "Gold Storage";
        case BuildingType::ELIXIR_STORAGE: return "Elixir Storage";
        case BuildingType::WALL:           return "Wall";
        case BuildingType::BUILDER_HUT:    return "Builder Hut";
        default:                           return "Unknown";
    }
}

static BuildingType building_slot_type(int slot) {
    switch (slot) {
        case 0: return BuildingType::TOWN_HALL;
        case 1: return BuildingType::CANNON;
        case 2: return BuildingType::ARCHER_TOWER;
        case 3: return BuildingType::MORTAR;
        case 4: return BuildingType::GOLD_STORAGE;
        case 5: return BuildingType::ELIXIR_STORAGE;
        case 6: return BuildingType::WALL;
        case 7: return BuildingType::BUILDER_HUT;
        default: return BuildingType::TOWN_HALL;
    }
}

// --- Procedural Texture Generation ---

// Diamond (rhombus) shaped tile for proper isometric look
static TextureID create_diamond_tile(Renderer* r, uint8_t rv, uint8_t gv, uint8_t bv, uint8_t shade = 20) {
    constexpr int W = 64, H = 32;
    uint8_t px[W * H * 4];
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            // Diamond shape: |x - W/2| / (W/2) + |y - H/2| / (H/2) <= 1
            float dx = std::abs(x - W / 2.0f) / (W / 2.0f);
            float dy = std::abs(y - H / 2.0f) / (H / 2.0f);
            if (dx + dy <= 1.0f) {
                // Inside diamond — subtle gradient for depth
                bool light = ((x + y) / 4) % 2 == 0;
                px[i+0] = light ? rv : (uint8_t)(rv - shade);
                px[i+1] = light ? gv : (uint8_t)(gv - shade);
                px[i+2] = light ? bv : (uint8_t)(bv - shade);
                px[i+3] = 255;
                // Edge highlight
                if (dx + dy > 0.85f) {
                    px[i+0] = (uint8_t)(rv * 0.7f);
                    px[i+1] = (uint8_t)(gv * 0.7f);
                    px[i+2] = (uint8_t)(bv * 0.7f);
                }
            } else {
                px[i+0] = px[i+1] = px[i+2] = 0;
                px[i+3] = 0; // Transparent outside diamond
            }
        }
    }
    return r->texture_create(W, H, px);
}

// Building block — diamond shaped with solid fill and dark border
static TextureID create_building_tile(Renderer* r) {
    constexpr int W = 64, H = 32;
    uint8_t px[W * H * 4];
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int i = (y * W + x) * 4;
            float dx = std::abs(x - W / 2.0f) / (W / 2.0f);
            float dy = std::abs(y - H / 2.0f) / (H / 2.0f);
            if (dx + dy <= 1.0f) {
                bool border = (dx + dy > 0.80f);
                if (border) {
                    px[i+0] = 40; px[i+1] = 40; px[i+2] = 40; px[i+3] = 255;
                } else {
                    px[i+0] = px[i+1] = px[i+2] = 200; px[i+3] = 255;
                }
            } else {
                px[i+0] = px[i+1] = px[i+2] = 0; px[i+3] = 0;
            }
        }
    }
    return r->texture_create(W, H, px);
}

// Troop — circle shape to visually differentiate from buildings
static TextureID create_troop_texture(Renderer* r) {
    constexpr int S = 32;
    uint8_t px[S * S * 4];
    float cx = S / 2.0f, cy = S / 2.0f, rad = S / 2.0f - 1;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int i = (y * S + x) * 4;
            float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d <= rad) {
                bool ring = (d > rad - 2.0f);
                px[i+0] = ring ? 40 : 220;
                px[i+1] = ring ? 40 : 220;
                px[i+2] = ring ? 40 : 220;
                px[i+3] = 255;
            } else {
                px[i+0] = px[i+1] = px[i+2] = 0; px[i+3] = 0;
            }
        }
    }
    return r->texture_create(S, S, px);
}

// Particle — small filled circle (8x8) for particle effects
static TextureID create_particle_texture(Renderer* r) {
    constexpr int S = 8;
    uint8_t px[S * S * 4];
    float cx = S / 2.0f, cy = S / 2.0f, rad = S / 2.0f;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            int i = (y * S + x) * 4;
            float d = std::sqrt((x - cx + 0.5f) * (x - cx + 0.5f) +
                                (y - cy + 0.5f) * (y - cy + 0.5f));
            if (d <= rad) {
                // Soft edge: fade alpha near the rim
                float edge = 1.0f - std::max(0.0f, (d - rad + 1.5f) / 1.5f);
                px[i+0] = 255;
                px[i+1] = 255;
                px[i+2] = 255;
                px[i+3] = (uint8_t)(255.0f * edge);
            } else {
                px[i+0] = px[i+1] = px[i+2] = 0; px[i+3] = 0;
            }
        }
    }
    return r->texture_create(S, S, px);
}

// --- Colors ---

static Color building_color(BuildingType type) {
    switch (type) {
        case BuildingType::TOWN_HALL:      return { 255, 210, 50, 255 };
        case BuildingType::CANNON:         return { 220, 60, 60, 255 };
        case BuildingType::ARCHER_TOWER:   return { 60, 140, 255, 255 };
        case BuildingType::MORTAR:         return { 160, 80, 220, 255 };
        case BuildingType::GOLD_STORAGE:   return { 255, 230, 100, 255 };
        case BuildingType::ELIXIR_STORAGE: return { 200, 60, 230, 255 };
        case BuildingType::WALL:           return { 180, 180, 180, 255 };
        case BuildingType::BUILDER_HUT:    return { 160, 110, 60, 255 };
        default:                           return { 255, 255, 255, 255 };
    }
}

static Color troop_color(TroopType type) {
    switch (type) {
        case TroopType::BARBARIAN:    return { 255, 170, 50, 255 };
        case TroopType::ARCHER:       return { 240, 80, 180, 255 };
        case TroopType::GIANT:        return { 200, 160, 100, 255 };
        case TroopType::WALL_BREAKER: return { 50, 220, 50, 255 };
        default:                      return { 255, 255, 255, 255 };
    }
}

static const char* troop_type_name(TroopType type) {
    switch (type) {
        case TroopType::BARBARIAN:    return "Barbarian";
        case TroopType::ARCHER:       return "Archer";
        case TroopType::GIANT:        return "Giant";
        case TroopType::WALL_BREAKER: return "Wall Breaker";
        default:                      return "Unknown";
    }
}

// --- Test Base ---

static BaseLayout create_test_base() {
    BaseLayout base;
    base.building_count = 0;

    auto add = [&](BuildingType t, uint8_t x, uint8_t y) {
        auto& bp = base.buildings[base.building_count++];
        bp.type = static_cast<uint8_t>(t);
        bp.level = 1; bp.grid_x = x; bp.grid_y = y;
    };

    add(BuildingType::TOWN_HALL, 18, 18);
    add(BuildingType::CANNON, 12, 12);
    add(BuildingType::CANNON, 25, 25);
    add(BuildingType::ARCHER_TOWER, 25, 14);
    add(BuildingType::ARCHER_TOWER, 14, 25);
    add(BuildingType::MORTAR, 19, 13);
    add(BuildingType::GOLD_STORAGE, 13, 18);
    add(BuildingType::ELIXIR_STORAGE, 24, 18);
    add(BuildingType::BUILDER_HUT, 10, 10);

    // Wall ring around town hall
    for (int i = 16; i <= 23; ++i) {
        add(BuildingType::WALL, (uint8_t)i, 16);
        add(BuildingType::WALL, (uint8_t)i, 23);
    }
    for (int i = 17; i < 23; ++i) {
        add(BuildingType::WALL, 16, (uint8_t)i);
        add(BuildingType::WALL, 23, (uint8_t)i);
    }

    return base;
}

// --- Troop slot index for selection ---

static TroopType troop_slot_type(int slot) {
    switch (slot) {
        case 0: return TroopType::BARBARIAN;
        case 1: return TroopType::ARCHER;
        case 2: return TroopType::GIANT;
        case 3: return TroopType::WALL_BREAKER;
        default: return TroopType::BARBARIAN;
    }
}

// === MAIN ===

int main() {
    WindowConfig wc;
    wc.width  = 1280;
    wc.height = 720;
    wc.title  = "MiniClash — Pebble Engine";

    WindowHandle window = window_create(wc);
    if (!window) { log_error("Window creation failed"); return 1; }

    Renderer* renderer = create_renderer();
    RendererConfig rc; rc.window = window; rc.width = wc.width; rc.height = wc.height;
    if (!renderer->init(rc)) { log_error("Renderer init failed"); return 1; }

    SpriteBatch batch;
    batch.init(renderer);

    // Camera — centered on grid center (20,20), zoomed in
    Camera camera;
    camera.set_viewport(wc.width, wc.height);
    camera.set_position(0, 640); // iso_y of center(20,20) = (20+20)*16 = 640
    camera.set_zoom(1.2f);

    // Textures
    TextureID grass_tex = create_diamond_tile(renderer, 110, 160, 80);
    TextureID grass_dark_tex = create_diamond_tile(renderer, 95, 140, 70, 15);
    TextureID building_tex = create_building_tile(renderer);
    TextureID troop_tex = create_troop_texture(renderer);
    TextureID particle_tex = create_particle_texture(renderer);

    // Particle system
    ParticleSystem particles;

    // Audio system
    pebble::audio::AudioEngine audio;
    if (!audio.init()) {
        log_info("Audio init failed — continuing without sound.");
    }

    // The base layout that will be used for attacks (starts with default test base)
    BaseLayout active_base_layout = create_test_base();

    // Simulation
    Simulation sim;
    sim.init();
    sim.load_base(active_base_layout);

    // Game state
    bool paused = false;
    TroopType selected_troop = TroopType::BARBARIAN;
    GameMode game_mode = MODE_READY;

    // Editor state
    EditorState editor;

    // Army inventory
    ArmyInventory army;
    army.reset();

    // Replay system
    static const char* REPLAY_DIR  = "replays";
    static const char* REPLAY_PATH = "replays/last_attack.prep";
    std::unique_ptr<ReplayRecorder> replay_recorder;
    std::unique_ptr<ReplayPlayer>   replay_player;
    uint32_t replay_tick = 0;           // Current tick during replay playback
    float    replay_tick_accum = 0.0f;  // Sub-tick accumulator for speed multiplier

    // UI context
    pebble::ui::UIContext ui_ctx;

    // Camera pan speed (pixels per second at zoom 1.0)
    static constexpr float PAN_SPEED = 400.0f;

    LinearAllocator frame_alloc;
    frame_alloc.init(4 * 1024 * 1024);

    // Profiler state
    bool show_profiler = false;
    static constexpr int FPS_HISTORY_SIZE = 30;
    float frame_times_ms[FPS_HISTORY_SIZE] = {};
    int frame_time_index = 0;
    bool frame_time_filled = false;

    int64_t accumulator = 0;
    int64_t last_time = time_now_ns();

    log_info("=== MiniClash — Pebble Engine v0.1.0 ===");
    log_info("Controls:");
    log_info("  WASD / Right-click drag: Pan camera");
    log_info("  Scroll wheel: Zoom in/out");
    log_info("  E: Edit base (in READY mode)");
    log_info("  SPACE: Start attack (in READY mode)");
    log_info("  Left-click: Deploy selected troop (in ATTACK mode)");
    log_info("  1/2/3/4: Select troop (Barbarian/Archer/Giant/Wall Breaker)");
    log_info("  P: Pause/Resume");
    log_info("  W: Watch replay (in RESULT screen)");
    log_info("  1/2/3/4: Replay speed 1x/2x/4x/8x (in REPLAY mode)");
    log_info("  ESC: Quit");

    while (!window_should_close(window)) {
        int64_t now = time_now_ns();
        int64_t dt = now - last_time;
        last_time = now;
        if (dt > 250'000'000) dt = 250'000'000;

        float dt_sec = (float)dt / 1'000'000'000.0f;
        float dt_ms = (float)dt / 1'000'000.0f;

        // Track frame times for profiler
        frame_times_ms[frame_time_index] = dt_ms;
        frame_time_index = (frame_time_index + 1) % FPS_HISTORY_SIZE;
        if (frame_time_index == 0) frame_time_filled = true;

        window_poll_events(window);

        const auto& input = get_input_state();

        int32_t window_w, window_h;
        window_get_size(window, &window_w, &window_h);

        // --- INPUT HANDLING ---

        // ESC to quit (or exit editor back to ready)
        if (input.keys_pressed[KEY_ESCAPE]) {
            if (game_mode == MODE_EDITING) {
                game_mode = MODE_READY;
                log_info("Editor cancelled. Returning to ready screen.");
            } else {
                break;
            }
        }

        // F1 to toggle profiler overlay
        if (input.keys_pressed[KEY_F1]) {
            show_profiler = !show_profiler;
        }

        // Camera pan via WASD (allowed in all modes)
        {
            float pan_x = 0, pan_y = 0;
            if (input.keys[KEY_W]) pan_y -= 1.0f;
            if (input.keys[KEY_S]) pan_y += 1.0f;
            if (input.keys[KEY_A]) pan_x -= 1.0f;
            if (input.keys[KEY_D]) pan_x += 1.0f;
            if (pan_x != 0 || pan_y != 0) {
                camera.pan(pan_x * PAN_SPEED * dt_sec, pan_y * PAN_SPEED * dt_sec);
            }
        }

        // Camera pan via right-click drag (in editor, use middle-click for pan since right-click removes)
        if (game_mode != MODE_EDITING) {
            if (input.mouse_right_down && (input.mouse_dx != 0 || input.mouse_dy != 0)) {
                camera.pan(-input.mouse_dx, -input.mouse_dy);
            }
        } else {
            if (input.mouse_middle_down && (input.mouse_dx != 0 || input.mouse_dy != 0)) {
                camera.pan(-input.mouse_dx, -input.mouse_dy);
            }
        }

        // Camera zoom via scroll wheel (allowed in all modes)
        if (input.scroll_dy != 0) {
            float new_zoom = camera.zoom() + input.scroll_dy * 0.1f;
            new_zoom = std::max(Camera::MIN_ZOOM, std::min(Camera::MAX_ZOOM, new_zoom));
            camera.set_zoom(new_zoom);
        }

        // --- MODE-SPECIFIC INPUT ---

        if (game_mode == MODE_READY) {
            // E to enter editor mode ('e' = 101)
            if (input.keys_pressed[101]) {
                editor.load_from_layout(active_base_layout);
                game_mode = MODE_EDITING;
                log_info("Entering Base Editor. Left-click to place, right-click to remove.");
                log_info("  1-8: Select building type. SPACE: Confirm and return. C: Clear all.");
            }

            // SPACE to start attack
            if (input.keys_pressed[KEY_SPACE]) {
                game_mode = MODE_ATTACKING;
                sim.start_attack();

                // Start recording replay
                replay_recorder = std::make_unique<ReplayRecorder>();
                replay_recorder->set_seed(12345); // Must match sim's deterministic seed
                replay_recorder->set_engine_version(1);
                replay_recorder->set_base_hash(sim.compute_state_hash());

                audio.play(pebble::audio::SoundID::ATTACK_START);
                log_info("Attack started! Deploy troops with left-click.");
            }
        }
        else if (game_mode == MODE_EDITING) {
            // Building type selection: 1-8
            if (input.keys_pressed[KEY_1]) {
                editor.selected_building = BuildingType::TOWN_HALL;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[KEY_2]) {
                editor.selected_building = BuildingType::CANNON;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[KEY_3]) {
                editor.selected_building = BuildingType::ARCHER_TOWER;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[KEY_4]) {
                editor.selected_building = BuildingType::MORTAR;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            // Keys 5-8: '5'=53, '6'=54, '7'=55, '8'=56
            if (input.keys_pressed[53]) {
                editor.selected_building = BuildingType::GOLD_STORAGE;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[54]) {
                editor.selected_building = BuildingType::ELIXIR_STORAGE;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[55]) {
                editor.selected_building = BuildingType::WALL;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }
            if (input.keys_pressed[56]) {
                editor.selected_building = BuildingType::BUILDER_HUT;
                log_info("[EDITOR] Selected: %s", building_type_name(editor.selected_building));
            }

            // C to clear all ('c' = 99)
            if (input.keys_pressed[99]) {
                editor.clear();
                log_info("[EDITOR] Cleared all buildings.");
            }

            // Update ghost position from mouse
            {
                float world_x, world_y;
                camera.screen_to_world(input.mouse_x, input.mouse_y, world_x, world_y);
                int gx = (int)std::floor(world_x);
                int gy = (int)std::floor(world_y);
                editor.ghost_x = gx;
                editor.ghost_y = gy;
            }

            // Left-click to place building (only if not on bottom panel)
            float editor_bottom_bar_h = 90.0f;
            if (input.mouse_left_pressed && input.mouse_y < window_h - editor_bottom_bar_h) {
                float world_x, world_y;
                camera.screen_to_world(input.mouse_x, input.mouse_y, world_x, world_y);
                int gx = (int)std::floor(world_x);
                int gy = (int)std::floor(world_y);
                if (editor.try_place(editor.selected_building, gx, gy)) {
                    log_info("[EDITOR] Placed %s at (%d, %d) — %d buildings total",
                             building_type_name(editor.selected_building), gx, gy,
                             editor.building_count);
                }
            }

            // Right-click to remove building
            if (input.mouse_right_pressed && input.mouse_y < window_h - editor_bottom_bar_h) {
                float world_x, world_y;
                camera.screen_to_world(input.mouse_x, input.mouse_y, world_x, world_y);
                int gx = (int)std::floor(world_x);
                int gy = (int)std::floor(world_y);
                if (editor.try_remove_at(gx, gy)) {
                    log_info("[EDITOR] Removed building at (%d, %d) — %d buildings remaining",
                             gx, gy, editor.building_count);
                }
            }

            // SPACE to confirm and leave editor
            if (input.keys_pressed[KEY_SPACE]) {
                // Save editor layout as the active base
                active_base_layout = editor.current_layout;

                // Reload sim with new layout
                sim.init();
                sim.load_base(active_base_layout);

                game_mode = MODE_READY;
                army.reset();
                accumulator = 0;
                selected_troop = TroopType::BARBARIAN;
                log_info("[EDITOR] Base saved with %d buildings. Press SPACE to attack or E to edit again.",
                         editor.building_count);
            }
        }
        else if (game_mode == MODE_ATTACKING) {
            // P to toggle pause
            if (input.keys_pressed[KEY_P]) {
                paused = !paused;
                log_info(paused ? "Game PAUSED" : "Game RESUMED");
            }

            // Troop selection: 1/2/3/4
            if (input.keys_pressed[KEY_1]) {
                selected_troop = TroopType::BARBARIAN;
                audio.play(pebble::audio::SoundID::BUTTON_CLICK);
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_2]) {
                selected_troop = TroopType::ARCHER;
                audio.play(pebble::audio::SoundID::BUTTON_CLICK);
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_3]) {
                selected_troop = TroopType::GIANT;
                audio.play(pebble::audio::SoundID::BUTTON_CLICK);
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_4]) {
                selected_troop = TroopType::WALL_BREAKER;
                audio.play(pebble::audio::SoundID::BUTTON_CLICK);
                log_info("Selected: %s", troop_type_name(selected_troop));
            }

            // Left-click to deploy troop (only if attack is active and we have troops)
            if (input.mouse_left_pressed && sim.is_attack_active()) {
                // Check click is not on bottom HUD bar area (below window_h - 80)
                if (input.mouse_y < window_h - 80) {
                    float world_x, world_y;
                    camera.screen_to_world(input.mouse_x, input.mouse_y, world_x, world_y);

                    // Only deploy within grid bounds
                    if (world_x >= 0 && world_x < GRID_SIZE && world_y >= 0 && world_y < GRID_SIZE) {
                        if (army.count_for(selected_troop) > 0) {
                            army.try_deploy(selected_troop);

                            TickInput tick_in;
                            tick_in.tick = sim.current_tick();
                            tick_in.count = 1;
                            tick_in.events[0].type = InputEvent::DEPLOY_TROOP;
                            tick_in.events[0].troop_type = (uint8_t)selected_troop;
                            tick_in.events[0].world_x = float_to_fp(world_x);
                            tick_in.events[0].world_y = float_to_fp(world_y);

                            // Record input for replay before ticking
                            if (replay_recorder) {
                                replay_recorder->record_tick(tick_in);
                            }

                            sim.tick(tick_in);
                            audio.play(pebble::audio::SoundID::TROOP_DEPLOY);

                            log_info("Deployed %s at (%.1f, %.1f) — %d remaining",
                                     troop_type_name(selected_troop), world_x, world_y,
                                     army.count_for(selected_troop));
                        } else {
                            log_info("No %s remaining!", troop_type_name(selected_troop));
                        }
                    }
                }
            }

            // Check if attack finished (sim says done) -> transition to result
            if (sim.result().finished) {
                game_mode = MODE_RESULT;
                paused = false;

                // Finalize and save replay
                if (replay_recorder) {
                    replay_recorder->finalize(sim.compute_state_hash(), sim.current_tick());
                    mkdir(REPLAY_DIR, 0755);
                    replay_recorder->save_to_file(REPLAY_PATH);
                }

                audio.play(sim.result().stars >= 2
                    ? pebble::audio::SoundID::VICTORY
                    : pebble::audio::SoundID::DEFEAT);
                log_info("Attack finished!");
            }
        }
        else if (game_mode == MODE_RESULT) {
            // R to restart (reload base and reset)
            if (input.keys_pressed[KEY_R]) {
                sim.init();
                sim.load_base(active_base_layout);
                army.reset();
                accumulator = 0;
                game_mode = MODE_READY;
                selected_troop = TroopType::BARBARIAN;
                log_info("Restarted! Scout the base and press SPACE to attack.");
            }
            // W to watch replay
            if (input.keys_pressed[KEY_W]) {
                replay_player = std::make_unique<ReplayPlayer>();
                if (replay_player->load_from_file(REPLAY_PATH)) {
                    // Reset simulation and reload the same base
                    sim.init();
                    sim.load_base(active_base_layout);
                    sim.start_attack();

                    replay_tick = 0;
                    replay_tick_accum = 0.0f;
                    replay_player->set_speed(1.0f);
                    game_mode = MODE_REPLAY;
                    log_info("[REPLAY] Playback started (%u ticks, speed: %.0fx)",
                             replay_player->tick_count(), replay_player->speed());
                } else {
                    log_info("[REPLAY] No replay file found!");
                    replay_player.reset();
                }
            }
            // SPACE for new attack (same as restart for now)
            if (input.keys_pressed[KEY_SPACE]) {
                sim.init();
                sim.load_base(active_base_layout);
                army.reset();
                accumulator = 0;
                game_mode = MODE_READY;
                selected_troop = TroopType::BARBARIAN;
                log_info("New attack! Scout the base and press SPACE to attack.");
            }
        }
        else if (game_mode == MODE_REPLAY) {
            // Speed controls: 1/2/3/4 for 1x/2x/4x/8x speed
            if (input.keys_pressed[KEY_1] && replay_player) {
                replay_player->set_speed(1.0f);
                log_info("[REPLAY] Speed: 1x");
            }
            if (input.keys_pressed[KEY_2] && replay_player) {
                replay_player->set_speed(2.0f);
                log_info("[REPLAY] Speed: 2x");
            }
            if (input.keys_pressed[KEY_3] && replay_player) {
                replay_player->set_speed(4.0f);
                log_info("[REPLAY] Speed: 4x");
            }
            if (input.keys_pressed[KEY_4] && replay_player) {
                replay_player->set_speed(8.0f);
                log_info("[REPLAY] Speed: 8x");
            }

            // ESC or R to exit replay early
            if (input.keys_pressed[KEY_R]) {
                game_mode = MODE_RESULT;
                replay_player.reset();
                // Restore sim to finished state for the result screen
                sim.init();
                sim.load_base(active_base_layout);
                log_info("[REPLAY] Playback cancelled.");
            }
        }

        // --- Helper: process simulation events into particles + audio ---
        auto process_sim_events = [&]() {
            for (int ei = 0; ei < sim.event_count(); ++ei) {
                const SimEvent& ev = sim.events()[ei];
                float ex = fp_to_float(ev.position.x);
                float ey = fp_to_float(ev.position.y);
                switch (ev.type) {
                    case SimEvent::BUILDING_DESTROYED: {
                        particles.emit_building_destroyed(ex, ey);
                        // Determine if it was a wall
                        bool was_wall = false;
                        sim.for_each_building([&](const Building& b) {
                            if (b.id == ev.source_id && b.type == BuildingType::WALL)
                                was_wall = true;
                        });
                        audio.play(was_wall
                            ? pebble::audio::SoundID::WALL_BREAK
                            : pebble::audio::SoundID::BUILDING_DESTROYED);
                        break;
                    }
                    case SimEvent::TROOP_KILLED:
                        particles.emit_troop_killed(ex, ey);
                        audio.play(pebble::audio::SoundID::TROOP_DEATH);
                        break;
                    case SimEvent::TROOP_ATTACK:
                        particles.emit_attack_hit(ex, ey);
                        break;
                    case SimEvent::DEFENSE_FIRE: {
                        float tx = ex, ty = ey;
                        BuildingType def_type = BuildingType::CANNON;
                        sim.for_each_troop([&](const Troop& t) {
                            if (t.id == ev.target_id) {
                                tx = fp_to_float(t.pos.x);
                                ty = fp_to_float(t.pos.y);
                            }
                        });
                        sim.for_each_building([&](const Building& b) {
                            if (b.id == ev.source_id) def_type = b.type;
                        });
                        particles.emit_defense_fire(ex, ey, tx, ty);
                        if (def_type == BuildingType::ARCHER_TOWER)
                            audio.play(pebble::audio::SoundID::ARROW_SHOOT, 0.4f);
                        else if (def_type == BuildingType::MORTAR)
                            audio.play(pebble::audio::SoundID::MORTAR_LAUNCH, 0.6f);
                        else
                            audio.play(pebble::audio::SoundID::CANNON_FIRE, 0.5f);
                        break;
                    }
                    case SimEvent::STAR_EARNED:
                        particles.emit_star_earned();
                        audio.play(pebble::audio::SoundID::STAR_EARNED);
                        break;
                }
            }
        };

        // --- SIMULATION ---
        if (game_mode == MODE_ATTACKING && !paused) {
            accumulator += dt;
            while (accumulator >= FIXED_DT_NS) {
                TickInput tick_in;
                tick_in.tick = sim.current_tick();
                tick_in.count = 0;
                sim.tick(tick_in);
                process_sim_events();
                accumulator -= FIXED_DT_NS;
            }

            // Re-check if sim finished after ticking
            if (sim.result().finished) {
                game_mode = MODE_RESULT;
                paused = false;

                // Finalize and save replay
                if (replay_recorder) {
                    replay_recorder->finalize(sim.compute_state_hash(), sim.current_tick());
                    mkdir(REPLAY_DIR, 0755);
                    replay_recorder->save_to_file(REPLAY_PATH);
                }

                audio.play(sim.result().stars >= 2
                    ? pebble::audio::SoundID::VICTORY
                    : pebble::audio::SoundID::DEFEAT);
                log_info("Attack finished!");
            }
        }

        // --- REPLAY SIMULATION ---
        if (game_mode == MODE_REPLAY && replay_player) {
            float speed = replay_player->speed();
            uint32_t total_ticks = replay_player->tick_count();

            // Accumulate real time into simulation ticks, scaled by speed
            replay_tick_accum += ((float)dt / (float)FIXED_DT_NS) * speed;

            while (replay_tick_accum >= 1.0f && replay_tick < total_ticks) {
                TickInput tick_in = replay_player->get_tick_input(replay_tick);
                tick_in.tick = sim.current_tick(); // Ensure tick matches sim's current tick
                sim.tick(tick_in);
                process_sim_events();
                replay_tick++;
                replay_tick_accum -= 1.0f;
            }

            // Check if replay finished
            if (replay_tick >= total_ticks) {
                // Verify state hash
                uint32_t computed_hash = sim.compute_state_hash();
                if (replay_player->verify(computed_hash)) {
                    log_info("[REPLAY] Verified: hash match!");
                } else {
                    log_info("[REPLAY] ERROR: hash mismatch!");
                }

                // Return to result screen
                game_mode = MODE_RESULT;
                replay_player.reset();

                // Re-init sim for the result screen (show clean base)
                sim.init();
                sim.load_base(active_base_layout);
            }
        }

        // --- UPDATE PARTICLES ---
        particles.update(dt_sec);

        // --- RENDER ---
        renderer->begin_frame(0.08f, 0.08f, 0.12f, 1.0f);
        camera.set_viewport(window_w, window_h);
        batch.begin(camera);

        // Ground tiles — checkerboard pattern for visual clarity
        for (int gy = 0; gy < GRID_SIZE; ++gy) {
            for (int gx = 0; gx < GRID_SIZE; ++gx) {
                Sprite tile;
                tile.world_x = (float)gx;
                tile.world_y = (float)gy;
                tile.width = Camera::TILE_WIDTH;
                tile.height = Camera::TILE_HEIGHT;
                tile.pivot_x = 0.5f; tile.pivot_y = 0.5f;
                tile.u0 = 0; tile.v0 = 0; tile.u1 = 1; tile.v1 = 1;
                tile.tint = { 255, 255, 255, 255 };
                tile.atlas = ((gx + gy) % 2 == 0) ? grass_tex : grass_dark_tex;
                tile.layer = LAYER_TERRAIN;
                tile.flip_x = false;
                batch.draw(tile);
            }
        }

        if (game_mode == MODE_EDITING) {
            // --- EDITOR: Draw placed buildings from editor layout ---
            for (int i = 0; i < editor.building_count; ++i) {
                const auto& bp = editor.current_layout.buildings[i];
                BuildingType bt = static_cast<BuildingType>(bp.type);
                Color col = building_color(bt);
                const auto& data = get_building_data(bt);
                int sz = data.size;

                for (int dy = 0; dy < sz; ++dy) {
                    for (int dx = 0; dx < sz; ++dx) {
                        Sprite s;
                        s.world_x = (float)(bp.grid_x + dx);
                        s.world_y = (float)(bp.grid_y + dy);
                        s.width = Camera::TILE_WIDTH;
                        s.height = Camera::TILE_HEIGHT;
                        s.pivot_x = 0.5f; s.pivot_y = 0.5f;
                        s.u0 = 0; s.v0 = 0; s.u1 = 1; s.v1 = 1;
                        s.tint = col;
                        s.atlas = building_tex;
                        s.layer = LAYER_BUILDINGS;
                        s.flip_x = false;
                        batch.draw(s);
                    }
                }
            }

            // --- EDITOR: Draw ghost building at mouse position ---
            {
                int gx = editor.ghost_x;
                int gy = editor.ghost_y;
                const auto& data = get_building_data(editor.selected_building);
                int sz = data.size;
                bool valid = editor.editor_grid.can_place(gx, gy, sz, sz);

                // Only draw ghost if mouse is roughly in bounds
                if (gx >= 0 && gy >= 0 && gx + sz <= GRID_SIZE && gy + sz <= GRID_SIZE) {
                    for (int dy = 0; dy < sz; ++dy) {
                        for (int dx = 0; dx < sz; ++dx) {
                            Sprite s;
                            s.world_x = (float)(gx + dx);
                            s.world_y = (float)(gy + dy);
                            s.width = Camera::TILE_WIDTH;
                            s.height = Camera::TILE_HEIGHT;
                            s.pivot_x = 0.5f; s.pivot_y = 0.5f;
                            s.u0 = 0; s.v0 = 0; s.u1 = 1; s.v1 = 1;
                            // Green if valid, red if invalid, semi-transparent
                            if (valid) {
                                s.tint = { 50, 220, 50, 140 };
                            } else {
                                s.tint = { 220, 50, 50, 140 };
                            }
                            s.atlas = building_tex;
                            s.layer = LAYER_EFFECTS; // Above buildings
                            s.flip_x = false;
                            batch.draw(s);
                        }
                    }
                }
            }
        } else {
            // --- Normal mode: Draw buildings from simulation ---
            sim.for_each_building([&](const Building& b) {
                Color col = building_color(b.type);

                // Darken based on damage
                float hp_pct = fp_to_float(b.hp) / fp_to_float(b.max_hp);
                if (hp_pct < 0.25f) {
                    col.r = (uint8_t)(col.r * 0.4f);
                    col.g = (uint8_t)(col.g * 0.4f);
                    col.b = (uint8_t)(col.b * 0.4f);
                } else if (hp_pct < 0.75f) {
                    col.r = (uint8_t)(col.r * 0.7f);
                    col.g = (uint8_t)(col.g * 0.7f);
                    col.b = (uint8_t)(col.b * 0.7f);
                }

                for (int dy = 0; dy < b.size(); ++dy) {
                    for (int dx = 0; dx < b.size(); ++dx) {
                        Sprite s;
                        s.world_x = (float)(b.grid_x + dx);
                        s.world_y = (float)(b.grid_y + dy);
                        s.width = Camera::TILE_WIDTH;
                        s.height = Camera::TILE_HEIGHT;
                        s.pivot_x = 0.5f; s.pivot_y = 0.5f;
                        s.u0 = 0; s.v0 = 0; s.u1 = 1; s.v1 = 1;
                        s.tint = col;
                        s.atlas = building_tex;
                        s.layer = LAYER_BUILDINGS;
                        s.flip_x = false;
                        batch.draw(s);
                    }
                }
            });

            // Troops (alive only) — drawn as circles
            sim.for_each_troop([&](const Troop& t) {
                Sprite s;
                s.world_x = fp_to_float(t.pos.x);
                s.world_y = fp_to_float(t.pos.y);
                float sz = (t.type == TroopType::GIANT) ? 1.4f : 0.7f;
                s.width = Camera::TILE_WIDTH * sz;
                s.height = Camera::TILE_WIDTH * sz; // Square aspect for circle
                s.pivot_x = 0.5f; s.pivot_y = 0.5f;
                s.u0 = 0; s.v0 = 0; s.u1 = 1; s.v1 = 1;
                s.tint = troop_color(t.type);
                s.atlas = troop_tex;
                s.layer = LAYER_TROOPS;
                s.flip_x = false;
                batch.draw(s);
            });
        }

        // Particles (effects layer)
        particles.draw(batch, particle_tex);

        batch.flush();

        // === UI RENDERING (after batch.flush, before end_frame) ===

        pebble::ui::ui_begin(ui_ctx, renderer, window_w, window_h);
        ui_ctx.mouse_x = input.mouse_x;
        ui_ctx.mouse_y = input.mouse_y;
        ui_ctx.mouse_down = input.mouse_left_down;
        ui_ctx.mouse_pressed = input.mouse_left_pressed;

        if (game_mode == MODE_READY) {
            // --- READY SCREEN ---
            float sw = (float)window_w;
            float sh = (float)window_h;

            // Semi-transparent dark overlay at bottom
            pebble::ui::draw_rect(ui_ctx, 0, sh - 60.0f, sw, 60.0f, 0, 0, 0, 160);

            // Centered banner with two options
            float banner_w = 320.0f, banner_h = 100.0f;
            float banner_x = (sw - banner_w) / 2.0f;
            float banner_y = (sh - banner_h) / 2.0f;

            pebble::ui::draw_rect(ui_ctx, banner_x, banner_y, banner_w, banner_h,
                                  20, 20, 40, 220);
            pebble::ui::draw_rect_outline(ui_ctx, banner_x, banner_y, banner_w, banner_h,
                                          2.0f, 100, 200, 255, 255);

            // --- [E] Edit Base button ---
            float edit_btn_x = banner_x + 20.0f;
            float edit_btn_y = banner_y + 12.0f;
            float edit_btn_w = 130.0f;
            float edit_btn_h = 32.0f;

            pebble::ui::draw_rect(ui_ctx, edit_btn_x, edit_btn_y, edit_btn_w, edit_btn_h,
                                  60, 120, 60, 255);
            pebble::ui::draw_rect_outline(ui_ctx, edit_btn_x, edit_btn_y, edit_btn_w, edit_btn_h,
                                          1.5f, 120, 220, 120, 255);
            // [E] key indicator
            pebble::ui::draw_rect(ui_ctx, edit_btn_x + 8, edit_btn_y + 6, 20, 20,
                                  100, 200, 100, 255);
            pebble::ui::draw_rect_outline(ui_ctx, edit_btn_x + 8, edit_btn_y + 6, 20, 20,
                                          1.5f, 200, 255, 200, 255);
            // "EDIT" label as 4 blocks
            for (int i = 0; i < 4; ++i) {
                pebble::ui::draw_rect(ui_ctx, edit_btn_x + 38 + i * 22.0f, edit_btn_y + 8,
                                      18.0f, 16.0f, 180, 255, 180, 255);
            }

            // --- [SPACE] Attack button ---
            float atk_btn_x = banner_x + 170.0f;
            float atk_btn_y = banner_y + 12.0f;
            float atk_btn_w = 130.0f;
            float atk_btn_h = 32.0f;

            pebble::ui::draw_rect(ui_ctx, atk_btn_x, atk_btn_y, atk_btn_w, atk_btn_h,
                                  80, 80, 160, 255);
            pebble::ui::draw_rect_outline(ui_ctx, atk_btn_x, atk_btn_y, atk_btn_w, atk_btn_h,
                                          1.5f, 150, 150, 255, 255);
            // Space bar shape
            pebble::ui::draw_rect(ui_ctx, atk_btn_x + 8, atk_btn_y + 8, 40, 16,
                                  120, 120, 220, 255);
            pebble::ui::draw_rect_outline(ui_ctx, atk_btn_x + 8, atk_btn_y + 8, 40, 16,
                                          1.5f, 200, 200, 255, 255);
            // "ATK" label as 3 blocks
            for (int i = 0; i < 3; ++i) {
                pebble::ui::draw_rect(ui_ctx, atk_btn_x + 58 + i * 22.0f, atk_btn_y + 8,
                                      18.0f, 16.0f, 180, 180, 255, 255);
            }

            // Building count hint
            float hint_y = banner_y + 54.0f;
            pebble::ui::draw_number(ui_ctx, banner_x + 100, hint_y, 14.0f,
                                    active_base_layout.building_count,
                                    200, 200, 200);
            // "buildings" label as small blocks
            for (int i = 0; i < 4; ++i) {
                pebble::ui::draw_rect(ui_ctx, banner_x + 150 + i * 14.0f, hint_y + 2,
                                      10.0f, 10.0f, 160, 160, 160, 255);
            }

            // Stars above banner
            pebble::ui::draw_stars(ui_ctx, (sw - 108.0f) / 2.0f,
                                   banner_y - 45.0f, 32.0f, 0, 3);
        }
        else if (game_mode == MODE_EDITING) {
            // --- EDITOR UI ---
            float sw = (float)window_w;
            float sh = (float)window_h;

            // --- TOP BAR: "BASE EDITOR" banner ---
            float top_bar_h = 40.0f;
            pebble::ui::draw_rect(ui_ctx, 0, 0, sw, top_bar_h, 20, 60, 20, 200);
            pebble::ui::draw_rect_outline(ui_ctx, 0, 0, sw, top_bar_h,
                                          2.0f, 60, 180, 60, 255);

            // "BASE EDITOR" as 10 colored blocks
            for (int i = 0; i < 10; ++i) {
                pebble::ui::draw_rect(ui_ctx, sw / 2.0f - 110.0f + i * 22.0f, 10.0f,
                                      18.0f, 20.0f, 100, 255, 100, 255);
            }

            // Building count (top-right)
            pebble::ui::draw_number(ui_ctx, sw - 100.0f, 8.0f, 22.0f,
                                    editor.building_count, 255, 255, 255);
            // "/64" indicator
            pebble::ui::draw_rect(ui_ctx, sw - 50.0f, 14.0f, 4.0f, 14.0f,
                                  180, 180, 180, 255); // slash
            pebble::ui::draw_number(ui_ctx, sw - 42.0f, 8.0f, 22.0f,
                                    64, 180, 180, 180);

            // --- BOTTOM PANEL: Building palette ---
            float bottom_bar_h = 90.0f;
            float bottom_bar_y = sh - bottom_bar_h;
            pebble::ui::draw_rect(ui_ctx, 0, bottom_bar_y, sw, bottom_bar_h,
                                  0, 0, 0, 200);
            pebble::ui::draw_rect_outline(ui_ctx, 0, bottom_bar_y, sw, bottom_bar_h,
                                          2.0f, 60, 120, 60, 255);

            // 8 building type buttons
            float btn_w = 60.0f;
            float btn_h = 50.0f;
            float btn_spacing = 8.0f;
            float total_btns_w = 8.0f * btn_w + 7.0f * btn_spacing;
            float btn_start_x = (sw - total_btns_w) / 2.0f;
            float btn_y = bottom_bar_y + 8.0f;

            for (int slot = 0; slot < 8; ++slot) {
                BuildingType slot_type = building_slot_type(slot);
                Color bc = building_color(slot_type);
                bool is_selected = (editor.selected_building == slot_type);
                float bx = btn_start_x + (float)slot * (btn_w + btn_spacing);

                // Draw the button — returns true if clicked
                if (pebble::ui::draw_button(ui_ctx, bx, btn_y, btn_w, btn_h,
                                            bc.r, bc.g, bc.b, is_selected)) {
                    editor.selected_building = slot_type;
                    log_info("[EDITOR] Selected: %s", building_type_name(slot_type));
                }

                // Draw key number on button (1-8)
                pebble::ui::draw_number(ui_ctx, bx + 24.0f, btn_y + 32.0f, 12.0f,
                                        slot + 1, 255, 255, 255);
            }

            // Hint text row below buttons
            float hint_y = btn_y + btn_h + 6.0f;

            // "SPACE: Confirm" hint (left side)
            pebble::ui::draw_rect(ui_ctx, btn_start_x, hint_y, 40.0f, 14.0f,
                                  80, 160, 80, 255);
            pebble::ui::draw_rect_outline(ui_ctx, btn_start_x, hint_y, 40.0f, 14.0f,
                                          1.0f, 150, 255, 150, 255);
            // "DONE" label as 4 blocks
            for (int i = 0; i < 4; ++i) {
                pebble::ui::draw_rect(ui_ctx, btn_start_x + 48 + i * 14.0f, hint_y + 1,
                                      10.0f, 12.0f, 150, 255, 150, 255);
            }

            // "C: Clear" hint (center)
            float clear_x = sw / 2.0f - 40.0f;
            pebble::ui::draw_rect(ui_ctx, clear_x, hint_y, 18.0f, 14.0f,
                                  200, 80, 80, 255);
            pebble::ui::draw_rect_outline(ui_ctx, clear_x, hint_y, 18.0f, 14.0f,
                                          1.0f, 255, 150, 150, 255);
            // "CLEAR" label as 5 blocks
            for (int i = 0; i < 5; ++i) {
                pebble::ui::draw_rect(ui_ctx, clear_x + 24 + i * 14.0f, hint_y + 1,
                                      10.0f, 12.0f, 255, 150, 150, 255);
            }

            // "ESC: Cancel" hint (right side)
            float esc_x = sw - btn_start_x - 140.0f;
            pebble::ui::draw_rect(ui_ctx, esc_x, hint_y, 28.0f, 14.0f,
                                  120, 120, 120, 255);
            pebble::ui::draw_rect_outline(ui_ctx, esc_x, hint_y, 28.0f, 14.0f,
                                          1.0f, 200, 200, 200, 255);
            // "CANCEL" label as 6 blocks
            for (int i = 0; i < 6; ++i) {
                pebble::ui::draw_rect(ui_ctx, esc_x + 34 + i * 14.0f, hint_y + 1,
                                      10.0f, 12.0f, 200, 200, 200, 255);
            }

            // --- Selected building info (top-left, below top bar) ---
            float info_y = top_bar_h + 8.0f;
            pebble::ui::draw_rect(ui_ctx, 8.0f, info_y, 160.0f, 28.0f,
                                  0, 0, 0, 160);
            Color sel_col = building_color(editor.selected_building);
            pebble::ui::draw_rect(ui_ctx, 12.0f, info_y + 4, 20.0f, 20.0f,
                                  sel_col.r, sel_col.g, sel_col.b, 255);
            pebble::ui::draw_rect_outline(ui_ctx, 12.0f, info_y + 4, 20.0f, 20.0f,
                                          1.5f, 255, 255, 255, 255);
            // Building size indicator
            const auto& sel_data = get_building_data(editor.selected_building);
            pebble::ui::draw_number(ui_ctx, 40.0f, info_y + 6, 16.0f,
                                    sel_data.size, 255, 255, 255);
            // "x" separator
            pebble::ui::draw_rect(ui_ctx, 58.0f, info_y + 10, 6.0f, 6.0f,
                                  200, 200, 200, 255);
            pebble::ui::draw_number(ui_ctx, 70.0f, info_y + 6, 16.0f,
                                    sel_data.size, 255, 255, 255);
        }
        else if (game_mode == MODE_ATTACKING) {
            // --- TOP BAR ---
            float top_bar_h = 50.0f;
            pebble::ui::draw_rect(ui_ctx, 0, 0, (float)window_w, top_bar_h,
                                  0, 0, 0, 180);

            // Left: Stars (earned = gold, unearned = gray)
            int earned_stars = sim.result().stars;
            pebble::ui::draw_stars(ui_ctx, 10.0f, 10.0f, 30.0f,
                                   earned_stars, 3);

            // Center: Timer countdown (MM:SS)
            uint32_t ticks_remaining = 0;
            if (sim.current_tick() < Simulation::MAX_ATTACK_TICKS) {
                ticks_remaining = Simulation::MAX_ATTACK_TICKS - sim.current_tick();
            }
            int seconds_remaining = (int)(ticks_remaining / 20);
            float timer_h = 36.0f;
            float timer_x = ((float)window_w - 120.0f) / 2.0f;
            pebble::ui::draw_timer(ui_ctx, timer_x, 7.0f, timer_h,
                                   seconds_remaining,
                                   255, 255, 255);

            // Right: Destruction percentage
            int destruction = sim.result().destruction_percent;
            float dest_x = (float)window_w - 120.0f;
            pebble::ui::draw_number(ui_ctx, dest_x, 10.0f, 28.0f,
                                    destruction,
                                    255, 200, 50);
            float pct_x = dest_x + 80.0f;
            pebble::ui::draw_rect(ui_ctx, pct_x, 18.0f, 12.0f, 12.0f,
                                  255, 200, 50, 255);

            // --- BOTTOM BAR ---
            float bottom_bar_h = 70.0f;
            float bottom_bar_y = (float)window_h - bottom_bar_h;
            pebble::ui::draw_rect(ui_ctx, 0, bottom_bar_y,
                                  (float)window_w, bottom_bar_h,
                                  0, 0, 0, 180);

            // Troop selection buttons (4 slots)
            float btn_w = 60.0f;
            float btn_h = 50.0f;
            float btn_spacing = 10.0f;
            float btn_start_x = 20.0f;
            float btn_y = bottom_bar_y + 10.0f;

            for (int slot = 0; slot < 4; ++slot) {
                TroopType slot_type = troop_slot_type(slot);
                Color tc = troop_color(slot_type);
                bool is_selected = (selected_troop == slot_type);
                float bx = btn_start_x + (float)slot * (btn_w + btn_spacing);

                if (pebble::ui::draw_button(ui_ctx, bx, btn_y, btn_w, btn_h,
                                            tc.r, tc.g, tc.b, is_selected)) {
                    selected_troop = slot_type;
                }

                int remaining = army.count_for(slot_type);
                pebble::ui::draw_number(ui_ctx, bx + 18.0f, btn_y + 30.0f, 14.0f,
                                        remaining,
                                        255, 255, 255);

                if (remaining == 0) {
                    pebble::ui::draw_rect(ui_ctx, bx, btn_y, btn_w, btn_h,
                                          0, 0, 0, 150);
                }
            }

            // "END ATTACK" button on far right (red)
            float end_btn_w = 100.0f;
            float end_btn_h = 50.0f;
            float end_btn_x = (float)window_w - end_btn_w - 20.0f;
            float end_btn_y = bottom_bar_y + 10.0f;

            if (pebble::ui::draw_button(ui_ctx, end_btn_x, end_btn_y,
                                        end_btn_w, end_btn_h,
                                        200, 40, 40, false)) {
                game_mode = MODE_RESULT;

                if (replay_recorder) {
                    replay_recorder->finalize(sim.compute_state_hash(), sim.current_tick());
                    mkdir(REPLAY_DIR, 0755);
                    replay_recorder->save_to_file(REPLAY_PATH);
                }

                log_info("Attack ended early by player.");
            }

            // --- HEALTH BARS ON BUILDINGS ---
            sim.for_each_building([&](const Building& b) {
                float hp_ratio = fp_to_float(b.hp) / fp_to_float(b.max_hp);
                if (hp_ratio < 1.0f) {
                    Vec2fp center = b.center();
                    float center_wx = fp_to_float(center.x);
                    float center_wy = fp_to_float(center.y);

                    float sx, sy;
                    camera.world_to_screen(center_wx, center_wy, sx, sy);

                    float bar_w = 40.0f;
                    float bar_h = 6.0f;
                    pebble::ui::draw_health_bar(ui_ctx,
                                                sx - bar_w / 2.0f,
                                                sy - 20.0f,
                                                bar_w, bar_h,
                                                hp_ratio);
                }
            });
        }
        else if (game_mode == MODE_RESULT) {
            // --- RESULT SCREEN ---
            float sw = (float)window_w;
            float sh = (float)window_h;

            // Dark overlay
            pebble::ui::draw_rect(ui_ctx, 0, 0, sw, sh, 0, 0, 0, 180);

            const auto& res = sim.result();

            // Title bar
            float title_h = 36.0f;
            float title_y = sh * 0.18f;
            pebble::ui::draw_rect(ui_ctx, sw * 0.25f, title_y, sw * 0.5f, title_h,
                                  40, 40, 60, 220);
            pebble::ui::draw_rect_outline(ui_ctx, sw * 0.25f, title_y, sw * 0.5f, title_h,
                                          2.0f, 100, 100, 200, 255);

            // Big stars
            float star_size = 60.0f;
            float stars_x = (sw - star_size * 3.6f) / 2.0f;
            float stars_y = sh * 0.30f;
            pebble::ui::draw_stars(ui_ctx, stars_x, stars_y, star_size, res.stars, 3);

            // Destruction percentage
            float pct_y = stars_y + star_size + 25.0f;
            pebble::ui::draw_number(ui_ctx, sw / 2.0f - 50.0f, pct_y, 48.0f,
                                    res.destruction_percent, 255, 220, 50);
            float pct_x = sw / 2.0f + 55.0f;
            pebble::ui::draw_rect(ui_ctx, pct_x, pct_y + 4, 10, 10, 255, 220, 50, 255);
            pebble::ui::draw_rect(ui_ctx, pct_x + 14, pct_y + 28, 10, 10, 255, 220, 50, 255);
            pebble::ui::draw_rect(ui_ctx, pct_x + 6, pct_y + 14, 12, 4, 255, 220, 50, 255);

            // --- Button: [R] RESTART ---
            float btn_w = 220.0f, btn_h = 44.0f;
            float btn_x = (sw - btn_w) / 2.0f;
            float btn1_y = pct_y + 80.0f;

            pebble::ui::draw_rect(ui_ctx, btn_x, btn1_y, btn_w, btn_h, 50, 50, 120, 240);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x, btn1_y, btn_w, btn_h, 2.0f,
                                          120, 120, 255, 255);
            pebble::ui::draw_rect(ui_ctx, btn_x + 12, btn1_y + 8, 28, 28, 100, 100, 220, 255);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x + 12, btn1_y + 8, 28, 28, 1.5f,
                                          200, 200, 255, 255);
            float rx = btn_x + 18, ry = btn1_y + 12;
            pebble::ui::draw_rect(ui_ctx, rx, ry, 3, 20, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, rx + 3, ry, 10, 3, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, rx + 13, ry + 3, 3, 6, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, rx + 3, ry + 8, 10, 3, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, rx + 8, ry + 11, 3, 9, 255, 255, 255, 255);

            float lx = btn_x + 55;
            const uint8_t lr = 200, lg = 200, lb = 255;
            for (int i = 0; i < 7; ++i) {
                pebble::ui::draw_rect(ui_ctx, lx + i * 22.0f, btn1_y + 13, 18, 18, lr, lg, lb, 255);
            }

            // --- Button: [SPACE] NEW ATTACK ---
            float btn2_y = btn1_y + btn_h + 12.0f;

            pebble::ui::draw_rect(ui_ctx, btn_x, btn2_y, btn_w, btn_h, 50, 120, 50, 240);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x, btn2_y, btn_w, btn_h, 2.0f,
                                          120, 255, 120, 255);
            pebble::ui::draw_rect(ui_ctx, btn_x + 12, btn2_y + 12, 50, 20, 100, 220, 100, 255);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x + 12, btn2_y + 12, 50, 20, 1.5f,
                                          200, 255, 200, 255);

            float lx2 = btn_x + 75;
            const uint8_t lr2 = 200, lg2 = 255, lb2 = 200;
            for (int i = 0; i < 8; ++i) {
                float w2 = (i == 3) ? 8.0f : 18.0f;
                pebble::ui::draw_rect(ui_ctx, lx2, btn2_y + 13, w2, 18, lr2, lg2, lb2, (i == 3) ? (uint8_t)0 : (uint8_t)255);
                lx2 += w2 + 4.0f;
            }

            // --- Button: [W] WATCH REPLAY ---
            float btn3_y = btn2_y + btn_h + 12.0f;

            pebble::ui::draw_rect(ui_ctx, btn_x, btn3_y, btn_w, btn_h, 120, 50, 120, 240);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x, btn3_y, btn_w, btn_h, 2.0f,
                                          200, 120, 255, 255);
            pebble::ui::draw_rect(ui_ctx, btn_x + 12, btn3_y + 8, 28, 28, 180, 100, 220, 255);
            pebble::ui::draw_rect_outline(ui_ctx, btn_x + 12, btn3_y + 8, 28, 28, 1.5f,
                                          220, 180, 255, 255);
            float wx = btn_x + 18, wy = btn3_y + 12;
            pebble::ui::draw_rect(ui_ctx, wx, wy, 3, 16, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, wx + 6, wy + 10, 3, 6, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, wx + 10, wy + 10, 3, 6, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, wx + 16, wy, 3, 16, 255, 255, 255, 255);
            pebble::ui::draw_rect(ui_ctx, wx + 3, wy + 16, 14, 3, 255, 255, 255, 255);

            float lx3 = btn_x + 55;
            const uint8_t lr3 = 220, lg3 = 180, lb3 = 255;
            for (int i = 0; i < 6; ++i) {
                pebble::ui::draw_rect(ui_ctx, lx3 + i * 22.0f, btn3_y + 13, 18, 18, lr3, lg3, lb3, 255);
            }
        }
        else if (game_mode == MODE_REPLAY) {
            // --- REPLAY HUD ---
            float sw = (float)window_w;

            float top_bar_h = 50.0f;
            pebble::ui::draw_rect(ui_ctx, 0, 0, sw, top_bar_h, 0, 0, 0, 180);

            int earned_stars = sim.result().stars;
            pebble::ui::draw_stars(ui_ctx, 10.0f, 10.0f, 30.0f, earned_stars, 3);

            uint32_t ticks_remaining = 0;
            if (sim.current_tick() < Simulation::MAX_ATTACK_TICKS) {
                ticks_remaining = Simulation::MAX_ATTACK_TICKS - sim.current_tick();
            }
            int seconds_remaining = (int)(ticks_remaining / 20);
            float timer_h = 36.0f;
            float timer_x = (sw - 120.0f) / 2.0f;
            pebble::ui::draw_timer(ui_ctx, timer_x, 7.0f, timer_h,
                                   seconds_remaining, 255, 255, 255);

            int destruction = sim.result().destruction_percent;
            float dest_x = sw - 120.0f;
            pebble::ui::draw_number(ui_ctx, dest_x, 10.0f, 28.0f,
                                    destruction, 255, 200, 50);
            pebble::ui::draw_rect(ui_ctx, dest_x + 80.0f, 18.0f, 12.0f, 12.0f,
                                  255, 200, 50, 255);

            // REPLAY BANNER
            float banner_w = 140.0f, banner_h = 32.0f;
            float banner_x = sw - banner_w - 10.0f;
            float banner_y = top_bar_h + 8.0f;

            pebble::ui::draw_rect(ui_ctx, banner_x, banner_y, banner_w, banner_h,
                                  180, 40, 40, 220);
            pebble::ui::draw_rect_outline(ui_ctx, banner_x, banner_y, banner_w, banner_h,
                                          2.0f, 255, 100, 100, 255);

            for (int i = 0; i < 6; ++i) {
                pebble::ui::draw_rect(ui_ctx, banner_x + 10 + i * 20.0f, banner_y + 7,
                                      16.0f, 18.0f, 255, 220, 220, 255);
            }

            // Speed indicator
            float speed_y = banner_y + banner_h + 6.0f;
            float current_speed = replay_player ? replay_player->speed() : 1.0f;
            int speed_int = (int)current_speed;

            pebble::ui::draw_rect(ui_ctx, banner_x, speed_y, banner_w, 22.0f,
                                  0, 0, 0, 160);

            for (int i = 0; i < speed_int && i < 8; ++i) {
                pebble::ui::draw_rect(ui_ctx, banner_x + 6 + i * 16.0f, speed_y + 4,
                                      12.0f, 14.0f, 100, 255, 100, 255);
            }

            // Bottom bar
            float bottom_bar_h = 40.0f;
            float bottom_bar_y = (float)window_h - bottom_bar_h;
            pebble::ui::draw_rect(ui_ctx, 0, bottom_bar_y, sw, bottom_bar_h,
                                  0, 0, 0, 160);

            for (int i = 0; i < 4; ++i) {
                float kx = 20.0f + i * 50.0f;
                float ky = bottom_bar_y + 8.0f;
                bool active = (speed_int == (1 << i));
                uint8_t kr = active ? (uint8_t)100 : (uint8_t)60;
                uint8_t kg = active ? (uint8_t)255 : (uint8_t)100;
                uint8_t kb = active ? (uint8_t)100 : (uint8_t)60;
                pebble::ui::draw_rect(ui_ctx, kx, ky, 24.0f, 24.0f, kr, kg, kb, 255);
                pebble::ui::draw_rect_outline(ui_ctx, kx, ky, 24.0f, 24.0f, 1.5f,
                                              160, 255, 160, 255);
            }

            // Health bars
            sim.for_each_building([&](const Building& b) {
                float hp_ratio = fp_to_float(b.hp) / fp_to_float(b.max_hp);
                if (hp_ratio < 1.0f) {
                    Vec2fp center = b.center();
                    float center_wx = fp_to_float(center.x);
                    float center_wy = fp_to_float(center.y);

                    float sx, sy;
                    camera.world_to_screen(center_wx, center_wy, sx, sy);

                    float bar_w = 40.0f;
                    float bar_h = 6.0f;
                    pebble::ui::draw_health_bar(ui_ctx,
                                                sx - bar_w / 2.0f,
                                                sy - 20.0f,
                                                bar_w, bar_h,
                                                hp_ratio);
                }
            });
        }

        // --- PROFILER OVERLAY ---
        if (show_profiler) {
            // Calculate average frame time and FPS
            int sample_count = frame_time_filled ? FPS_HISTORY_SIZE : frame_time_index;
            float avg_frame_ms = 0.0f;
            if (sample_count > 0) {
                for (int i = 0; i < sample_count; ++i) avg_frame_ms += frame_times_ms[i];
                avg_frame_ms /= (float)sample_count;
            }
            int fps = (avg_frame_ms > 0.0f) ? (int)(1000.0f / avg_frame_ms + 0.5f) : 0;
            int frame_ms_int = (int)(avg_frame_ms * 10.0f);

            int troop_count = 0;
            int building_count = 0;
            sim.for_each_troop([&](const Troop&) { ++troop_count; });
            sim.for_each_building([&](const Building&) { ++building_count; });

            int sprites = (int)batch.sprite_count();
            int draws = (int)batch.draw_call_count();
            int tick = (int)sim.current_tick();
            int alloc_used_kb = (int)(frame_alloc.used() / 1024);
            int alloc_cap_kb = (int)(frame_alloc.capacity() / 1024);

            float panel_w = 260.0f;
            float panel_h = 140.0f;
            float panel_x = (float)window_w - panel_w - 10.0f;
            float panel_y = 10.0f;
            float row_h = 20.0f;
            float pad_x = 10.0f;
            float label_w = 8.0f;

            pebble::ui::draw_rect(ui_ctx, panel_x, panel_y, panel_w, panel_h,
                                  10, 10, 20, 200);
            pebble::ui::draw_rect_outline(ui_ctx, panel_x, panel_y, panel_w, panel_h,
                                          1.5f, 80, 180, 255, 200);

            pebble::ui::draw_rect(ui_ctx, panel_x, panel_y, panel_w, 18.0f,
                                  30, 60, 120, 220);
            for (int i = 0; i < 8; ++i) {
                pebble::ui::draw_rect(ui_ctx, panel_x + 8 + i * 14.0f, panel_y + 3,
                                      11.0f, 12.0f, 150, 220, 255, 255);
            }

            float row_y = panel_y + 22.0f;

            pebble::ui::draw_rect(ui_ctx, panel_x + pad_x, row_y + 3, label_w, label_w,
                                  100, 255, 100, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + pad_x + 14.0f, row_y, 16.0f,
                                    fps, 100, 255, 100);
            pebble::ui::draw_rect(ui_ctx, panel_x + 110.0f, row_y + 3, label_w, label_w,
                                  200, 200, 100, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + 124.0f, row_y, 16.0f,
                                    frame_ms_int / 10, 200, 200, 100);
            pebble::ui::draw_rect(ui_ctx, panel_x + 174.0f, row_y + 10.0f, 3.0f, 3.0f,
                                  200, 200, 100, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + 180.0f, row_y, 16.0f,
                                    frame_ms_int % 10, 200, 200, 100);
            pebble::ui::draw_rect(ui_ctx, panel_x + 200.0f, row_y + 3, 12.0f, 6.0f,
                                  200, 200, 100, 180);
            pebble::ui::draw_rect(ui_ctx, panel_x + 214.0f, row_y + 3, 12.0f, 6.0f,
                                  200, 200, 100, 180);

            row_y += row_h;

            pebble::ui::draw_rect(ui_ctx, panel_x + pad_x, row_y + 3, label_w, label_w,
                                  100, 180, 255, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + pad_x + 14.0f, row_y, 16.0f,
                                    sprites, 100, 180, 255);
            pebble::ui::draw_rect(ui_ctx, panel_x + 110.0f, row_y + 3, label_w, label_w,
                                  255, 160, 100, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + 124.0f, row_y, 16.0f,
                                    draws, 255, 160, 100);

            row_y += row_h;

            pebble::ui::draw_rect(ui_ctx, panel_x + pad_x, row_y + 3, label_w, label_w,
                                  220, 220, 100, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + pad_x + 14.0f, row_y, 16.0f,
                                    tick, 220, 220, 100);

            row_y += row_h;

            pebble::ui::draw_rect(ui_ctx, panel_x + pad_x, row_y + 3, label_w, label_w,
                                  180, 130, 255, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + pad_x + 14.0f, row_y, 16.0f,
                                    alloc_used_kb, 180, 130, 255);
            pebble::ui::draw_rect(ui_ctx, panel_x + 100.0f, row_y + 2, 3.0f, 14.0f,
                                  180, 130, 255, 180);
            pebble::ui::draw_number(ui_ctx, panel_x + 110.0f, row_y, 16.0f,
                                    alloc_cap_kb, 180, 130, 255);
            pebble::ui::draw_rect(ui_ctx, panel_x + 200.0f, row_y + 3, 12.0f, 6.0f,
                                  180, 130, 255, 180);

            row_y += row_h;

            pebble::ui::draw_rect(ui_ctx, panel_x + pad_x, row_y + 3, label_w, label_w,
                                  255, 170, 50, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + pad_x + 14.0f, row_y, 16.0f,
                                    troop_count, 255, 170, 50);
            pebble::ui::draw_rect(ui_ctx, panel_x + 110.0f, row_y + 3, label_w, label_w,
                                  50, 200, 255, 255);
            pebble::ui::draw_number(ui_ctx, panel_x + 124.0f, row_y, 16.0f,
                                    building_count, 50, 200, 255);
        }

        pebble::ui::ui_end(ui_ctx);

        renderer->end_frame();
        frame_alloc.reset();

        // Status log
        if (game_mode == MODE_ATTACKING && sim.is_attack_active() && sim.current_tick() % 200 == 0) {
            auto& r2 = sim.result();
            int secs_left = (int)(Simulation::MAX_ATTACK_TICKS - sim.current_tick()) / 20;
            log_info("[%d:%02d] %d%% destroyed, %d stars",
                     secs_left / 60, secs_left % 60,
                     r2.destruction_percent, r2.stars);
        }
    }

    auto& res = sim.result();
    log_info("=== RESULT: %d stars, %d%% destruction ===", res.stars, res.destruction_percent);

    audio.shutdown();
    renderer->shutdown();
    delete renderer;
    window_destroy(window);
    return 0;
}
