#pragma once

#include "pebble/core/fixed_point.h"
#include "pebble/core/types.h"
#include <cstdint>

namespace miniclash {

enum class TroopType : uint8_t {
    BARBARIAN    = 0,
    ARCHER       = 1,
    GIANT        = 2,
    WALL_BREAKER = 3,
    COUNT        = 4
};

enum class TroopState : uint8_t {
    SPAWNING    = 0,
    FIND_TARGET = 1,
    MOVING      = 2,
    ATTACKING   = 3,
    DEAD        = 4
};

struct TroopData {
    const char*  name;
    pebble::fp32 max_hp;
    pebble::fp32 dps;
    pebble::fp32 move_speed;
    pebble::fp32 attack_range;
    pebble::fp32 attack_cooldown;   // Seconds (fixed-point)
    uint8_t      housing_space;
    pebble::fp32 deploy_time;       // Seconds (fixed-point)
    pebble::fp32 splash_radius;     // 0 = single target
    pebble::fp32 wall_damage_mult;  // 0 = normal

    enum PreferredTarget : uint8_t {
        TARGET_ANY      = 0,
        TARGET_DEFENSES = 1,
        TARGET_WALLS    = 2
    };
    PreferredTarget preferred_target;
};

const TroopData& get_troop_data(TroopType type);

struct Troop {
    uint16_t          id = 0;
    TroopType         type = TroopType::BARBARIAN;
    TroopState        state = TroopState::SPAWNING;
    pebble::Vec2fp    pos = {};
    pebble::Vec2fp    facing = {};
    pebble::fp32      hp = 0;
    pebble::fp32      attack_timer = 0;
    pebble::fp32      spawn_timer = 0;
    uint16_t          target_building = 0; // Building ID (0 = none)
    pebble::EntityID  entity = pebble::INVALID_ENTITY;
    bool              alive = false;
};

// Army composition before attack
struct ArmySlot {
    TroopType type;
    uint8_t   count;
};

struct Army {
    static constexpr uint8_t MAX_HOUSING = 30;
    static constexpr uint8_t MAX_SLOTS = 4;

    ArmySlot slots[MAX_SLOTS] = {};
    uint8_t  slot_count = 0;

    uint8_t total_housing() const {
        uint8_t total = 0;
        for (uint8_t i = 0; i < slot_count; ++i) {
            total += slots[i].count * get_troop_data(slots[i].type).housing_space;
        }
        return total;
    }
};

} // namespace miniclash
