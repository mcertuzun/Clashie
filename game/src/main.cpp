#include "pebble/platform/platform.h"
#include "pebble/core/fixed_point.h"
#include "pebble/core/memory.h"
#include "pebble/core/input.h"
#include "pebble/gfx/renderer.h"
#include "pebble/gfx/camera.h"
#include "pebble/gfx/sprite_batch.h"
#include "pebble/framework/ui.h"
#include "miniclash/simulation.h"
#include <cmath>
#include <algorithm>

using namespace pebble;
using namespace pebble::platform;
using namespace pebble::gfx;
using namespace miniclash;

static constexpr int64_t FIXED_DT_NS = 50'000'000;

// --- Game States ---

enum GameMode {
    MODE_READY,
    MODE_ATTACKING,
    MODE_RESULT
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

    // Simulation
    Simulation sim;
    sim.init();
    sim.load_base(create_test_base());

    // Game state
    bool paused = false;
    TroopType selected_troop = TroopType::BARBARIAN;
    GameMode game_mode = MODE_READY;

    // Army inventory
    ArmyInventory army;
    army.reset();

    // UI context
    pebble::ui::UIContext ui_ctx;

    // Camera pan speed (pixels per second at zoom 1.0)
    static constexpr float PAN_SPEED = 400.0f;

    LinearAllocator frame_alloc;
    frame_alloc.init(4 * 1024 * 1024);

    int64_t accumulator = 0;
    int64_t last_time = time_now_ns();

    log_info("=== MiniClash — Pebble Engine v0.1.0 ===");
    log_info("Controls:");
    log_info("  WASD / Right-click drag: Pan camera");
    log_info("  Scroll wheel: Zoom in/out");
    log_info("  SPACE or Left-click: Start attack (in READY mode)");
    log_info("  Left-click: Deploy selected troop (in ATTACK mode)");
    log_info("  1/2/3/4: Select troop (Barbarian/Archer/Giant/Wall Breaker)");
    log_info("  P: Pause/Resume");
    log_info("  ESC: Quit");

    while (!window_should_close(window)) {
        int64_t now = time_now_ns();
        int64_t dt = now - last_time;
        last_time = now;
        if (dt > 250'000'000) dt = 250'000'000;

        float dt_sec = (float)dt / 1'000'000'000.0f;

        window_poll_events(window);

        const auto& input = get_input_state();

        int32_t screen_w = renderer->width();
        int32_t screen_h = renderer->height();

        // --- INPUT HANDLING ---

        // ESC to quit
        if (input.keys_pressed[KEY_ESCAPE]) {
            break;
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

        // Camera pan via right-click drag (allowed in all modes)
        if (input.mouse_right_down && (input.mouse_dx != 0 || input.mouse_dy != 0)) {
            camera.pan(-input.mouse_dx, -input.mouse_dy);
        }

        // Camera zoom via scroll wheel (allowed in all modes)
        if (input.scroll_dy != 0) {
            float new_zoom = camera.zoom() + input.scroll_dy * 0.1f;
            new_zoom = std::max(Camera::MIN_ZOOM, std::min(Camera::MAX_ZOOM, new_zoom));
            camera.set_zoom(new_zoom);
        }

        // --- MODE-SPECIFIC INPUT ---

        if (game_mode == MODE_READY) {
            // SPACE or left-click to start attack
            if (input.keys_pressed[KEY_SPACE] || input.mouse_left_pressed) {
                game_mode = MODE_ATTACKING;
                sim.start_attack();
                log_info("Attack started! Deploy troops with left-click.");
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
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_2]) {
                selected_troop = TroopType::ARCHER;
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_3]) {
                selected_troop = TroopType::GIANT;
                log_info("Selected: %s", troop_type_name(selected_troop));
            }
            if (input.keys_pressed[KEY_4]) {
                selected_troop = TroopType::WALL_BREAKER;
                log_info("Selected: %s", troop_type_name(selected_troop));
            }

            // Left-click to deploy troop (only if attack is active and we have troops)
            if (input.mouse_left_pressed && sim.is_attack_active()) {
                // Check click is not on bottom HUD bar area (below screen_h - 80)
                if (input.mouse_y < screen_h - 80) {
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
                            sim.tick(tick_in);

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
                log_info("Attack finished!");
            }
        }
        else if (game_mode == MODE_RESULT) {
            // R to restart (reload base and reset)
            if (input.keys_pressed[KEY_R]) {
                sim.init();
                sim.load_base(create_test_base());
                army.reset();
                accumulator = 0;
                game_mode = MODE_READY;
                selected_troop = TroopType::BARBARIAN;
                log_info("Restarted! Scout the base and press SPACE to attack.");
            }
            // SPACE for new attack (same as restart for now)
            if (input.keys_pressed[KEY_SPACE]) {
                sim.init();
                sim.load_base(create_test_base());
                army.reset();
                accumulator = 0;
                game_mode = MODE_READY;
                selected_troop = TroopType::BARBARIAN;
                log_info("New attack! Scout the base and press SPACE to attack.");
            }
        }

        // --- SIMULATION ---
        if (game_mode == MODE_ATTACKING && !paused) {
            accumulator += dt;
            while (accumulator >= FIXED_DT_NS) {
                TickInput tick_in;
                tick_in.tick = sim.current_tick();
                tick_in.count = 0;
                sim.tick(tick_in);
                accumulator -= FIXED_DT_NS;
            }

            // Re-check if sim finished after ticking
            if (sim.result().finished) {
                game_mode = MODE_RESULT;
                paused = false;
                log_info("Attack finished!");
            }
        }

        // --- RENDER ---
        renderer->begin_frame(0.08f, 0.08f, 0.12f, 1.0f);
        camera.set_viewport(renderer->width(), renderer->height());
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

        // Buildings (alive only)
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

        batch.flush();

        // === UI RENDERING (after batch.flush, before end_frame) ===

        pebble::ui::ui_begin(ui_ctx, renderer, screen_w, screen_h);
        ui_ctx.mouse_x = input.mouse_x;
        ui_ctx.mouse_y = input.mouse_y;
        ui_ctx.mouse_down = input.mouse_left_down;
        ui_ctx.mouse_pressed = input.mouse_left_pressed;

        if (game_mode == MODE_READY) {
            // --- READY SCREEN ---
            // Semi-transparent dark overlay at bottom
            pebble::ui::draw_rect(ui_ctx, 0, (float)screen_h - 60.0f,
                                  (float)screen_w, 60.0f,
                                  0, 0, 0, 160);

            // "Click to start attack" indicator — centered banner
            float banner_w = 300.0f;
            float banner_h = 50.0f;
            float banner_x = ((float)screen_w - banner_w) / 2.0f;
            float banner_y = ((float)screen_h - banner_h) / 2.0f;

            // Dark background for banner
            pebble::ui::draw_rect(ui_ctx, banner_x, banner_y,
                                  banner_w, banner_h,
                                  20, 20, 40, 200);
            pebble::ui::draw_rect_outline(ui_ctx, banner_x, banner_y,
                                          banner_w, banner_h, 2.0f,
                                          100, 200, 255, 255);

            // "SPACE" indicator as colored block
            float space_w = 80.0f;
            float space_h = 24.0f;
            float space_x = ((float)screen_w - space_w) / 2.0f;
            float space_y = banner_y + 13.0f;
            pebble::ui::draw_rect(ui_ctx, space_x, space_y, space_w, space_h,
                                  100, 200, 255, 255);

            // Small decorative stars above banner
            pebble::ui::draw_stars(ui_ctx,
                                   ((float)screen_w - 90.0f) / 2.0f,
                                   banner_y - 40.0f,
                                   30.0f, 0, 3);
        }
        else if (game_mode == MODE_ATTACKING) {
            // --- TOP BAR ---
            float top_bar_h = 50.0f;
            pebble::ui::draw_rect(ui_ctx, 0, 0, (float)screen_w, top_bar_h,
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
            // Approximate width of timer: ~5 digits worth (MM:SS) = ~100px
            float timer_x = ((float)screen_w - 120.0f) / 2.0f;
            pebble::ui::draw_timer(ui_ctx, timer_x, 7.0f, timer_h,
                                   seconds_remaining,
                                   255, 255, 255);

            // Right: Destruction percentage
            int destruction = sim.result().destruction_percent;
            // Position from right side
            float dest_x = (float)screen_w - 120.0f;
            pebble::ui::draw_number(ui_ctx, dest_x, 10.0f, 28.0f,
                                    destruction,
                                    255, 200, 50);
            // "%" indicator as small colored block next to number
            float pct_x = dest_x + 80.0f;
            pebble::ui::draw_rect(ui_ctx, pct_x, 18.0f, 12.0f, 12.0f,
                                  255, 200, 50, 255);

            // --- BOTTOM BAR ---
            float bottom_bar_h = 70.0f;
            float bottom_bar_y = (float)screen_h - bottom_bar_h;
            pebble::ui::draw_rect(ui_ctx, 0, bottom_bar_y,
                                  (float)screen_w, bottom_bar_h,
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

                // Draw the button — returns true if clicked
                if (pebble::ui::draw_button(ui_ctx, bx, btn_y, btn_w, btn_h,
                                            tc.r, tc.g, tc.b, is_selected)) {
                    selected_troop = slot_type;
                }

                // Draw remaining count on button
                int remaining = army.count_for(slot_type);
                pebble::ui::draw_number(ui_ctx, bx + 18.0f, btn_y + 30.0f, 14.0f,
                                        remaining,
                                        255, 255, 255);

                // If count is zero, dim overlay
                if (remaining == 0) {
                    pebble::ui::draw_rect(ui_ctx, bx, btn_y, btn_w, btn_h,
                                          0, 0, 0, 150);
                }
            }

            // "END ATTACK" button on far right (red)
            float end_btn_w = 100.0f;
            float end_btn_h = 50.0f;
            float end_btn_x = (float)screen_w - end_btn_w - 20.0f;
            float end_btn_y = bottom_bar_y + 10.0f;

            if (pebble::ui::draw_button(ui_ctx, end_btn_x, end_btn_y,
                                        end_btn_w, end_btn_h,
                                        200, 40, 40, false)) {
                // End attack early — transition to result
                // We need to force the sim to finish. Since we can't modify sim,
                // we just switch to result mode directly.
                game_mode = MODE_RESULT;
                log_info("Attack ended early by player.");
            }

            // --- HEALTH BARS ON BUILDINGS ---
            sim.for_each_building([&](const Building& b) {
                float hp_ratio = fp_to_float(b.hp) / fp_to_float(b.max_hp);
                // Only show health bar if building is damaged
                if (hp_ratio < 1.0f) {
                    // Get building center in world coords
                    Vec2fp center = b.center();
                    float center_wx = fp_to_float(center.x);
                    float center_wy = fp_to_float(center.y);

                    // Convert to screen position
                    float sx, sy;
                    camera.world_to_screen(center_wx, center_wy, sx, sy);

                    // Draw health bar above the building
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

            // Dark overlay covering entire screen
            pebble::ui::draw_rect(ui_ctx, 0, 0,
                                  (float)screen_w, (float)screen_h,
                                  0, 0, 0, 180);

            const auto& res = sim.result();

            // Big stars in center
            float star_size = 60.0f;
            float stars_total_w = star_size * 3.0f + 20.0f; // 3 stars with gaps
            float stars_x = ((float)screen_w - stars_total_w) / 2.0f;
            float stars_y = (float)screen_h / 2.0f - 80.0f;
            pebble::ui::draw_stars(ui_ctx, stars_x, stars_y, star_size,
                                   res.stars, 3);

            // Destruction percentage as large number
            float num_x = ((float)screen_w - 80.0f) / 2.0f;
            float num_y = stars_y + star_size + 30.0f;
            pebble::ui::draw_number(ui_ctx, num_x, num_y, 40.0f,
                                    res.destruction_percent,
                                    255, 200, 50);
            // "%" block indicator
            pebble::ui::draw_rect(ui_ctx, num_x + 100.0f, num_y + 10.0f,
                                  16.0f, 16.0f,
                                  255, 200, 50, 255);

            // "DESTROYED" label bar
            float label_w = 200.0f;
            float label_h = 24.0f;
            float label_x = ((float)screen_w - label_w) / 2.0f;
            float label_y = num_y + 50.0f;
            pebble::ui::draw_rect(ui_ctx, label_x, label_y, label_w, label_h,
                                  80, 80, 80, 200);

            // "Press R to restart" hint — small colored rect as visual cue
            float hint_w = 180.0f;
            float hint_h = 30.0f;
            float hint_x = ((float)screen_w - hint_w) / 2.0f;
            float hint_y = label_y + 50.0f;
            pebble::ui::draw_rect(ui_ctx, hint_x, hint_y, hint_w, hint_h,
                                  60, 60, 100, 220);
            pebble::ui::draw_rect_outline(ui_ctx, hint_x, hint_y, hint_w, hint_h,
                                          2.0f, 120, 120, 200, 255);

            // "R" key indicator block inside the hint
            pebble::ui::draw_rect(ui_ctx, hint_x + 20.0f, hint_y + 5.0f,
                                  20.0f, 20.0f,
                                  180, 180, 255, 255);

            // "SPACE" key indicator block for new attack
            float hint2_y = hint_y + 40.0f;
            pebble::ui::draw_rect(ui_ctx, hint_x, hint2_y, hint_w, hint_h,
                                  60, 100, 60, 220);
            pebble::ui::draw_rect_outline(ui_ctx, hint_x, hint2_y, hint_w, hint_h,
                                          2.0f, 120, 200, 120, 255);
            pebble::ui::draw_rect(ui_ctx, hint_x + 20.0f, hint2_y + 5.0f,
                                  60.0f, 20.0f,
                                  180, 255, 180, 255);
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

    renderer->shutdown();
    delete renderer;
    window_destroy(window);
    return 0;
}
