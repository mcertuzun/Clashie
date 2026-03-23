#include <gtest/gtest.h>
#include "miniclash/simulation.h"
#include "miniclash/building.h"
#include "miniclash/troop.h"
#include "miniclash/grid.h"
#include "pebble/core/fixed_point.h"

using namespace miniclash;
using namespace pebble;

// ---------------------------------------------------------------------------
// Helper utilities
// ---------------------------------------------------------------------------

// Create a minimal base layout with a single town hall at (10,10)
static BaseLayout create_simple_base() {
    BaseLayout layout;
    layout.building_count = 1;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::TOWN_HALL), 1, 10, 10 };
    return layout;
}

// Create a base layout with a town hall and a cannon
static BaseLayout create_base_with_defense() {
    BaseLayout layout;
    layout.building_count = 2;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::TOWN_HALL), 1, 10, 10 };
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::CANNON), 1, 20, 20 };
    return layout;
}

// Create a deploy troop InputEvent at the given grid position
static InputEvent deploy_troop_at(TroopType type, int x, int y) {
    InputEvent ev;
    ev.type = InputEvent::DEPLOY_TROOP;
    ev.troop_type = static_cast<uint8_t>(type);
    ev.world_x = int_to_fp(x);
    ev.world_y = int_to_fp(y);
    return ev;
}

// Wrap a single InputEvent into a TickInput
static TickInput make_tick_input(const InputEvent& ev, uint32_t tick = 0) {
    TickInput ti;
    ti.tick = tick;
    ti.count = 1;
    ti.events[0] = ev;
    return ti;
}

// Empty tick input (no events)
static TickInput empty_tick(uint32_t tick = 0) {
    TickInput ti;
    ti.tick = tick;
    ti.count = 0;
    return ti;
}

// Run N ticks with no input
static void advance_ticks(Simulation& sim, int n) {
    for (int i = 0; i < n; ++i) {
        sim.tick(empty_tick(sim.current_tick()));
    }
}

// ---------------------------------------------------------------------------
// Grid tests
// ---------------------------------------------------------------------------

TEST(Grid, CanPlaceEmpty) {
    TileGrid grid;
    grid.clear();
    EXPECT_TRUE(grid.can_place(5, 5, 3, 3));
}

TEST(Grid, CannotPlaceOccupied) {
    TileGrid grid;
    grid.clear();
    grid.place(5, 5, 3, 3, 1);
    EXPECT_FALSE(grid.can_place(5, 5, 3, 3));
    // Overlapping by one tile
    EXPECT_FALSE(grid.can_place(7, 7, 2, 2));
}

TEST(Grid, CannotPlaceOutOfBounds) {
    TileGrid grid;
    grid.clear();
    // Completely out
    EXPECT_FALSE(grid.can_place(GRID_SIZE, 0, 1, 1));
    EXPECT_FALSE(grid.can_place(0, GRID_SIZE, 1, 1));
    // Partially out (extends past boundary)
    EXPECT_FALSE(grid.can_place(GRID_SIZE - 1, 0, 3, 3));
    // Negative coordinate
    EXPECT_FALSE(grid.can_place(-1, 0, 1, 1));
}

TEST(Grid, RemoveFreesSpace) {
    TileGrid grid;
    grid.clear();
    grid.place(5, 5, 3, 3, 42);
    EXPECT_FALSE(grid.can_place(5, 5, 3, 3));
    grid.remove(5, 5, 3, 3);
    EXPECT_TRUE(grid.can_place(5, 5, 3, 3));
    // Verify cell state is EMPTY
    EXPECT_EQ(grid.cells[5][5], CellState::EMPTY);
    EXPECT_EQ(grid.building_id[5][5], 0);
}

// ---------------------------------------------------------------------------
// Building tests
// ---------------------------------------------------------------------------

TEST(Building, LoadBase) {
    Simulation sim;
    sim.init();

    BaseLayout layout = create_base_with_defense();
    sim.load_base(layout);

    int count = 0;
    bool has_town_hall = false;
    bool has_cannon = false;
    sim.for_each_building([&](const Building& b) {
        count++;
        if (b.type == BuildingType::TOWN_HALL) has_town_hall = true;
        if (b.type == BuildingType::CANNON) has_cannon = true;
    });

    EXPECT_EQ(count, 2);
    EXPECT_TRUE(has_town_hall);
    EXPECT_TRUE(has_cannon);
}

TEST(Building, BuildingData) {
    // Town Hall: size 4, HP 2000, not defense
    const auto& th = get_building_data(BuildingType::TOWN_HALL);
    EXPECT_EQ(th.size, 4);
    EXPECT_EQ(th.max_hp, int_to_fp(2000));
    EXPECT_FALSE(th.is_defense);
    EXPECT_FALSE(th.is_wall);

    // Cannon: size 3, HP 800, is defense
    const auto& cannon = get_building_data(BuildingType::CANNON);
    EXPECT_EQ(cannon.size, 3);
    EXPECT_EQ(cannon.max_hp, int_to_fp(800));
    EXPECT_TRUE(cannon.is_defense);
    EXPECT_FALSE(cannon.is_wall);

    // Wall: size 1, HP 500, is_wall
    const auto& wall = get_building_data(BuildingType::WALL);
    EXPECT_EQ(wall.size, 1);
    EXPECT_EQ(wall.max_hp, int_to_fp(500));
    EXPECT_FALSE(wall.is_defense);
    EXPECT_TRUE(wall.is_wall);
}

// ---------------------------------------------------------------------------
// Troop tests
// ---------------------------------------------------------------------------

TEST(Troop, DeployTroop) {
    Simulation sim;
    sim.init();
    sim.load_base(create_simple_base());
    sim.start_attack();

    InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 0, 0);
    TickInput ti = make_tick_input(ev);
    sim.tick(ti);

    int troop_count = 0;
    pebble::Vec2fp deployed_pos = {};
    TroopType deployed_type = TroopType::BARBARIAN;
    sim.for_each_troop([&](const miniclash::Troop& t) {
        troop_count++;
        deployed_pos = t.pos;
        deployed_type = t.type;
    });

    EXPECT_EQ(troop_count, 1);
    EXPECT_EQ(deployed_pos.x, int_to_fp(0));
    EXPECT_EQ(deployed_pos.y, int_to_fp(0));
    EXPECT_EQ(deployed_type, TroopType::BARBARIAN);
}

TEST(Troop, TroopTargetsNearest) {
    // Place two buildings at different distances from the troop spawn
    Simulation sim;
    sim.init();

    BaseLayout layout;
    layout.building_count = 2;
    // Gold storage close (at 5,5)
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::GOLD_STORAGE), 1, 5, 5 };
    // Elixir storage far (at 30,30)
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::ELIXIR_STORAGE), 1, 30, 30 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarian at origin (0,0) -- closer to the storage at (5,5)
    InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 0, 0);
    sim.tick(make_tick_input(ev));

    // Advance enough ticks for spawn to complete and target to be found
    // Barbarian deploy time is 0.2s = 4 ticks at 20 ticks/s, plus 1 tick for FIND_TARGET
    advance_ticks(sim, 10);

    // Check that the troop targets the closer building (gold storage)
    uint16_t target_id = 0;
    sim.for_each_troop([&](const miniclash::Troop& t) {
        target_id = t.target_building;
    });

    // The first building loaded gets id 1 (gold storage at 5,5)
    // The second gets id 2 (elixir storage at 30,30)
    // Barbarian should target id 1 (nearer)
    EXPECT_EQ(target_id, 1);
}

TEST(Troop, GiantTargetsDefenses) {
    Simulation sim;
    sim.init();

    BaseLayout layout;
    layout.building_count = 2;
    // Gold storage very close (at 2,2)
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::GOLD_STORAGE), 1, 2, 2 };
    // Cannon farther (at 15,15) -- defense building
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::CANNON), 1, 15, 15 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy giant at origin (0,0)
    InputEvent ev = deploy_troop_at(TroopType::GIANT, 0, 0);
    sim.tick(make_tick_input(ev));

    // Giant deploy time is 0.5s = 10 ticks
    advance_ticks(sim, 15);

    uint16_t target_id = 0;
    sim.for_each_troop([&](const miniclash::Troop& t) {
        target_id = t.target_building;
    });

    // Giant prefers defenses, so it should target the cannon (id 2),
    // even though gold storage (id 1) is closer.
    EXPECT_EQ(target_id, 2);
}

// ---------------------------------------------------------------------------
// Combat tests
// ---------------------------------------------------------------------------

TEST(Combat, TroopDamagesBuilding) {
    Simulation sim;
    sim.init();

    // Place a single building right next to where the troop will spawn
    BaseLayout layout;
    layout.building_count = 1;
    // Builder hut at (2,0), size=2, HP=250
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::BUILDER_HUT), 1, 2, 0 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarian adjacent to the building
    // Builder hut center = (2 + 1, 0 + 1) = (3, 1)
    // Barbarian attack range = 1, builder hut size/2 = 1, so range check = dist <= 2
    // Deploy at (1, 1) => dist from (3,1) = 2 tiles => in range
    InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1, 1);
    sim.tick(make_tick_input(ev));

    // Record initial HP
    fp32 initial_hp = PFP_ZERO;
    sim.for_each_building([&](const Building& b) {
        initial_hp = b.hp;
    });
    EXPECT_EQ(initial_hp, int_to_fp(250));

    // Run enough ticks for the barbarian to spawn, find target, move, and attack
    // Spawn: 4 ticks, find target: 1 tick, movement + attack happens quickly
    advance_ticks(sim, 40);

    fp32 current_hp = int_to_fp(250);
    sim.for_each_building([&](const Building& b) {
        current_hp = b.hp;
    });

    EXPECT_LT(current_hp, initial_hp);
}

TEST(Combat, DefenseAttacksTroop) {
    Simulation sim;
    sim.init();

    // Place a cannon at (10,10) with range 9
    BaseLayout layout;
    layout.building_count = 1;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::CANNON), 1, 10, 10 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarian within cannon range
    // Cannon center = (10+1.5, 10+1.5) = (11.5, 11.5), range = 9
    // Deploy at (15, 15) => manhattan ~7, euclidean ~5 => in range
    InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 15, 15);
    sim.tick(make_tick_input(ev));

    // Run ticks: spawn (4 ticks) + cooldown + defense fires
    advance_ticks(sim, 30);

    fp32 current_hp = int_to_fp(450);
    sim.for_each_troop([&](const miniclash::Troop& t) {
        current_hp = t.hp;
    });

    // Cannon should have dealt some damage (cannon damage = 62 per hit)
    EXPECT_LT(current_hp, int_to_fp(450));
}

TEST(Combat, BuildingDestroyed) {
    Simulation sim;
    sim.init();

    // Builder hut: HP=250, smallest HP building
    BaseLayout layout;
    layout.building_count = 1;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::BUILDER_HUT), 1, 2, 0 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarian next to it
    InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1, 1);
    sim.tick(make_tick_input(ev));

    // Run many ticks to destroy the builder hut
    // Barbarian DPS = 26, builder hut HP = 250 => ~10 attacks
    // Attack cooldown = 1s = 20 ticks, so ~200 ticks to destroy
    advance_ticks(sim, 250);

    // Check that the building is destroyed (removed from alive buildings)
    int alive_count = 0;
    sim.for_each_building([&](const Building&) {
        alive_count++;
    });

    EXPECT_EQ(alive_count, 0);

    // Grid should be cleared where the building was
    EXPECT_TRUE(sim.grid().can_place(2, 0, 2, 2));
}

// ---------------------------------------------------------------------------
// Determinism tests
// ---------------------------------------------------------------------------

TEST(Determinism, SameInputsSameHash) {
    auto run_sim = []() -> uint32_t {
        Simulation sim;
        sim.init();
        sim.load_base(create_base_with_defense());
        sim.start_attack();

        // Deploy a barbarian on tick 0
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 0, 0);
        sim.tick(make_tick_input(ev, 0));

        // Run 100 ticks
        for (uint32_t i = 1; i <= 100; ++i) {
            sim.tick(empty_tick(i));
        }

        return sim.compute_state_hash();
    };

    uint32_t hash1 = run_sim();
    uint32_t hash2 = run_sim();
    EXPECT_EQ(hash1, hash2);
}

TEST(Determinism, DifferentInputsDifferentHash) {
    auto run_sim = [](int deploy_x, int deploy_y) -> uint32_t {
        Simulation sim;
        sim.init();
        sim.load_base(create_base_with_defense());
        sim.start_attack();

        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, deploy_x, deploy_y);
        sim.tick(make_tick_input(ev, 0));

        for (uint32_t i = 1; i <= 100; ++i) {
            sim.tick(empty_tick(i));
        }

        return sim.compute_state_hash();
    };

    uint32_t hash_a = run_sim(0, 0);
    uint32_t hash_b = run_sim(5, 5);
    EXPECT_NE(hash_a, hash_b);
}

// ---------------------------------------------------------------------------
// Victory tests
// ---------------------------------------------------------------------------

TEST(Victory, TownHallStar) {
    Simulation sim;
    sim.init();

    // Two buildings: town hall (HP=2000) and gold storage (HP=1400)
    // Destroying town hall alone earns 1 star (TH star) but not 50% (only 1/2 = 50% exactly)
    BaseLayout layout;
    layout.building_count = 3;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::TOWN_HALL), 1, 2, 0 };
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::GOLD_STORAGE), 1, 30, 30 };
    layout.buildings[2] = { static_cast<uint8_t>(BuildingType::ELIXIR_STORAGE), 1, 30, 20 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy multiple barbarians to quickly destroy the town hall
    // TH HP = 2000, barbarian DPS = 26 per attack, cooldown = 1s = 20 ticks
    // With many barbs, they can destroy it faster
    for (int i = 0; i < 5; ++i) {
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1 + i, 2);
        sim.tick(make_tick_input(ev, sim.current_tick()));
    }

    // Run enough ticks to destroy town hall
    // 5 barbs * 26 dps = 130 dmg per attack cycle, 2000/130 ~ 16 attack cycles
    // 16 cycles * 20 ticks = 320 ticks + spawn time
    advance_ticks(sim, 500);

    const auto& result = sim.result();
    // Town hall star should be earned; 1 of 3 buildings destroyed = 33% (no 50% star)
    EXPECT_GE(result.stars, 1);
}

TEST(Victory, FiftyPercentStar) {
    Simulation sim;
    sim.init();

    // 2 buildings: destroy 1 = 50%
    BaseLayout layout;
    layout.building_count = 2;
    // Builder hut (HP=250) close, easy to destroy
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::BUILDER_HUT), 1, 2, 0 };
    // Gold storage far away, won't be destroyed
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::GOLD_STORAGE), 1, 35, 35 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarians next to builder hut
    for (int i = 0; i < 3; ++i) {
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1, i);
        sim.tick(make_tick_input(ev, sim.current_tick()));
    }

    // Run enough ticks to destroy builder hut (HP=250, 3 barbs * 26 = 78 per cycle)
    advance_ticks(sim, 200);

    const auto& result = sim.result();
    // 1 of 2 destroyed = 50% => earns the 50% star
    EXPECT_GE(result.destruction_percent, 50);
    EXPECT_GE(result.stars, 1);
}

TEST(Victory, ThreeStars) {
    Simulation sim;
    sim.init();

    // Single builder hut (HP=250, size=2, non-defense)
    BaseLayout layout;
    layout.building_count = 1;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::BUILDER_HUT), 1, 2, 0 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy barbarians to destroy it
    for (int i = 0; i < 3; ++i) {
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1, i);
        sim.tick(make_tick_input(ev, sim.current_tick()));
    }

    advance_ticks(sim, 250);

    const auto& result = sim.result();
    // All buildings destroyed => 100% destruction
    // Stars breakdown: 50% star = 1, 100% star = 1, TH star = 0 (no TH in base)
    // Total = 2 stars. Attack does not auto-finish (needs 3 stars, or all troops dead,
    // or timeout). Troops are alive with no targets, so attack continues.
    EXPECT_EQ(result.destruction_percent, 100);
    EXPECT_EQ(result.stars, 2);
}

TEST(Victory, ThreeStarsWithTownHall) {
    // Test actual 3-star scenario: must include a town hall
    Simulation sim;
    sim.init();

    // Two buildings: town hall + builder hut (both small HP for quick test)
    BaseLayout layout;
    layout.building_count = 2;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::BUILDER_HUT), 1, 2, 0 };
    layout.buildings[1] = { static_cast<uint8_t>(BuildingType::TOWN_HALL), 1, 6, 0 };
    sim.load_base(layout);
    sim.start_attack();

    // Deploy many barbarians near both buildings
    for (int i = 0; i < 5; ++i) {
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 1, i);
        sim.tick(make_tick_input(ev, sim.current_tick()));
    }
    for (int i = 0; i < 5; ++i) {
        InputEvent ev = deploy_troop_at(TroopType::BARBARIAN, 5, i);
        sim.tick(make_tick_input(ev, sim.current_tick()));
    }

    // TH HP = 2000, need many ticks
    advance_ticks(sim, 1500);

    const auto& result = sim.result();
    EXPECT_EQ(result.destruction_percent, 100);
    EXPECT_EQ(result.stars, 3);
    EXPECT_TRUE(result.finished);
}

TEST(Victory, TimeoutEndsAttack) {
    Simulation sim;
    sim.init();

    // Place a building with lots of HP so it won't be destroyed
    BaseLayout layout;
    layout.building_count = 1;
    layout.buildings[0] = { static_cast<uint8_t>(BuildingType::TOWN_HALL), 1, 20, 20 };
    sim.load_base(layout);
    sim.start_attack();

    // Don't deploy any troops. Just run until timeout.
    // MAX_ATTACK_TICKS = 3600
    // But check_victory only ends on timeout OR all troops dead (with > 0 troops) OR 3 stars.
    // With 0 troops deployed, m_troops.active_count() == 0, so the
    // "all_troops_dead && active_count > 0" branch won't trigger.
    // Only timeout will trigger.
    for (uint32_t i = 0; i < Simulation::MAX_ATTACK_TICKS + 1; ++i) {
        sim.tick(empty_tick(i));
        if (sim.result().finished) break;
    }

    EXPECT_TRUE(sim.result().finished);
    EXPECT_EQ(sim.result().stars, 0);
    // Duration should be at or very near MAX_ATTACK_TICKS
    EXPECT_GE(sim.result().duration_ticks, Simulation::MAX_ATTACK_TICKS);
}
