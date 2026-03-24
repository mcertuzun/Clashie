#include "miniclash/simulation.h"
#include "pebble/platform/platform.h"

using namespace pebble;

namespace miniclash {

void Simulation::init() {
    m_tick = 0;
    m_seed = 12345;
    m_grid.clear();
    m_buildings.clear();
    m_troops.clear();
    m_next_building_id = 1;
    m_next_troop_id = 1;
    std::memset(m_building_by_id, 0, sizeof(m_building_by_id));
    m_wall_version = 0;
    m_flow_cache.invalidate_all();
    m_total_buildings = 0;
    m_destroyed_buildings = 0;
    m_town_hall_destroyed = false;
    m_attack_active = false;
    m_result = {};
    m_event_count = 0;
}

void Simulation::load_base(const BaseLayout& layout) {
    for (uint16_t i = 0; i < layout.building_count; ++i) {
        const auto& bp = layout.buildings[i];
        auto type = static_cast<BuildingType>(bp.type);
        const auto& data = get_building_data(type);

        if (!m_grid.can_place(bp.grid_x, bp.grid_y, data.size, data.size))
            continue;

        Building* b = m_buildings.alloc();
        if (!b) break;

        b->id = m_next_building_id++;
        b->type = type;
        b->grid_x = bp.grid_x;
        b->grid_y = bp.grid_y;
        b->hp = data.max_hp;
        b->max_hp = data.max_hp;
        b->attack_timer = PFP_ZERO;
        b->alive = true;
        m_building_by_id[b->id] = b;

        CellState cell = data.is_wall ? CellState::WALL : CellState::OCCUPIED;
        m_grid.place(bp.grid_x, bp.grid_y, data.size, data.size, b->id, cell);
        m_total_buildings++;
    }

    platform::log_info("Base loaded: %d buildings", m_total_buildings);
}

void Simulation::start_attack() {
    m_attack_active = true;
    m_tick = 0;
    m_result = {};
    platform::log_info("Attack started! Timer: 3:00");
}

void Simulation::tick(const TickInput& input) {
    m_event_count = 0;

    if (!m_attack_active) return;

    // Process inputs
    for (uint8_t i = 0; i < input.count; ++i) {
        process_input(input.events[i]);
    }

    // Update troops
    update_troops();

    // Update defenses
    update_defenses();

    // Check victory / timeout
    check_victory();

    m_tick++;
}

void Simulation::process_input(const InputEvent& event) {
    if (event.type == InputEvent::DEPLOY_TROOP) {
        auto troop_type = static_cast<TroopType>(event.troop_type);
        const auto& data = get_troop_data(troop_type);

        Troop* t = m_troops.alloc();
        if (!t) return;

        t->id = m_next_troop_id++;
        t->type = troop_type;
        t->state = TroopState::SPAWNING;
        t->pos = { event.world_x, event.world_y };
        t->facing = { PFP_ZERO, PFP_ZERO };
        t->hp = data.max_hp;
        t->attack_timer = PFP_ZERO;
        t->spawn_timer = data.deploy_time;
        t->target_building = 0;
        t->alive = true;
    }
}

void Simulation::update_troops() {
    m_troops.for_each([this](Troop& troop, uint16_t) {
        if (!troop.alive) return;

        switch (troop.state) {
        case TroopState::SPAWNING:
            troop.spawn_timer -= SIM_FIXED_DT;
            if (troop.spawn_timer <= PFP_ZERO) {
                troop.state = TroopState::FIND_TARGET;
            }
            break;

        case TroopState::FIND_TARGET:
            find_target(troop);
            if (troop.target_building != 0) {
                troop.state = TroopState::MOVING;
            }
            break;

        case TroopState::MOVING:
            move_troop(troop);
            break;

        case TroopState::ATTACKING:
            troop_attack(troop);
            break;

        case TroopState::DEAD:
            break;
        }
    });
}

void Simulation::find_target(Troop& troop) {
    const auto& data = get_troop_data(troop.type);
    uint16_t best_id = 0;
    fp32 best_dist = PFP_MAX;

    m_buildings.for_each([&](const Building& b, uint16_t) {
        if (!b.alive) return;

        // Preferred target filter
        if (data.preferred_target == TroopData::TARGET_DEFENSES && !b.is_defense())
            return;
        if (data.preferred_target == TroopData::TARGET_WALLS && !b.is_wall())
            return;

        fp32 dist = fp_manhattan(troop.pos, b.center());

        if (dist < best_dist || (dist == best_dist && b.id < best_id)) {
            best_dist = dist;
            best_id = b.id;
        }
    });

    // If preferred target not found (e.g., no walls left), fall back to any
    if (best_id == 0 && data.preferred_target != TroopData::TARGET_ANY) {
        m_buildings.for_each([&](const Building& b, uint16_t) {
            if (!b.alive) return;
            fp32 dist = fp_manhattan(troop.pos, b.center());
            if (dist < best_dist || (dist == best_dist && b.id < best_id)) {
                best_dist = dist;
                best_id = b.id;
            }
        });
    }

    troop.target_building = best_id;
}

// Passability callback for flow field computation
static bool sim_passable(int x, int y, void* user_data) {
    const TileGrid* grid = static_cast<const TileGrid*>(user_data);
    if (!grid->in_bounds(x, y)) return false;
    CellState state = grid->cells[y][x];
    return state == CellState::EMPTY;
}

void Simulation::move_troop(Troop& troop) {
    if (troop.target_building == 0) {
        troop.state = TroopState::FIND_TARGET;
        return;
    }

    // Find target building via O(1) lookup
    Building* target = (troop.target_building > 0 && troop.target_building < m_next_building_id)
        ? m_building_by_id[troop.target_building] : nullptr;
    if (!target || !target->alive) {
        troop.target_building = 0;
        troop.state = TroopState::FIND_TARGET;
        return;
    }

    const auto& data = get_troop_data(troop.type);
    Vec2fp target_center = target->center();
    fp32 dist = fp_distance(troop.pos, target_center);

    // In range? Attack!
    if (dist <= data.attack_range + int_to_fp(target->size() / 2)) {
        troop.state = TroopState::ATTACKING;
        troop.attack_timer = PFP_ZERO;
        return;
    }

    // Determine target tile for the flow field (center of target building)
    uint8_t flow_tx = static_cast<uint8_t>(fp_to_int(target_center.x));
    uint8_t flow_ty = static_cast<uint8_t>(fp_to_int(target_center.y));

    // Clamp to grid bounds
    if (flow_tx >= GRID_SIZE) flow_tx = GRID_SIZE - 1;
    if (flow_ty >= GRID_SIZE) flow_ty = GRID_SIZE - 1;

    // If target tile is occupied (building sits on it), the BFS seeds from it anyway
    // and troops that target walls should walk straight to them
    const pebble::FlowField* field = m_flow_cache.get_or_compute(
        flow_tx, flow_ty, m_wall_version, m_tick,
        sim_passable, const_cast<TileGrid*>(&m_grid));

    // Get the troop's current grid cell
    int cell_x = fp_to_int(troop.pos.x);
    int cell_y = fp_to_int(troop.pos.y);

    // Clamp to grid
    if (cell_x < 0) cell_x = 0;
    if (cell_x >= GRID_SIZE) cell_x = GRID_SIZE - 1;
    if (cell_y < 0) cell_y = 0;
    if (cell_y >= GRID_SIZE) cell_y = GRID_SIZE - 1;

    Vec2fp dir;

    if (field) {
        dir = pebble::FlowFieldCache::get_direction(*field, cell_x, cell_y);
    }

    // If no valid flow direction (at target, or unreachable), fall back to direct movement
    if (!field || (dir.x == PFP_ZERO && dir.y == PFP_ZERO)) {
        dir = fp_normalize(target_center - troop.pos);
    }

    troop.pos.x += fp_mul(dir.x, fp_mul(data.move_speed, SIM_FIXED_DT));
    troop.pos.y += fp_mul(dir.y, fp_mul(data.move_speed, SIM_FIXED_DT));
    troop.facing = dir;

    // Simple separation from other troops (squared distance to skip sqrt for most pairs)
    static const fp32 TROOP_MIN_DIST = 26214;     // float_to_fp(0.4f)
    static const fp32 TROOP_MIN_DIST_SQ = fp_mul(TROOP_MIN_DIST, TROOP_MIN_DIST);
    static const fp32 TROOP_SEP_FORCE = 6553;     // float_to_fp(0.1f)
    m_troops.for_each([&](Troop& other, uint16_t) {
        if (!other.alive || other.id == troop.id) return;
        Vec2fp diff = troop.pos - other.pos;
        fp32 d_sq = fp_length_sq(diff);
        if (d_sq > PFP_ZERO && d_sq < TROOP_MIN_DIST_SQ) {
            Vec2fp push = fp_normalize(diff);
            troop.pos += fp_scale(push, TROOP_SEP_FORCE);
        }
    });
}

void Simulation::troop_attack(Troop& troop) {
    if (troop.target_building == 0) {
        troop.state = TroopState::FIND_TARGET;
        return;
    }

    // Find target building via O(1) lookup
    Building* target = (troop.target_building > 0 && troop.target_building < m_next_building_id)
        ? m_building_by_id[troop.target_building] : nullptr;
    if (!target || !target->alive) {
        troop.target_building = 0;
        troop.state = TroopState::FIND_TARGET;
        return;
    }

    // Cooldown
    if (troop.attack_timer > PFP_ZERO) {
        troop.attack_timer -= SIM_FIXED_DT;
        return;
    }

    const auto& data = get_troop_data(troop.type);

    // Range check
    fp32 dist = fp_distance(troop.pos, target->center());
    if (dist > data.attack_range + int_to_fp(target->size() / 2)) {
        troop.state = TroopState::MOVING;
        return;
    }

    // Deal damage
    fp32 damage = data.dps;
    if (data.wall_damage_mult > PFP_ZERO && target->is_wall()) {
        damage = fp_mul(damage, data.wall_damage_mult);
    }

    target->hp -= damage;
    troop.attack_timer = data.attack_cooldown;

    emit_event({ SimEvent::TROOP_ATTACK, troop.id, target->id, damage, troop.pos });

    if (target->hp <= PFP_ZERO) {
        target->hp = PFP_ZERO;
        destroy_building(*target);
        troop.target_building = 0;
        troop.state = TroopState::FIND_TARGET;
    }
}

void Simulation::update_defenses() {
    m_buildings.for_each([this](Building& defense, uint16_t) {
        if (!defense.alive || !defense.is_defense()) return;
        defense_attack(defense);
    });
}

void Simulation::defense_attack(Building& defense) {
    const auto& data = get_building_data(defense.type);

    // Cooldown
    if (defense.attack_timer > PFP_ZERO) {
        defense.attack_timer -= SIM_FIXED_DT;
        return;
    }

    Vec2fp def_center = defense.center();

    // Find nearest troop in range
    uint16_t best_id = 0;
    fp32 best_dist = PFP_MAX;
    Troop* best_troop = nullptr;

    m_troops.for_each([&](Troop& troop, uint16_t) {
        if (!troop.alive || troop.state == TroopState::DEAD) return;

        fp32 dist = fp_distance(def_center, troop.pos);

        // Mortar minimum range
        if (data.min_attack_range > PFP_ZERO && dist < data.min_attack_range)
            return;

        if (dist <= data.attack_range) {
            if (dist < best_dist || (dist == best_dist && troop.id < best_id)) {
                best_dist = dist;
                best_id = troop.id;
                best_troop = &troop;
            }
        }
    });

    if (!best_troop) return;

    // Apply damage
    if (data.splash_radius > PFP_ZERO) {
        // Splash: damage all troops near target
        Vec2fp splash_center = best_troop->pos;
        m_troops.for_each([&](Troop& troop, uint16_t) {
            if (!troop.alive) return;
            fp32 d = fp_distance(troop.pos, splash_center);
            if (d <= data.splash_radius) {
                troop.hp -= data.attack_damage;
                if (troop.hp <= PFP_ZERO) {
                    troop.hp = PFP_ZERO;
                    kill_troop(troop);
                }
            }
        });
    } else {
        // Single target
        best_troop->hp -= data.attack_damage;
        if (best_troop->hp <= PFP_ZERO) {
            best_troop->hp = PFP_ZERO;
            kill_troop(*best_troop);
        }
    }

    defense.attack_timer = data.attack_cooldown;
    emit_event({ SimEvent::DEFENSE_FIRE, defense.id, best_id, data.attack_damage, def_center });
}

void Simulation::destroy_building(Building& building) {
    building.alive = false;
    m_building_by_id[building.id] = nullptr;
    m_destroyed_buildings++;

    if (building.is_wall()) {
        m_wall_version++;
    }

    const auto& data = get_building_data(building.type);
    m_grid.remove(building.grid_x, building.grid_y, data.size, data.size);

    if (building.type == BuildingType::TOWN_HALL) {
        m_town_hall_destroyed = true;
    }

    emit_event({ SimEvent::BUILDING_DESTROYED, building.id, 0, PFP_ZERO, building.center() });
}

void Simulation::kill_troop(Troop& troop) {
    troop.alive = false;
    troop.state = TroopState::DEAD;
    emit_event({ SimEvent::TROOP_KILLED, troop.id, 0, PFP_ZERO, troop.pos });
}

void Simulation::check_victory() {
    // Check stars
    uint8_t stars = 0;
    if (m_town_hall_destroyed) stars++;
    if (m_total_buildings > 0 && m_destroyed_buildings * 100 / m_total_buildings >= 50) stars++;
    if (m_destroyed_buildings == m_total_buildings) stars++;

    if (stars > m_result.stars) {
        m_result.stars = stars;
        emit_event({ SimEvent::STAR_EARNED, 0, 0, int_to_fp(stars), {} });
    }

    m_result.destruction_percent = m_total_buildings > 0
        ? static_cast<uint8_t>(m_destroyed_buildings * 100 / m_total_buildings) : 0;
    m_result.duration_ticks = m_tick;

    // Check end conditions
    bool all_troops_dead = true;
    m_troops.for_each([&](const Troop& t, uint16_t) {
        if (t.alive) all_troops_dead = false;
    });

    bool timeout = (m_tick >= MAX_ATTACK_TICKS);

    if (stars == 3 || (all_troops_dead && m_troops.active_count() > 0) || timeout) {
        m_result.finished = true;
        m_attack_active = false;
        platform::log_info("Attack ended: %d stars, %d%% destruction, %u ticks",
                           m_result.stars, m_result.destruction_percent, m_tick);
    }
}

void Simulation::emit_event(const SimEvent& event) {
    if (m_event_count < MAX_EVENTS_PER_TICK) {
        m_events[m_event_count++] = event;
    }
}

uint32_t Simulation::compute_state_hash() const {
    // Simple CRC-like hash of all simulation state
    uint32_t hash = 0xDEADBEEF;

    auto mix = [&](uint32_t val) {
        hash ^= val;
        hash = (hash << 13) | (hash >> 19);
        hash *= 0x5BD1E995;
    };

    mix(m_tick);
    mix(m_seed);

    m_buildings.for_each([&](const Building& b, uint16_t) {
        mix(b.id);
        mix(static_cast<uint32_t>(b.hp));
        mix(static_cast<uint32_t>(b.alive));
    });

    m_troops.for_each([&](const Troop& t, uint16_t) {
        mix(t.id);
        mix(static_cast<uint32_t>(t.pos.x));
        mix(static_cast<uint32_t>(t.pos.y));
        mix(static_cast<uint32_t>(t.hp));
        mix(static_cast<uint32_t>(t.state));
    });

    return hash;
}

} // namespace miniclash
