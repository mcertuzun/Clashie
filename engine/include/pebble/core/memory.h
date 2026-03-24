#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>

namespace pebble {

// Linear allocator — reset every frame, O(1) "free all"
class LinearAllocator {
public:
    LinearAllocator() = default;
    ~LinearAllocator();

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;

    void init(size_t capacity);
    void* alloc(size_t size, size_t alignment = 16);
    void reset() { m_offset = 0; }

    size_t used() const { return m_offset; }
    size_t capacity() const { return m_capacity; }

private:
    uint8_t* m_base     = nullptr;
    size_t   m_offset   = 0;
    size_t   m_capacity = 0;
    bool     m_owns_memory = false;
};

// Pool allocator — fixed-size objects, O(1) alloc/free via free-list
template<typename T, size_t MaxCount>
class PoolAllocator {
public:
    PoolAllocator() { clear(); }

    void clear() {
        m_active_count = 0;
        m_free_count = MaxCount;
        for (size_t i = 0; i < MaxCount; ++i) {
            m_free_list[i] = static_cast<uint16_t>(i);
            m_active[i] = false;
        }
    }

    T* alloc() {
        if (m_free_count == 0) return nullptr;
        uint16_t idx = m_free_list[--m_free_count];
        m_active[idx] = true;
        m_active_indices[m_active_count] = idx;
        ++m_active_count;
        T* ptr = &m_storage[idx];
        std::memset(ptr, 0, sizeof(T));
        return ptr;
    }

    void free(T* ptr) {
        assert(ptr >= m_storage && ptr < m_storage + MaxCount);
        size_t idx = static_cast<size_t>(ptr - m_storage);
        assert(m_active[idx]);
        m_active[idx] = false;
        m_free_list[m_free_count++] = static_cast<uint16_t>(idx);
        // Swap-remove from compact active indices
        for (size_t i = 0; i < m_active_count; ++i) {
            if (m_active_indices[i] == static_cast<uint16_t>(idx)) {
                m_active_indices[i] = m_active_indices[m_active_count - 1];
                break;
            }
        }
        --m_active_count;
    }

    // Index of a pointer in the pool
    uint16_t index_of(const T* ptr) const {
        assert(ptr >= m_storage && ptr < m_storage + MaxCount);
        return static_cast<uint16_t>(ptr - m_storage);
    }

    T* at(uint16_t idx) {
        assert(idx < MaxCount && m_active[idx]);
        return &m_storage[idx];
    }

    const T* at(uint16_t idx) const {
        assert(idx < MaxCount && m_active[idx]);
        return &m_storage[idx];
    }

    bool is_active(uint16_t idx) const { return idx < MaxCount && m_active[idx]; }
    size_t active_count() const { return m_active_count; }
    static constexpr size_t max_count() { return MaxCount; }

    // Iterate over active elements (uses compact index list)
    template<typename Func>
    void for_each(Func&& func) {
        for (size_t i = 0; i < m_active_count; ++i) {
            uint16_t idx = m_active_indices[i];
            func(m_storage[idx], idx);
        }
    }

    template<typename Func>
    void for_each(Func&& func) const {
        for (size_t i = 0; i < m_active_count; ++i) {
            uint16_t idx = m_active_indices[i];
            func(m_storage[idx], idx);
        }
    }

private:
    T        m_storage[MaxCount];
    uint16_t m_free_list[MaxCount];
    bool     m_active[MaxCount];
    uint16_t m_active_indices[MaxCount];
    size_t   m_free_count = MaxCount;
    size_t   m_active_count = 0;
};

// Ring buffer — dynamic vertex data (map/orphan pattern for GPU uploads)
class RingBuffer {
public:
    RingBuffer() = default;
    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    void init(size_t capacity);
    void* map(size_t size, size_t alignment = 16);
    void reset() { m_write_offset = 0; }

    size_t used() const { return m_write_offset; }
    size_t capacity() const { return m_capacity; }

private:
    uint8_t* m_base         = nullptr;
    size_t   m_capacity     = 0;
    size_t   m_write_offset = 0;
};

} // namespace pebble
