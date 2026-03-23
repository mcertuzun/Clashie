#include "miniclash/troop.h"

using namespace pebble;

namespace miniclash {

static const TroopData TROOP_TABLE[] = {
    // BARBARIAN
    { "Barbarian",
      int_to_fp(450), int_to_fp(26), int_to_fp(16),
      int_to_fp(1), int_to_fp(1),
      1, float_to_fp(0.2f),
      PFP_ZERO, PFP_ZERO,
      TroopData::TARGET_ANY },

    // ARCHER
    { "Archer",
      int_to_fp(200), int_to_fp(22), int_to_fp(24),
      float_to_fp(3.5f), float_to_fp(1.2f),
      1, float_to_fp(0.2f),
      PFP_ZERO, PFP_ZERO,
      TroopData::TARGET_ANY },

    // GIANT
    { "Giant",
      int_to_fp(2800), int_to_fp(38), int_to_fp(12),
      int_to_fp(1), int_to_fp(2),
      5, float_to_fp(0.5f),
      PFP_ZERO, PFP_ZERO,
      TroopData::TARGET_DEFENSES },

    // WALL_BREAKER
    { "Wall Breaker",
      int_to_fp(200), int_to_fp(46), int_to_fp(28),
      int_to_fp(1), float_to_fp(1.5f),
      2, float_to_fp(0.3f),
      int_to_fp(2), int_to_fp(40),
      TroopData::TARGET_WALLS },
};

const TroopData& get_troop_data(TroopType type) {
    return TROOP_TABLE[static_cast<uint8_t>(type)];
}

} // namespace miniclash
