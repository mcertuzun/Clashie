#include <gtest/gtest.h>
#include "pebble/core/fixed_point.h"

using namespace pebble;

TEST(FixedPoint, Conversion) {
    EXPECT_EQ(int_to_fp(1), PFP_ONE);
    EXPECT_EQ(int_to_fp(0), PFP_ZERO);
    EXPECT_EQ(fp_to_int(PFP_ONE), 1);
    EXPECT_EQ(fp_to_int(int_to_fp(42)), 42);
    EXPECT_EQ(fp_to_int(int_to_fp(-10)), -10);
}

TEST(FixedPoint, FloatConversion) {
    EXPECT_NEAR(fp_to_float(PFP_ONE), 1.0f, 0.001f);
    EXPECT_NEAR(fp_to_float(PFP_HALF), 0.5f, 0.001f);
    EXPECT_NEAR(fp_to_float(float_to_fp(3.14f)), 3.14f, 0.001f);
}

TEST(FixedPoint, Multiplication) {
    fp32 two = int_to_fp(2);
    fp32 three = int_to_fp(3);
    EXPECT_EQ(fp_to_int(fp_mul(two, three)), 6);

    fp32 half = PFP_HALF;
    EXPECT_EQ(fp_mul(PFP_ONE, half), half);

    // Negative
    fp32 neg_two = int_to_fp(-2);
    EXPECT_EQ(fp_to_int(fp_mul(neg_two, three)), -6);
}

TEST(FixedPoint, Division) {
    fp32 six = int_to_fp(6);
    fp32 two = int_to_fp(2);
    EXPECT_EQ(fp_to_int(fp_div(six, two)), 3);

    fp32 one = PFP_ONE;
    EXPECT_EQ(fp_div(one, two), PFP_HALF);
}

TEST(FixedPoint, AbsMinMax) {
    fp32 neg = int_to_fp(-5);
    fp32 pos = int_to_fp(5);
    EXPECT_EQ(fp_abs(neg), pos);
    EXPECT_EQ(fp_abs(pos), pos);
    EXPECT_EQ(fp_min(neg, pos), neg);
    EXPECT_EQ(fp_max(neg, pos), pos);
}

TEST(FixedPoint, Clamp) {
    fp32 lo = int_to_fp(0);
    fp32 hi = int_to_fp(10);
    EXPECT_EQ(fp_clamp(int_to_fp(5), lo, hi), int_to_fp(5));
    EXPECT_EQ(fp_clamp(int_to_fp(-3), lo, hi), lo);
    EXPECT_EQ(fp_clamp(int_to_fp(15), lo, hi), hi);
}

TEST(FixedPoint, Sqrt) {
    EXPECT_EQ(fp_to_int(fp_sqrt(int_to_fp(4))), 2);
    EXPECT_EQ(fp_to_int(fp_sqrt(int_to_fp(9))), 3);
    EXPECT_EQ(fp_to_int(fp_sqrt(int_to_fp(16))), 4);
    EXPECT_EQ(fp_to_int(fp_sqrt(int_to_fp(100))), 10);
    EXPECT_EQ(fp_sqrt(PFP_ZERO), PFP_ZERO);
}

TEST(FixedPoint, SqrtPrecision) {
    // sqrt(2) ~= 1.41421
    fp32 sqrt2 = fp_sqrt(int_to_fp(2));
    EXPECT_NEAR(fp_to_float(sqrt2), 1.41421f, 0.01f);
}

TEST(FixedPoint, Vec2Operations) {
    Vec2fp a = { int_to_fp(3), int_to_fp(4) };
    Vec2fp b = { int_to_fp(1), int_to_fp(1) };

    Vec2fp sum = a + b;
    EXPECT_EQ(fp_to_int(sum.x), 4);
    EXPECT_EQ(fp_to_int(sum.y), 5);

    Vec2fp diff = a - b;
    EXPECT_EQ(fp_to_int(diff.x), 2);
    EXPECT_EQ(fp_to_int(diff.y), 3);
}

TEST(FixedPoint, Vec2Length) {
    // 3-4-5 triangle
    Vec2fp v = { int_to_fp(3), int_to_fp(4) };
    fp32 len = fp_length(v);
    EXPECT_EQ(fp_to_int(len), 5);
}

TEST(FixedPoint, Vec2Distance) {
    Vec2fp a = { int_to_fp(0), int_to_fp(0) };
    Vec2fp b = { int_to_fp(3), int_to_fp(4) };
    fp32 dist = fp_distance(a, b);
    EXPECT_EQ(fp_to_int(dist), 5);
}

TEST(FixedPoint, Vec2Normalize) {
    Vec2fp v = { int_to_fp(3), int_to_fp(4) };
    Vec2fp n = fp_normalize(v);
    // Should be (0.6, 0.8)
    EXPECT_NEAR(fp_to_float(n.x), 0.6f, 0.01f);
    EXPECT_NEAR(fp_to_float(n.y), 0.8f, 0.01f);

    // Length should be ~1.0
    fp32 len = fp_length(n);
    EXPECT_NEAR(fp_to_float(len), 1.0f, 0.01f);
}

TEST(FixedPoint, Vec2NormalizeZero) {
    Vec2fp zero = { PFP_ZERO, PFP_ZERO };
    Vec2fp n = fp_normalize(zero);
    EXPECT_EQ(n.x, PFP_ZERO);
    EXPECT_EQ(n.y, PFP_ZERO);
}

TEST(FixedPoint, Manhattan) {
    Vec2fp a = { int_to_fp(1), int_to_fp(2) };
    Vec2fp b = { int_to_fp(4), int_to_fp(6) };
    fp32 dist = fp_manhattan(a, b);
    EXPECT_EQ(fp_to_int(dist), 7); // |4-1| + |6-2| = 3 + 4 = 7
}

TEST(FixedPoint, Determinism) {
    // The same operations must produce identical results every time
    // This is the foundation of the replay system
    for (int i = 0; i < 100; ++i) {
        fp32 a = int_to_fp(i * 7 + 3);
        fp32 b = int_to_fp(i * 11 + 5);
        fp32 result = fp_mul(a, b);
        fp32 result2 = fp_mul(a, b);
        EXPECT_EQ(result, result2);
    }
}

TEST(FixedPoint, SinCos) {
    // sin(0) = 0, cos(0) = 1
    EXPECT_NEAR(fp_to_float(fp_sin(PFP_ZERO)), 0.0f, 0.02f);
    EXPECT_NEAR(fp_to_float(fp_cos(PFP_ZERO)), 1.0f, 0.02f);

    // sin(pi/2) = 1, cos(pi/2) = 0
    fp32 half_pi = PFP_PI >> 1;
    EXPECT_NEAR(fp_to_float(fp_sin(half_pi)), 1.0f, 0.02f);
    EXPECT_NEAR(fp_to_float(fp_cos(half_pi)), 0.0f, 0.02f);
}
