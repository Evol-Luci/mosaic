#pragma once

#include <cstdint>

#include "engine/ecs/entity_registry.hpp"

namespace mosaic::engine::ecs::detail
{

// Test-only seam: drives a slot's stored generation directly so the
// generation-exhaustion path can be exercised without 4 billion allocations.
struct RegistryTestAccess
{
    static void set_generation(EntityRegistry& registry,
                               std::uint32_t index,
                               std::uint32_t generation)
    {
        registry.generations_[index] = generation;
    }
};

} // namespace mosaic::engine::ecs::detail
