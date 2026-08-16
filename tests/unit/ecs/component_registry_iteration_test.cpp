#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_registry.hpp"
#include "engine/ecs/entity_registry.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::ecs::ComponentRegistry;
using mosaic::engine::ecs::EntityRegistry;
using mosaic::foundation::EntityID;

namespace
{
struct Health { int value; };
} // namespace

TEST_CASE("ComponentRegistry::for_each visits only entities holding the component", "[component_registry]")
{
    EntityRegistry entities;
    ComponentRegistry components(entities);

    const EntityID a = entities.create();
    const EntityID b = entities.create();
    const EntityID c = entities.create();

    components.add<Health>(a, Health{10});
    components.add<Health>(c, Health{30});
    // b intentionally has no Health.

    std::vector<int> seen;
    components.for_each<Health>([&](EntityID, Health& h) { seen.push_back(h.value); });

    REQUIRE(seen == std::vector<int>{10, 30});
}

TEST_CASE("ComponentRegistry::for_each on an unused component type is a no-op", "[component_registry]")
{
    EntityRegistry entities;
    ComponentRegistry components(entities);

    int calls = 0;
    components.for_each<Health>([&](EntityID, Health&) { ++calls; });
    REQUIRE(calls == 0);
}
