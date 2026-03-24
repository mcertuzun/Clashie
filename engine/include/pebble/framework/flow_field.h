#pragma once

#include "pebble/core/fixed_point.h"
#include <cstdint>
#include <cstring>

namespace pebble {

static constexpr int FLOW_GRID_SIZE = 40;
static constexpr uint16_t FLOW_IMPASSABLE = 0xFFFF;
static constexpr uint8_t FLOW_DIR_NONE = 8;
static constexpr int FLOW_CACHE_MAX = 16;

// Wall-adjacent cost penalty (added on top of normal BFS cost)
static constexpr uint16_t FLOW_WALL_ADJACENT_COST = 5;

struct FlowField {
    uint16_t integration[FLOW_GRID_SIZE][FLOW_GRID_SIZE];
    uint8_t  direction[FLOW_GRID_SIZE][FLOW_GRID_SIZE];   // 0-7 = 8 compass dirs, 8 = NONE

    void clear() {
        std::memset(integration, 0xFF, sizeof(integration)); // All IMPASSABLE
        std::memset(direction, FLOW_DIR_NONE, sizeof(direction));
    }
};

// 8 directions: N, NE, E, SE, S, SW, W, NW
// dx/dy offsets for each direction index
static constexpr int FLOW_DX[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static constexpr int FLOW_DY[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

// Cache key for a flow field
struct FlowFieldKey {
    uint8_t  target_x;
    uint8_t  target_y;
    uint32_t wall_version;

    bool operator==(const FlowFieldKey& o) const {
        return target_x == o.target_x && target_y == o.target_y && wall_version == o.wall_version;
    }
};

// LRU cache entry
struct FlowFieldCacheEntry {
    FlowFieldKey key;
    FlowField    field;
    uint32_t     last_used_tick;  // For LRU eviction
    bool         valid;
};

// Passability callback: returns true if the cell at (x,y) is walkable
using PassabilityFunc = bool (*)(int x, int y, void* user_data);

class FlowFieldCache {
public:
    FlowFieldCache();

    // Compute or retrieve a cached flow field for the given target tile.
    // passable_fn returns true if the cell is walkable.
    // current_tick is used for LRU tracking.
    const FlowField* get_or_compute(uint8_t target_x, uint8_t target_y,
                                    uint32_t wall_version, uint32_t current_tick,
                                    PassabilityFunc passable_fn, void* user_data);

    // Get the fixed-point direction vector for a cell from a flow field
    static Vec2fp get_direction(const FlowField& field, int cell_x, int cell_y);

    // Invalidate all cached entries (e.g., when wall layout changes dramatically)
    void invalidate_all();

private:
    void compute(FlowField& field, uint8_t target_x, uint8_t target_y,
                 PassabilityFunc passable_fn, void* user_data);

    // Keys separated from fields for cache-friendly linear search
    FlowFieldKey m_keys[FLOW_CACHE_MAX];
    FlowField    m_fields[FLOW_CACHE_MAX];
    uint32_t     m_last_used_tick[FLOW_CACHE_MAX];
    bool         m_valid[FLOW_CACHE_MAX];
};

} // namespace pebble
