#include <gtest/gtest.h>
#include "pebble/core/memory.h"

using namespace pebble;

TEST(LinearAllocator, BasicAlloc) {
    LinearAllocator alloc;
    alloc.init(1024);

    void* p1 = alloc.alloc(64);
    ASSERT_NE(p1, nullptr);
    EXPECT_GE(alloc.used(), 64u);

    void* p2 = alloc.alloc(128);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(LinearAllocator, Reset) {
    LinearAllocator alloc;
    alloc.init(1024);

    alloc.alloc(512);
    EXPECT_GE(alloc.used(), 512u);

    alloc.reset();
    EXPECT_EQ(alloc.used(), 0u);

    // Can allocate again after reset
    void* p = alloc.alloc(256);
    ASSERT_NE(p, nullptr);
}

TEST(LinearAllocator, Alignment) {
    LinearAllocator alloc;
    alloc.init(4096);

    // Allocate odd size, then aligned
    alloc.alloc(7, 1);
    void* p = alloc.alloc(64, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0u);

    void* p2 = alloc.alloc(64, 64);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0u);
}

struct TestObj {
    uint16_t id;
    int32_t  value;
    bool     flag;
};

TEST(PoolAllocator, AllocFree) {
    PoolAllocator<TestObj, 8> pool;

    TestObj* a = pool.alloc();
    ASSERT_NE(a, nullptr);
    a->id = 1;
    a->value = 42;
    EXPECT_EQ(pool.active_count(), 1u);

    TestObj* b = pool.alloc();
    ASSERT_NE(b, nullptr);
    b->id = 2;
    EXPECT_EQ(pool.active_count(), 2u);

    pool.free(a);
    EXPECT_EQ(pool.active_count(), 1u);

    // Freed slot can be reused
    TestObj* c = pool.alloc();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(pool.active_count(), 2u);
}

TEST(PoolAllocator, ExhaustPool) {
    PoolAllocator<TestObj, 4> pool;

    TestObj* ptrs[4];
    for (int i = 0; i < 4; ++i) {
        ptrs[i] = pool.alloc();
        ASSERT_NE(ptrs[i], nullptr);
    }

    // Pool is full
    EXPECT_EQ(pool.alloc(), nullptr);

    // Free one, then allocate again
    pool.free(ptrs[2]);
    TestObj* p = pool.alloc();
    ASSERT_NE(p, nullptr);
}

TEST(PoolAllocator, ForEach) {
    PoolAllocator<TestObj, 8> pool;

    auto* a = pool.alloc(); a->value = 10;
    auto* b = pool.alloc(); b->value = 20;
    auto* c = pool.alloc(); c->value = 30;
    pool.free(b); // Hole in the middle

    int sum = 0;
    int count = 0;
    pool.for_each([&](const TestObj& obj, uint16_t) {
        sum += obj.value;
        count++;
    });

    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 40); // 10 + 30
}

TEST(PoolAllocator, IndexOf) {
    PoolAllocator<TestObj, 8> pool;

    auto* a = pool.alloc();
    auto* b = pool.alloc();

    uint16_t idx_a = pool.index_of(a);
    uint16_t idx_b = pool.index_of(b);

    EXPECT_NE(idx_a, idx_b);
    EXPECT_EQ(pool.at(idx_a), a);
    EXPECT_EQ(pool.at(idx_b), b);
}

TEST(PoolAllocator, Clear) {
    PoolAllocator<TestObj, 4> pool;

    pool.alloc();
    pool.alloc();
    EXPECT_EQ(pool.active_count(), 2u);

    pool.clear();
    EXPECT_EQ(pool.active_count(), 0u);

    // Can allocate all slots again
    for (int i = 0; i < 4; ++i) {
        EXPECT_NE(pool.alloc(), nullptr);
    }
}

TEST(RingBuffer, BasicMap) {
    RingBuffer ring;
    ring.init(1024);

    void* p1 = ring.map(64);
    ASSERT_NE(p1, nullptr);

    void* p2 = ring.map(128);
    ASSERT_NE(p2, nullptr);
    EXPECT_NE(p1, p2);
}

TEST(RingBuffer, Reset) {
    RingBuffer ring;
    ring.init(256);

    ring.map(200);
    EXPECT_GE(ring.used(), 200u);

    ring.reset();
    EXPECT_EQ(ring.used(), 0u);
}
