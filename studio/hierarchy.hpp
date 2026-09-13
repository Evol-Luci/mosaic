#pragma once

#include <string>
#include <vector>

#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::studio
{

// One row in the Hierarchy panel: the entity and its display label.
struct HierarchyRow
{
    mosaic::foundation::EntityID id;
    std::string                  label; // the entity's Name, or "Entity <index>" if unnamed
};

// Flat list of every live entity in the world, in iteration order. Separates entity
// enumeration (unit-tested here) from the ImGui rendering that consumes it.
inline std::vector<HierarchyRow> build_hierarchy(mosaic::engine::World& world)
{
    std::vector<HierarchyRow> rows;
    world.for_each_entity(
        [&](mosaic::foundation::EntityID id)
        {
            const mosaic::engine::Name* name = world.get_component<mosaic::engine::Name>(id);
            std::string label = (name != nullptr) ? name->value : "Entity " + std::to_string(id.index());
            rows.push_back(HierarchyRow{id, std::move(label)});
        });
    return rows;
}

} // namespace mosaic::studio
