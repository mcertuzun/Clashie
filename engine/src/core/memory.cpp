#include "pebble/core/memory.h"
#include <cstdlib>
#include <cassert>

namespace pebble {

// --- LinearAllocator ---

LinearAllocator::~LinearAllocator() {
    if (m_owns_memory && m_base) {
        std::free(m_base);
    }
}

void LinearAllocator::init(size_t capacity) {
    assert(m_base == nullptr && "Already initialized");
    // Allocate with max alignment guarantee (64 bytes) so sub-allocations
    // requesting any power-of-two alignment up to 64 always succeed
    void* raw = nullptr;
    int err = posix_memalign(&raw, 64, capacity);
    assert(err == 0 && raw && "Failed to allocate linear allocator memory");
    (void)err;
    m_base = static_cast<uint8_t*>(raw);
    m_capacity = capacity;
    m_offset = 0;
    m_owns_memory = true;
}

void* LinearAllocator::alloc(size_t size, size_t alignment) {
    // Align offset
    size_t aligned = (m_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > m_capacity) {
        assert(false && "LinearAllocator out of memory");
        return nullptr;
    }
    void* ptr = m_base + aligned;
    m_offset = aligned + size;
    return ptr;
}

// --- RingBuffer ---

RingBuffer::~RingBuffer() {
    if (m_base) {
        std::free(m_base);
    }
}

void RingBuffer::init(size_t capacity) {
    assert(m_base == nullptr && "Already initialized");
    m_base = static_cast<uint8_t*>(std::malloc(capacity));
    assert(m_base && "Failed to allocate ring buffer memory");
    m_capacity = capacity;
    m_write_offset = 0;
}

void* RingBuffer::map(size_t size, size_t alignment) {
    size_t aligned = (m_write_offset + alignment - 1) & ~(alignment - 1);

    // Wrap around if not enough space at end
    if (aligned + size > m_capacity) {
        aligned = 0;
    }

    assert(aligned + size <= m_capacity && "RingBuffer: allocation too large");
    void* ptr = m_base + aligned;
    m_write_offset = aligned + size;
    return ptr;
}

} // namespace pebble
