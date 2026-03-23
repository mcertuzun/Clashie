#include "pebble/core/fixed_point.h"

namespace pebble {

// Sine lookup table (1024 entries covering 0 to 2*pi)
// Each entry = sin(i * 2*pi / 1024) * 65536
static constexpr int TRIG_TABLE_SIZE = 1024;
static fp32 sin_table[TRIG_TABLE_SIZE];
static bool trig_tables_initialized = false;

static void init_trig_tables() {
    if (trig_tables_initialized) return;
    // Build table at startup using double precision, then never use float again
    for (int i = 0; i < TRIG_TABLE_SIZE; ++i) {
        double angle = (static_cast<double>(i) / TRIG_TABLE_SIZE) * 6.283185307179586;
        double s = __builtin_sin(angle);
        sin_table[i] = static_cast<fp32>(s * 65536.0);
    }
    trig_tables_initialized = true;
}

fp32 fp_sqrt(fp32 v) {
    if (v <= PFP_ZERO) return PFP_ZERO;

    // Newton-Raphson in fixed-point
    // Initial guess: shift-based approximation
    uint32_t uv = static_cast<uint32_t>(v);
    fp32 guess = static_cast<fp32>(uv);

    // Find a reasonable starting point
    int shift = 0;
    uint32_t temp = uv;
    while (temp > 0) { temp >>= 1; ++shift; }
    guess = static_cast<fp32>(1u << ((shift + 16) / 2));

    // 8 iterations of Newton-Raphson: x = (x + v/x) / 2
    for (int i = 0; i < 8; ++i) {
        if (guess == 0) return PFP_ZERO;
        fp32 div = fp_div(v, guess);
        guess = (guess + div) >> 1;
    }
    return guess;
}

fp32 fp_sin(fp32 angle) {
    init_trig_tables();

    // Normalize angle to [0, 2*pi) range
    // angle is in fixed-point radians
    while (angle < PFP_ZERO) angle += PFP_TWO_PI;
    while (angle >= PFP_TWO_PI) angle -= PFP_TWO_PI;

    // Map to table index [0, 1024)
    int64_t idx64 = (static_cast<int64_t>(angle) * TRIG_TABLE_SIZE) / PFP_TWO_PI;
    int idx = static_cast<int>(idx64) & (TRIG_TABLE_SIZE - 1);

    // Linear interpolation between table entries
    int next_idx = (idx + 1) & (TRIG_TABLE_SIZE - 1);
    int64_t frac = (static_cast<int64_t>(angle) * TRIG_TABLE_SIZE) % PFP_TWO_PI;
    if (frac < 0) frac += PFP_TWO_PI;
    fp32 t = static_cast<fp32>((frac << 16) / PFP_TWO_PI);

    fp32 a = sin_table[idx];
    fp32 b = sin_table[next_idx];
    return a + fp_mul(b - a, t);
}

fp32 fp_cos(fp32 angle) {
    // cos(x) = sin(x + pi/2)
    return fp_sin(angle + (PFP_PI >> 1));
}

fp32 fp_atan2(fp32 y, fp32 x) {
    // CORDIC-style atan2 approximation
    // Returns angle in fixed-point radians [0, 2*pi)
    if (x == PFP_ZERO && y == PFP_ZERO) return PFP_ZERO;

    fp32 abs_y = fp_abs(y);
    fp32 abs_x = fp_abs(x);
    fp32 angle;

    // atan approximation for first octant
    if (abs_x >= abs_y) {
        fp32 r = fp_div(abs_y, abs_x);
        // atan(r) ~= r * (pi/4) for small r, with correction
        // Using: atan(r) ~= r * 0.9817 - r^3 * 0.1963 (in fixed-point)
        fp32 r3 = fp_mul(fp_mul(r, r), r);
        angle = fp_mul(r, 64357) - fp_mul(r3, 12867); // coefficients * 65536
    } else {
        fp32 r = fp_div(abs_x, abs_y);
        fp32 r3 = fp_mul(fp_mul(r, r), r);
        angle = (PFP_PI >> 1) - fp_mul(r, 64357) + fp_mul(r3, 12867);
    }

    // Map to correct quadrant
    if (x < 0 && y >= 0) angle = PFP_PI - angle;
    else if (x < 0 && y < 0) angle = PFP_PI + angle;
    else if (x >= 0 && y < 0) angle = PFP_TWO_PI - angle;

    return angle;
}

fp32 fp_length_sq(Vec2fp v) {
    return fp_mul(v.x, v.x) + fp_mul(v.y, v.y);
}

fp32 fp_length(Vec2fp v) {
    return fp_sqrt(fp_length_sq(v));
}

fp32 fp_distance_sq(Vec2fp a, Vec2fp b) {
    return fp_length_sq(a - b);
}

fp32 fp_distance(Vec2fp a, Vec2fp b) {
    return fp_length(a - b);
}

Vec2fp fp_normalize(Vec2fp v) {
    fp32 len = fp_length(v);
    if (len == PFP_ZERO) return { PFP_ZERO, PFP_ZERO };
    return { fp_div(v.x, len), fp_div(v.y, len) };
}

} // namespace pebble
