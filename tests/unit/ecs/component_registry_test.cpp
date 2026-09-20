#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_registry.hpp"
#include "engine/ecs/component_type_id.hpp"
#include "engine/ecs/entity_registry.hpp"
#include "foundation/reflection/type_id.hpp"

using mosaic::engine::ecs::ComponentRegistry;
using mosaic::engine::ecs::EntityRegistry;
using mosaic::engine::ecs::component_type_id;
using mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };
struct Velocity { float dx, dy; };
struct DenseProbeA { int x; };
struct DenseProbeB { int x; };
struct NotStoredAsAComponent { int x; }; // only ever type_id<T>()'d, never stored
} // namespace

TEST_CASE("component_type_id is stable per type and distinct across types", "[components]")
{
    REQUIRE(component_type_id<Position>() == component_type_id<Position>());
    REQUIRE(component_type_id<Position>() != component_type_id<Velocity>());
}

TEST_CASE("issue #25: storage slots stay dense across an unrelated type_id<T>() call", "[components]")
{
    // ComponentRegistry indexes its own storages_ vector by a private dense slot
    // (component_registry.hpp's detail::component_storage_slot<T>()), NOT by
    // ComponentTypeID/type_id<T>() directly -- so a non-component reflected type
    // consuming a TypeID in between two component types being stored must not
    // leave a gap in ComponentRegistry's own slot numbering. Before this fix,
    // storages_ was indexed by the raw, shared type_id<T>() value, so this same
    // sequence would have left a one-slot gap (DenseProbeB's slot would have been
    // slot_a + 2, not slot_a + 1).
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();

    components.add<DenseProbeA>(e, DenseProbeA{1});
    const std::size_t slot_a = mosaic::engine::ecs::detail::component_storage_slot<DenseProbeA>();

    // A type that only ever goes through the reflection system's own TypeID
    // counter, never through any ComponentRegistry storage call -- exactly the
    // "Vec3, Quat, and every other non-component reflected struct" scenario the
    // issue describes.
    (void)mosaic::foundation::reflection::type_id<NotStoredAsAComponent>();

    components.add<DenseProbeB>(e, DenseProbeB{2});
    const std::size_t slot_b = mosaic::engine::ecs::detail::component_storage_slot<DenseProbeB>();

    REQUIRE(slot_b == slot_a + 1);
}

TEST_CASE("add then get returns the component", "[components]")
{
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();

    REQUIRE(components.add<Position>(e, Position{1.0f, 2.0f}));
    REQUIRE(components.has<Position>(e));
    Position* p = components.get<Position>(e);
    REQUIRE(p != nullptr);
    REQUIRE(p->x == 1.0f);
    REQUIRE(p->y == 2.0f);
}

TEST_CASE("distinct component types are stored independently", "[components]")
{
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();

    components.add<Position>(e, Position{1.0f, 2.0f});
    REQUIRE(components.has<Position>(e));
    REQUIRE_FALSE(components.has<Velocity>(e));
}

TEST_CASE("remove deletes a single component type", "[components]")
{
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();

    components.add<Position>(e, Position{1.0f, 2.0f});
    REQUIRE(components.remove<Position>(e));
    REQUIRE_FALSE(components.has<Position>(e));
    REQUIRE(components.get<Position>(e) == nullptr);
    REQUIRE_FALSE(components.remove<Position>(e)); // absent is a no-op
}

TEST_CASE("operations on an invalid entity are rejected", "[components]")
{
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();
    components.add<Position>(e, Position{1.0f, 2.0f});
    // on_destroy deliberately omitted to isolate validity rejection; stale component data intentionally remains.
    entities.destroy(e);

    REQUIRE_FALSE(components.has<Position>(e));
    REQUIRE(components.get<Position>(e) == nullptr);
    REQUIRE_FALSE(components.add<Position>(e, Position{3.0f, 4.0f}));
    REQUIRE_FALSE(components.remove<Position>(e));
}

TEST_CASE("on_destroy erases the entity from all storages", "[components]")
{
    EntityRegistry entities;
    ComponentRegistry components{entities};
    EntityID e = entities.create();
    components.add<Position>(e, Position{1.0f, 2.0f});
    components.add<Velocity>(e, Velocity{0.5f, 0.5f});

    components.on_destroy(e); // called before the registry destroy
    entities.destroy(e);

    // Recreate the same slot; the new entity inherits nothing.
    EntityID reused = entities.create();
    REQUIRE(reused.index() == e.index());
    REQUIRE_FALSE(components.has<Position>(reused));
    REQUIRE_FALSE(components.has<Velocity>(reused));
}
