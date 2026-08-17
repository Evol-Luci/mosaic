#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/runtime/runtime_loop.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Vec3;
using EntityID = World::EntityID;

TEST_CASE("populated World is stable over many ticks", "[runtime]")
{
    World world;
    world.add_system<TransformSystem>();

    const EntityID parent = world.create_entity();
    Transform pt;
    pt.position = Vec3{1.0f, 0.0f, 0.0f};
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{2.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    RuntimeLoop loop(0.016f);
    loop.start();
    for (int frame = 0; frame < 1000; ++frame)
    {
        loop.advance(world, 0.016f);
    }

    REQUIRE(loop.tick_count() >= 900);      // ~1000 ticks; tolerant of float drift
    REQUIRE(world.entity_count() == 2);     // graph intact
    REQUIRE(world.is_valid(parent));
    REQUIRE(world.is_valid(child));

    const WorldTransform* cw = world.get_component<WorldTransform>(child);
    REQUIRE(cw != nullptr);
    const Vec3 p = transform_point(cw->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(3.0f)); // parent(1) composed with child(2)
}
