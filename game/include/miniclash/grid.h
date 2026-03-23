#pragma once

#include "pebble/core/fixed_point.h"
#include <cstdint>
#include <cstring>

namespace miniclash {

static constexpr int GRID_SIZE = 40;

enum class CellState : uint8_t {
    EMPTY    = 0,
    OCCUPIED = 1,
    WALL     = 2,
    BOUNDARY = 3
};

struct TileGrid {
    CellState cells[GRID_SIZE][GRID_SIZE];
    uint16_t  building_id[GRID_SIZE][GRID_SIZE]; // 0 = none

    void clear() {
        std::memset(cells, 0, sizeof(cells));
        std::memset(building_id, 0, sizeof(building_id));
    }

    bool in_bounds(int x, int y) const {
        return x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE;
    }

    bool can_place(int x, int y, int w, int h) const {
        for (int dy = 0; dy < h; ++dy) {
            for (int dx = 0; dx < w; ++dx) {
                int cx = x + dx;
                int cy = y + dy;
                if (!in_bounds(cx, cy)) return false;
                if (cells[cy][cx] != CellState::EMPTY) return false;
            }
        }
        return true;
    }

    void place(int x, int y, int w, int h, uint16_t bid, CellState state = CellState::OCCUPIED) {
        for (int dy = 0; dy < h; ++dy) {
            for (int dx = 0; dx < w; ++dx) {
                cells[y + dy][x + dx] = state;
                building_id[y + dy][x + dx] = bid;
            }
        }
    }

    void remove(int x, int y, int w, int h) {
        for (int dy = 0; dy < h; ++dy) {
            for (int dx = 0; dx < w; ++dx) {
                cells[y + dy][x + dx] = CellState::EMPTY;
                building_id[y + dy][x + dx] = 0;
            }
        }
    }
};

} // namespace miniclash
