#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/run/demo_scene.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/particles/particle_emitter.hpp"
#include "engine/particles/particle_system.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/physics/physics_system.hpp"
#include "engine/script/script_source.hpp"
#include <cmath>
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

TEST_CASE("a script drives particle emission via burst_trigger", "[app]")
{
    using mosaic::engine::particles::ParticleEmitter;
    using mosaic::engine::particles::ParticleSystem;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    ParticleEmitter em;
    em.emitting = false; // burst-only, so growth comes solely from the script's triggers
    em.burst    = 4;
    em.lifetime = 100.0f; // long, so nothing ages out during the test
    world.add_component<ParticleEmitter>(e, em);
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    c = get_component(entity, 'ParticleEmitter')\n"
        "    c['burst_trigger'] += 1\n"
        "    set_component(entity, 'ParticleEmitter', c)\n"});

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });
    auto* psys = world.get_system<ParticleSystem>();
    REQUIRE(psys != nullptr);

    // ScriptSystem runs before ParticleSystem. On the first tick the script bumps
    // burst_trigger to 1 before the emitter is first seen, so the first-sight sync
    // absorbs it and only the on-sight burst spawns (4).
    world.tick(0.1f);
    REQUIRE(psys->particles_for(e)->size() == 4);

    // Each later tick the script's bump differs from the last acted value, re-arming
    // exactly one burst (+4) per tick.
    world.tick(0.1f);
    world.tick(0.1f);
    REQUIRE(psys->particles_for(e)->size() == 12);
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

TEST_CASE("physics contacts are bridged to scripts as 'contact' events", "[app]")
{
    using mosaic::engine::physics::Collider;
    using mosaic::engine::physics::RigidBody;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    // A static ground and a dynamic box that falls onto it (a begin-touch contact).
    const auto ground = world.create_entity();
    world.add_component<Transform>(
        ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto box = world.create_entity();
    world.add_component<Transform>(box, Transform{Vec3{0, 3, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(box, RigidBody{true, 1.0f});
    world.add_component<Collider>(box, Collider{Vec3{0.5f, 0.5f, 0.5f}});

    // A script-only probe (no physics) that counts begin-contacts via the on_event hook.
    const auto probe = world.create_entity();
    world.add_component<Transform>(probe, Transform{});
    world.add_component<ScriptComponent>(probe, ScriptComponent{
        "def on_update(dt):\n"
        "    pass\n"
        "def on_event(name, payload):\n"
        "    if name == 'contact' and payload['phase'] == 'begin':\n"
        "        t = get_component(entity, 'Transform')\n"
        "        t['position']['x'] += 1.0\n"
        "        set_component(entity, 'Transform', t)\n"});

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 180; ++i) { world.tick(1.0f / 60.0f); }
    // The box landing produced at least one begin-contact, bridged to the probe's on_event.
    REQUIRE(world.get_component<Transform>(probe)->position.x >= 1.0f);
}

TEST_CASE("make_physics_apply_impulse resolves the world's PhysicsSystem", "[app]")
{
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::RigidBody;
    using mosaic::engine::physics::SphereCollider;
    using mosaic::engine::physics::WorldDef;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;
    using mosaic::physics::box3d::create_box3d_backend;

    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto ball = world.create_entity();
    world.add_component<Transform>(
        ball, Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    world.tick(1.0f / 60.0f); // create the backend body + handle->entity map

    auto push = make_physics_apply_impulse(world);
    // Applied at the bottom of the ball, off the centre of mass, so it spins too.
    REQUIRE(push(ball.value(), Vec3{2.0f, 0.0f, 0.0f}, Vec3{0.0f, 4.5f, 0.0f}));

    const Quat before = world.get_component<Transform>(ball)->rotation;
    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }
    const auto* after = world.get_component<Transform>(ball);
    REQUIRE(after != nullptr);
    REQUIRE(after->position.x > 0.05f); // it moved along the push

    // Review Focus 6 from the spec: off-centre means it SPINS, not just translates.
    const Quat r = after->rotation;
    const float dq = std::fabs(r.x - before.x) + std::fabs(r.y - before.y)
                   + std::fabs(r.z - before.z) + std::fabs(r.w - before.w);
    REQUIRE(dq > 0.01f);
}

TEST_CASE("make_physics_apply_impulse returns false for an entity with no body", "[app]")
{
    // Review Focus 3: a destroyed or never-simulated entity must report failure
    // rather than silently claiming the push landed.
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::WorldDef;
    using mosaic::foundation::math::Vec3;
    using mosaic::physics::box3d::create_box3d_backend;

    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto bare = world.create_entity(); // no RigidBody, no collider
    world.tick(1.0f / 60.0f);

    auto push = make_physics_apply_impulse(world);
    REQUIRE_FALSE(push(bare.value(), Vec3{1, 0, 0}, Vec3{0, 0, 0}));
    REQUIRE_FALSE(push(999999u, Vec3{1, 0, 0}, Vec3{0, 0, 0})); // never existed
}

TEST_CASE("make_physics_apply_impulse returns false with no physics system", "[app]")
{
    // Review Focus 4: scripts also run under the MCP server and asset importers,
    // which have no PhysicsSystem at all.
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::foundation::math::Vec3;

    World world; // deliberately no PhysicsSystem
    auto  push = make_physics_apply_impulse(world);
    REQUIRE_FALSE(push(1u, Vec3{1, 0, 0}, Vec3{0, 0, 0}));
}
