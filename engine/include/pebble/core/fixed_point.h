#pragma once

#include <cstdint>

namespace pebble {

// 16.16 fixed-point (32-bit: 16 bits integer, 16 bits fraction)
using fp32 = int32_t;

// Constants — prefixed with PFP_ to avoid collision with macOS math.h FP_ZERO etc.
constexpr fp32 PFP_ONE       = 1 << 16;        // 65536
constexpr fp32 PFP_HALF      = 1 << 15;        // 32768
constexpr fp32 PFP_ZERO      = 0;
constexpr fp32 PFP_MAX       = 0x7FFFFFFF;
constexpr fp32 PFP_PI        = 205887;          // pi * 65536
constexpr fp32 PFP_TWO_PI    = 411775;          // 2*pi * 65536

// Conversion
inline constexpr fp32 int_to_fp(int32_t v) { return v << 16; }
inline constexpr int32_t fp_to_int(fp32 v) { return v >> 16; }
inline float fp_to_float(fp32 v) { return static_cast<float>(v) / 65536.0f; }
inline fp32 float_to_fp(float v) { return static_cast<fp32>(v * 65536.0f); }

// Arithmetic
inline fp32 fp_mul(fp32 a, fp32 b) {
    return static_cast<fp32>((static_cast<int64_t>(a) * static_cast<int64_t>(b)) >> 16);
}

inline fp32 fp_div(fp32 a, fp32 b) {
    return static_cast<fp32>((static_cast<int64_t>(a) << 16) / b);
}

inline fp32 fp_abs(fp32 v) {
    return v < 0 ? -v : v;
}

inline fp32 fp_min(fp32 a, fp32 b) { return a < b ? a : b; }
inline fp32 fp_max(fp32 a, fp32 b) { return a > b ? a : b; }

inline fp32 fp_clamp(fp32 v, fp32 lo, fp32 hi) {
    return fp_min(fp_max(v, lo), hi);
}

// Square root (Newton-Raphson in fixed-point)
fp32 fp_sqrt(fp32 v);

// Trigonometry (lookup table based)
fp32 fp_sin(fp32 angle);
fp32 fp_cos(fp32 angle);
fp32 fp_atan2(fp32 y, fp32 x);

// 2D fixed-point vector
struct Vec2fp {
    fp32 x = PFP_ZERO;
    fp32 y = PFP_ZERO;

    Vec2fp operator+(const Vec2fp& o) const { return { x + o.x, y + o.y }; }
    Vec2fp operator-(const Vec2fp& o) const { return { x - o.x, y - o.y }; }
    Vec2fp& operator+=(const Vec2fp& o) { x += o.x; y += o.y; return *this; }
    Vec2fp& operator-=(const Vec2fp& o) { x -= o.x; y -= o.y; return *this; }
    bool operator==(const Vec2fp& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2fp& o) const { return !(*this == o); }
};

inline Vec2fp fp_scale(Vec2fp v, fp32 s) {
    return { fp_mul(v.x, s), fp_mul(v.y, s) };
}

fp32   fp_length(Vec2fp v);
fp32   fp_length_sq(Vec2fp v);
fp32   fp_distance(Vec2fp a, Vec2fp b);
fp32   fp_distance_sq(Vec2fp a, Vec2fp b);
Vec2fp fp_normalize(Vec2fp v);

// Manhattan distance (no sqrt, fully deterministic, fast)
inline fp32 fp_manhattan(Vec2fp a, Vec2fp b) {
    return fp_abs(a.x - b.x) + fp_abs(a.y - b.y);
}

} // namespace pebble
