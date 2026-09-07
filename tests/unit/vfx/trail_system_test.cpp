#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/vfx/trail_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::engine::vfx::TrailRenderer;
using mosaic::engine::vfx::TrailSystem;
using mosaic::foundation::math::Vec3;

namespace
{
void set_pos(World& world, mosaic::foundation::EntityID e, float x)
{
    world.get_component<Transform>(e)->position = Vec3{x, 0.0f, 0.0f};
}
} // namespace

TEST_CASE("TrailSystem samples one point per interval and tracks the newest position", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);
    TrailRenderer tr; tr.sample_interval = 0.1f; tr.max_points = 10;
    world.add_component<TrailRenderer>(e, tr);

    world.tick(0.1f);                 // first sight: seed head at x=0
    set_pos(world, e, 1.0f); world.tick(0.1f); // sample x=1
    set_pos(world, e, 2.0f); world.tick(0.1f); // sample x=2

    const auto* pts = sys.points_for(e);
    REQUIRE(pts != nullptr);
    REQUIRE(pts->size() == 3);
    REQUIRE(pts->front().x == Approx(0.0f)); // oldest
    REQUIRE(pts->back().x == Approx(2.0f));  // newest = current position
}

TEST_CASE("TrailSystem respects max_points and drops the oldest", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
    world.add_component<Transform>(e, tf);
    TrailRenderer tr; tr.sample_interval = 0.1f; tr.max_points = 2;
    world.add_component<TrailRenderer>(e, tr);

    world.tick(0.1f);                                   // seed x=0 -> [0]
    set_pos(world, e, 1.0f); world.tick(0.1f);          // [0,1]
    set_pos(world, e, 2.0f); world.tick(0.1f);          // [0,1,2] -> trim -> [1,2]
    set_pos(world, e, 3.0f); world.tick(0.1f);          // [1,2,3] -> trim -> [2,3]

    const auto* pts = sys.points_for(e);
    REQUIRE(pts->size() == 2);
    REQUIRE(pts->front().x == Approx(2.0f));
    REQUIRE(pts->back().x == Approx(3.0f));
}

TEST_CASE("TrailSystem drops history when the entity is destroyed", "[vfx]")
{
    World world;
    auto& sys = world.add_system<TrailSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<TrailRenderer>(e, TrailRenderer{});

    world.tick(0.1f);
    REQUIRE(sys.points_for(e) != nullptr);
    world.destroy_entity(e);
    world.tick(0.1f); // still seen this tick
    world.tick(0.1f); // now pruned
    REQUIRE(sys.points_for(e) == nullptr);
}

TEST_CASE("TrailSystem history is deterministic across runs", "[vfx]")
{
    auto run = []
    {
        World world;
        auto& sys = world.add_system<TrailSystem>();
        const auto e = world.create_entity();
        Transform tf; tf.position = Vec3{0.0f, 0.0f, 0.0f};
        world.add_component<Transform>(e, tf);
        TrailRenderer tr; tr.sample_interval = 0.05f; tr.max_points = 8;
        world.add_component<TrailRenderer>(e, tr);
        for (int i = 1; i <= 6; ++i) { set_pos(world, e, static_cast<float>(i)); world.tick(0.05f); }
        return *sys.points_for(e);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) { REQUIRE(a[i].x == b[i].x); }
}

TEST_CASE("TrailRenderer round-trips through reflection", "[vfx]")
{
    TrailRenderer t;
    t.color           = Vec3{0.2f, 0.4f, 0.6f};
    t.size            = 0.25f;
    t.max_points      = 48;
    t.sample_interval = 0.02f;

    const auto value = mosaic::foundation::serialization::to_value(t);
    TrailRenderer back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.color.x == Approx(0.2f));
    REQUIRE(back.size == Approx(0.25f));
    REQUIRE(back.max_points == 48);
    REQUIRE(back.sample_interval == Approx(0.02f));
}

TEST_CASE("TrailRenderer is a registered built-in component", "[vfx]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("TrailRenderer"));
}
