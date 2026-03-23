#pragma once

#include "pebble/core/fixed_point.h"
#include "pebble/core/types.h"
#include <cstdint>

namespace miniclash {

enum class BuildingType : uint8_t {
    TOWN_HALL     = 0,
    CANNON        = 1,
    ARCHER_TOWER  = 2,
    MORTAR        = 3,
    GOLD_STORAGE  = 4,
    ELIXIR_STORAGE = 5,
    WALL          = 6,
    BUILDER_HUT   = 7,
    COUNT         = 8
};

struct BuildingData {
    const char* name;
    uint8_t     size;           // Width/height in tiles
    pebble::fp32 max_hp;
    bool        is_defense;
    bool        is_wall;
    pebble::fp32 attack_range;     // 0 for non-defense
    pebble::fp32 min_attack_range; // Mortar dead zone
    pebble::fp32 attack_damage;
    pebble::fp32 attack_cooldown;  // Fixed-point seconds
    pebble::fp32 splash_radius;    // 0 for single target
};

// Balance data table — loaded from JSON at startup, but hardcoded defaults here
const BuildingData& get_building_data(BuildingType type);

struct Building {
    uint16_t          id = 0;
    BuildingType      type = BuildingType::TOWN_HALL;
    uint8_t           grid_x = 0;
    uint8_t           grid_y = 0;
    pebble::fp32      hp = 0;
    pebble::fp32      max_hp = 0;
    pebble::fp32      attack_timer = 0;
    pebble::EntityID  entity = pebble::INVALID_ENTITY;
    bool              alive = false;

    uint8_t size() const { return get_building_data(type).size; }
    bool is_defense() const { return get_building_data(type).is_defense; }
    bool is_wall() const { return get_building_data(type).is_wall; }

    // Center position in world coordinates (fixed-point)
    pebble::Vec2fp center() const {
        auto s = pebble::int_to_fp(size());
        return {
            pebble::int_to_fp(grid_x) + (s >> 1),
            pebble::int_to_fp(grid_y) + (s >> 1)
        };
    }
};

// Serialisation for base layout save/load
struct BuildingPlacement {
    uint8_t type;
    uint8_t level;
    uint8_t grid_x;
    uint8_t grid_y;
};

struct BaseLayout {
    static constexpr uint32_t MAGIC = 0x45534250; // 'PBSE'
    uint32_t magic = MAGIC;
    uint16_t version = 1;
    uint16_t building_count = 0;
    BuildingPlacement buildings[64]; // Max 64 buildings per base
};

} // namespace miniclash
