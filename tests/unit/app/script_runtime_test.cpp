#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/script/script_source.hpp"
#include <memory>
#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::setup_runtime;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("setup_runtime wires ScriptSystem so scripts tick in the runtime", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"});

    AssetRegistry registry;
    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); } // 0.25 * 4, binary-exact
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}

TEST_CASE("setup_runtime lets a scene reference a script by AssetID", "[app]")
{
    using mosaic::engine::assets::AssetMetadata;
    using mosaic::engine::script::ScriptSource;
    using mosaic::foundation::serialization::AssetID;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    const AssetID id = AssetID::from_string("00000000-0000-0000-0000-0000000000cc");
    AssetMetadata meta; meta.uuid = id; meta.type = "script"; meta.version = 1;
    registry.register_resource<ScriptSource>(meta, std::make_shared<ScriptSource>(ScriptSource{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"}));

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    ScriptComponent sc; sc.script = id;
    world.add_component<ScriptComponent>(e, sc);

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}

TEST_CASE("make_physics_raycast resolves the world's PhysicsSystem", "[app]")
{
    using mosaic::app::make_physics_raycast;
    using mosaic::engine::physics::Collider;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::RigidBody;
    using mosaic::engine::physics::WorldDef;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;
    using mosaic::physics::box3d::create_box3d_backend;

    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto ground = world.create_entity();
    world.add_component<Transform>(
        ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}}); // top y=0.5
    world.tick(1.0f / 60.0f); // create the backend body + handle->entity map

    auto rc = make_physics_raycast(world);
    const auto h = rc(Vec3{0, 5, 0}, Vec3{0, -1, 0}, 10.0f);
    REQUIRE(h.hit);
    REQUIRE(h.entity == ground.value());
    REQUIRE(h.distance == Catch::Approx(4.5f).margin(0.05f));
    REQUIRE_FALSE(rc(Vec3{100, 5, 0}, Vec3{0, -1, 0}, 10.0f).hit);
}

TEST_CASE("a script can raycast the physics world via setup_runtime", "[app]")
{
    using mosaic::engine::physics::Collider;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::RigidBody;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    // A static box the ray will hit (top at y=0.5).
    const auto ground = world.create_entity();
    world.add_component<Transform>(
        ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    // A script-only probe that raycasts straight down and records the hit distance in its own x.
    const auto probe = world.create_entity();
    world.add_component<Transform>(probe, Transform{});
    world.add_component<ScriptComponent>(probe, ScriptComponent{
        "def on_update(dt):\n"
        "    r = raycast([0.0, 5.0, 0.0], [0.0, -1.0, 0.0], 10.0)\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = r['distance'] if r['hit'] else -1.0\n"
        "    set_component(entity, 'Transform', t)\n"});

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    // Scripts run before physics, so the box's backend body exists from tick 2 on.
    for (int i = 0; i < 3; ++i) { world.tick(1.0f / 60.0f); }
    REQUIRE(world.get_component<Transform>(probe)->position.x == Catch::Approx(4.5f).margin(0.05f));
}
