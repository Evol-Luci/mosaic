#pragma once

#include <string>
#include <vector>

#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::studio
{

// One row in the Hierarchy panel: the entity and its display label.
struct HierarchyRow
{
    mosaic::foundation::EntityID id;
    std::string                  label; // "Entity <index>"
};

// Flat list of every live entity in the world, in iteration order. Separates entity
// enumeration (unit-tested here) from the ImGui rendering that consumes it.
inline std::vector<HierarchyRow> build_hierarchy(const mosaic::engine::World& world)
{
    std::vector<HierarchyRow> rows;
    world.for_each_entity(
        [&](mosaic::foundation::EntityID id)
        {
            rows.push_back(HierarchyRow{id, "Entity " + std::to_string(id.index())});
        });
    return rows;
}

} // namespace mosaic::studio
