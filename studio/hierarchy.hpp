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
            // An empty Name (e.g. freshly added via the Inspector, before typing a value)
            // falls back too — an empty label reaching ImGui::Selectable with no enclosing
            // PushID crashes ("Cannot have an empty ID at the root of a window").
            std::string label = (name != nullptr && !name->value.empty())
                                     ? name->value
                                     : "Entity " + std::to_string(id.index());
            rows.push_back(HierarchyRow{id, std::move(label)});
        });
    return rows;
}

} // namespace mosaic::studio
