#include "miniclash/building.h"

using namespace pebble;

namespace miniclash {

static const BuildingData BUILDING_TABLE[] = {
    // TOWN_HALL
    { "Town Hall", 4, int_to_fp(2000), false, false,
      PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO },

    // CANNON
    { "Cannon", 3, int_to_fp(800), true, false,
      int_to_fp(9), PFP_ZERO, int_to_fp(62), float_to_fp(0.8f), PFP_ZERO },

    // ARCHER_TOWER
    { "Archer Tower", 3, int_to_fp(700), true, false,
      int_to_fp(10), PFP_ZERO, int_to_fp(50), int_to_fp(1), PFP_ZERO },

    // MORTAR
    { "Mortar", 3, int_to_fp(600), true, false,
      int_to_fp(11), int_to_fp(4), int_to_fp(30), int_to_fp(5), float_to_fp(1.5f) },

    // GOLD_STORAGE
    { "Gold Storage", 3, int_to_fp(1400), false, false,
      PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO },

    // ELIXIR_STORAGE
    { "Elixir Storage", 3, int_to_fp(1400), false, false,
      PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO },

    // WALL
    { "Wall", 1, int_to_fp(500), false, true,
      PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO },

    // BUILDER_HUT
    { "Builder Hut", 2, int_to_fp(250), false, false,
      PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO, PFP_ZERO },
};

const BuildingData& get_building_data(BuildingType type) {
    return BUILDING_TABLE[static_cast<uint8_t>(type)];
}

} // namespace miniclash
