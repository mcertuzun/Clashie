#pragma once

#include "miniclash/grid.h"
#include "miniclash/building.h"
#include "miniclash/troop.h"
#include "pebble/core/memory.h"
#include "pebble/core/fixed_point.h"
#include "pebble/framework/flow_field.h"
#include <cstdint>

namespace miniclash {

// Fixed timestep: 20 ticks/second
static constexpr pebble::fp32 SIM_FIXED_DT = pebble::PFP_ONE / 20;
static constexpr int MAX_BUILDINGS = 64;
static constexpr int MAX_TROOPS = 512;

// Input events recorded for replay
struct InputEvent {
    enum Type : uint8_t {
        DEPLOY_TROOP = 1,
    };

    Type         type;
    uint8_t      troop_type;
    pebble::fp32 world_x;
    pebble::fp32 world_y;
};

struct TickInput {
    uint32_t   tick;
    uint8_t    count = 0;
    InputEvent events[8]; // Max 8 inputs per tick
};

// Attack result
struct AttackResult {
    uint8_t  stars = 0;
    uint8_t  destruction_percent = 0;
    uint32_t duration_ticks = 0;
    bool     finished = false;
};

// Events emitted by simulation (for rendering / VFX)
struct SimEvent {
    enum Type : uint8_t {
        BUILDING_DESTROYED = 1,
        TROOP_KILLED       = 2,
        TROOP_ATTACK       = 3,
        DEFENSE_FIRE       = 4,
        STAR_EARNED        = 5,
    };
    Type     type;
    uint16_t source_id;
    uint16_t target_id;
    pebble::fp32 damage;
    pebble::Vec2fp position;
};

static constexpr int MAX_EVENTS_PER_TICK = 64;

class Simulation {
public:
    void init();
    void load_base(const BaseLayout& layout);
    void tick(const TickInput& input);
    void process_input(const InputEvent& event);

    uint32_t compute_state_hash() const;

    // Accessors
    uint32_t current_tick() const { return m_tick; }
    const TileGrid& grid() const { return m_grid; }
    const AttackResult& result() const { return m_result; }
    bool is_attack_active() const { return m_attack_active; }

    // Building iteration
    template<typename Func> void for_each_building(Func&& f) {
        m_buildings.for_each([&](Building& b, uint16_t idx) { (void)idx; if (b.alive) f(b); });
    }
    template<typename Func> void for_each_building(Func&& f) const {
        m_buildings.for_each([&](const Building& b, uint16_t idx) { (void)idx; if (b.alive) f(b); });
    }

    // Troop iteration
    template<typename Func> void for_each_troop(Func&& f) {
        m_troops.for_each([&](Troop& t, uint16_t idx) { (void)idx; if (t.alive) f(t); });
    }
    template<typename Func> void for_each_troop(Func&& f) const {
        m_troops.for_each([&](const Troop& t, uint16_t idx) { (void)idx; if (t.alive) f(t); });
    }

    // Events this tick (consumed by rendering)
    const SimEvent* events() const { return m_events; }
    int event_count() const { return m_event_count; }

    void start_attack();

private:
    void update_troops();
    void update_defenses();
    void find_target(Troop& troop);
    void move_troop(Troop& troop);
    void troop_attack(Troop& troop);
    void defense_attack(Building& defense);
    void destroy_building(Building& building);
    void kill_troop(Troop& troop);
    void check_victory();
    void emit_event(const SimEvent& event);

    uint32_t m_tick = 0;
    uint32_t m_seed = 12345; // Deterministic RNG

    TileGrid m_grid;
    pebble::PoolAllocator<Building, MAX_BUILDINGS> m_buildings;
    pebble::PoolAllocator<Troop, MAX_TROOPS>       m_troops;

    uint16_t m_next_building_id = 1;
    uint16_t m_next_troop_id = 1;
    uint32_t m_wall_version = 0; // Invalidates flow fields when walls break
    pebble::FlowFieldCache m_flow_cache;

    int      m_total_buildings = 0;
    int      m_destroyed_buildings = 0;
    bool     m_town_hall_destroyed = false;
    bool     m_attack_active = false;

    AttackResult m_result;

    SimEvent m_events[MAX_EVENTS_PER_TICK];
    int      m_event_count = 0;

public:
    // Attack timer: 3 minutes = 3600 ticks
    static constexpr uint32_t MAX_ATTACK_TICKS = 3600;
private:
};

} // namespace miniclash
