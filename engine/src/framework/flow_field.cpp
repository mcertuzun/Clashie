#include "pebble/framework/flow_field.h"

namespace pebble {

// Pre-computed normalized direction vectors (fixed-point) for the 8 compass directions
// Diagonal directions use 1/sqrt(2) ~ 0.7071 ~ 46341 in 16.16 fixed-point
static constexpr fp32 DIAG_COMPONENT = 46341; // float_to_fp(0.70710678f)

static const Vec2fp DIR_VECTORS[9] = {
    {  PFP_ZERO,    -PFP_ONE   },  // 0: N   ( 0, -1)
    {  DIAG_COMPONENT, -DIAG_COMPONENT },  // 1: NE  (+, -)
    {  PFP_ONE,     PFP_ZERO   },  // 2: E   (+1,  0)
    {  DIAG_COMPONENT,  DIAG_COMPONENT },  // 3: SE  (+, +)
    {  PFP_ZERO,     PFP_ONE   },  // 4: S   ( 0, +1)
    { -DIAG_COMPONENT,  DIAG_COMPONENT },  // 5: SW  (-, +)
    { -PFP_ONE,     PFP_ZERO   },  // 6: W   (-1,  0)
    { -DIAG_COMPONENT, -DIAG_COMPONENT },  // 7: NW  (-, -)
    {  PFP_ZERO,     PFP_ZERO  },  // 8: NONE
};

// BFS queue — static storage to avoid allocations
// Max queue size = FLOW_GRID_SIZE * FLOW_GRID_SIZE = 1600
struct BFSNode {
    uint8_t x, y;
};

static BFSNode s_bfs_queue[FLOW_GRID_SIZE * FLOW_GRID_SIZE];

// ---------- FlowFieldCache ----------

FlowFieldCache::FlowFieldCache() {
    invalidate_all();
}

void FlowFieldCache::invalidate_all() {
    for (int i = 0; i < FLOW_CACHE_MAX; ++i) {
        m_valid[i] = false;
        m_last_used_tick[i] = 0;
    }
}

const FlowField* FlowFieldCache::get_or_compute(uint8_t target_x, uint8_t target_y,
                                                  uint32_t wall_version, uint32_t current_tick,
                                                  PassabilityFunc passable_fn, void* user_data) {
    FlowFieldKey key = { target_x, target_y, wall_version };

    // Search cache for existing entry (keys are compact for cache-friendly scan)
    for (int i = 0; i < FLOW_CACHE_MAX; ++i) {
        if (m_valid[i] && m_keys[i] == key) {
            m_last_used_tick[i] = current_tick;
            return &m_fields[i];
        }
    }

    // Not found — find a slot (prefer invalid, then LRU)
    int slot = -1;
    uint32_t oldest_tick = 0xFFFFFFFF;

    for (int i = 0; i < FLOW_CACHE_MAX; ++i) {
        if (!m_valid[i]) {
            slot = i;
            break;
        }
        if (m_last_used_tick[i] < oldest_tick) {
            oldest_tick = m_last_used_tick[i];
            slot = i;
        }
    }

    // Compute the flow field
    m_keys[slot] = key;
    m_last_used_tick[slot] = current_tick;
    m_valid[slot] = true;
    m_fields[slot].clear();

    compute(m_fields[slot], target_x, target_y, passable_fn, user_data);

    return &m_fields[slot];
}

void FlowFieldCache::compute(FlowField& field, uint8_t target_x, uint8_t target_y,
                              PassabilityFunc passable_fn, void* user_data) {
    // Phase 1: BFS integration field from target outward
    // The target cell has cost 0. Each neighbor adds 1 (cardinal) or 14 (diagonal, ~1.414 * 10).
    // We use cost units of 10 for cardinal, 14 for diagonal, so distances are more accurate.

    int head = 0;
    int tail = 0;

    // Seed: target cell
    if (target_x < FLOW_GRID_SIZE && target_y < FLOW_GRID_SIZE) {
        field.integration[target_y][target_x] = 0;
        s_bfs_queue[tail++] = { target_x, target_y };
    }

    // Determine wall-adjacent cells for cost penalty.
    // We'll mark them during BFS neighbor expansion.
    // First, build a quick wall-adjacency map.
    bool wall_adjacent[FLOW_GRID_SIZE][FLOW_GRID_SIZE];
    std::memset(wall_adjacent, 0, sizeof(wall_adjacent));

    for (int y = 0; y < FLOW_GRID_SIZE; ++y) {
        for (int x = 0; x < FLOW_GRID_SIZE; ++x) {
            // If this cell is impassable, mark its passable neighbors as wall-adjacent
            if (!passable_fn(x, y, user_data)) {
                for (int d = 0; d < 8; ++d) {
                    int nx = x + FLOW_DX[d];
                    int ny = y + FLOW_DY[d];
                    if (nx >= 0 && nx < FLOW_GRID_SIZE && ny >= 0 && ny < FLOW_GRID_SIZE) {
                        if (passable_fn(nx, ny, user_data)) {
                            wall_adjacent[ny][nx] = true;
                        }
                    }
                }
            }
        }
    }

    // BFS (Dijkstra-like with uniform edge costs per direction)
    while (head < tail) {
        BFSNode cur = s_bfs_queue[head++];
        uint16_t cur_cost = field.integration[cur.y][cur.x];

        for (int d = 0; d < 8; ++d) {
            int nx = cur.x + FLOW_DX[d];
            int ny = cur.y + FLOW_DY[d];

            if (nx < 0 || nx >= FLOW_GRID_SIZE || ny < 0 || ny >= FLOW_GRID_SIZE)
                continue;

            if (!passable_fn(nx, ny, user_data)) {
                // Mark as impassable
                field.integration[ny][nx] = FLOW_IMPASSABLE;
                continue;
            }

            // Cardinal move costs 10, diagonal costs 14
            bool diagonal = (FLOW_DX[d] != 0 && FLOW_DY[d] != 0);
            uint16_t move_cost = diagonal ? 14 : 10;

            // For diagonal moves, check that both adjacent cardinal cells are passable
            // (prevents cutting through wall corners)
            if (diagonal) {
                if (!passable_fn(cur.x + FLOW_DX[d], cur.y, user_data) ||
                    !passable_fn(cur.x, cur.y + FLOW_DY[d], user_data)) {
                    continue;
                }
            }

            // Wall-adjacent penalty
            uint16_t penalty = wall_adjacent[ny][nx] ? FLOW_WALL_ADJACENT_COST : 0;

            uint16_t new_cost = cur_cost + move_cost + penalty;
            if (new_cost < field.integration[ny][nx]) {
                field.integration[ny][nx] = new_cost;
                s_bfs_queue[tail++] = { static_cast<uint8_t>(nx), static_cast<uint8_t>(ny) };
            }
        }
    }

    // Phase 2: Build direction field via gradient descent
    // Each cell points toward the neighbor with the lowest integration cost
    for (int y = 0; y < FLOW_GRID_SIZE; ++y) {
        for (int x = 0; x < FLOW_GRID_SIZE; ++x) {
            if (field.integration[y][x] == FLOW_IMPASSABLE) {
                field.direction[y][x] = FLOW_DIR_NONE;
                continue;
            }

            // Target cell: no direction needed
            if (field.integration[y][x] == 0) {
                field.direction[y][x] = FLOW_DIR_NONE;
                continue;
            }

            uint16_t best_cost = field.integration[y][x];
            uint8_t  best_dir = FLOW_DIR_NONE;

            for (int d = 0; d < 8; ++d) {
                int nx = x + FLOW_DX[d];
                int ny = y + FLOW_DY[d];

                if (nx < 0 || nx >= FLOW_GRID_SIZE || ny < 0 || ny >= FLOW_GRID_SIZE)
                    continue;

                // For diagonal, ensure we can actually move diagonally (no corner cutting)
                bool diagonal = (FLOW_DX[d] != 0 && FLOW_DY[d] != 0);
                if (diagonal) {
                    if (field.integration[y][x + FLOW_DX[d]] == FLOW_IMPASSABLE ||
                        field.integration[y + FLOW_DY[d]][x] == FLOW_IMPASSABLE) {
                        continue;
                    }
                }

                uint16_t neighbor_cost = field.integration[ny][nx];
                if (neighbor_cost < best_cost) {
                    best_cost = neighbor_cost;
                    best_dir = static_cast<uint8_t>(d);
                }
            }

            field.direction[y][x] = best_dir;
        }
    }
}

Vec2fp FlowFieldCache::get_direction(const FlowField& field, int cell_x, int cell_y) {
    if (cell_x < 0 || cell_x >= FLOW_GRID_SIZE || cell_y < 0 || cell_y >= FLOW_GRID_SIZE) {
        return DIR_VECTORS[FLOW_DIR_NONE];
    }

    uint8_t dir = field.direction[cell_y][cell_x];
    return DIR_VECTORS[dir];
}

} // namespace pebble
