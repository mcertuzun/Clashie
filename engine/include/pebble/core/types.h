#pragma once

#include <cstdint>
#include <cstddef>

namespace pebble {

// Opaque handle types (zero = invalid)
using EntityID      = uint32_t;
using TextureID     = uint32_t;
using ShaderID      = uint32_t;
using BufferID      = uint32_t;
using PipelineID    = uint32_t;
using WindowHandle  = uintptr_t;
using GfxContext     = uintptr_t;

constexpr EntityID INVALID_ENTITY = 0;

} // namespace pebble
