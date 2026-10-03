// Backend-level destruction. The gameplay consequence -- a destroyed entity stops
// blocking things -- is tested at the PhysicsSystem level in physics_system_test.cpp;
// these pin the backend contract the system relies on.

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/physics/physics_backend.hpp"
#include "modules/physics_box3d/box3d_backend.hpp"

using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyHandle;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::WorldDef;
using mosaic::foundation::math::Vec3;
using mosaic::physics::box3d::create_box3d_backend;

namespace
{
// A static floor spanning the origin, and a dynamic box resting above it.
BodyHandle make_ground(mosaic::engine::physics::PhysicsBackend& backend)
{
    BodyDef ground;
    ground.type = BodyType::Static;
    ground.position = Vec3{0, 0, 0};
    ground.shape.half_extents = Vec3{5, 0.5f, 5};
    return backend.create_body(ground);
}

BodyHandle make_box(mosaic::engine::physics::PhysicsBackend& backend, float y)
{
    BodyDef box;
    box.type = BodyType::Dynamic;
    box.position = Vec3{0, y, 0};
    box.shape.half_extents = Vec3{0.5f, 0.5f, 0.5f};
    return backend.create_body(box);
}
} // namespace

TEST_CASE("destroy_body makes the other handle-taking methods inert", "[physics]")
{
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle box = make_box(*backend, 3.0f);

    backend->step(1.0f / 60.0f, 4);
    REQUIRE(backend->body_pose(box).position.y < 3.0f); // it was simulating

    backend->destroy_body(box);

    // A destroyed handle reads as the default pose rather than indexing a freed body.
    REQUIRE(backend->body_pose(box).position.y == 0.0f);
    // And the mutating calls do nothing instead of corrupting the world.
    REQUIRE_NOTHROW(backend->set_body_transform(box, Vec3{9, 9, 9}, mosaic::foundation::math::Quat{}));
    REQUIRE_NOTHROW(backend->apply_impulse(box, Vec3{100, 0, 0}, Vec3{0, 0, 0}));
    REQUIRE_NOTHROW(backend->step(1.0f / 60.0f, 4));
    REQUIRE(backend->body_pose(box).position.y == 0.0f);
}

TEST_CASE("destroy_body is a no-op for an unknown or already-destroyed handle", "[physics]")
{
    // Review Focus 1: the reap erases its map entries so it cannot fire twice for one
    // entity, but a caller holding a stale handle can -- and b3DestroyBody on a freed id
    // is undefined. The tombstone plus the liveness guard is what makes this safe, and
    // nothing else in the suite would catch its absence.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle box = make_box(*backend, 3.0f);

    REQUIRE_NOTHROW(backend->destroy_body(0));        // the reserved invalid handle
    REQUIRE_NOTHROW(backend->destroy_body(box + 99)); // never issued
    REQUIRE_NOTHROW(backend->destroy_body(box));
    REQUIRE_NOTHROW(backend->destroy_body(box)); // again: must not double-free
    REQUIRE_NOTHROW(backend->step(1.0f / 60.0f, 4));
}

TEST_CASE("a destroyed body no longer stops a raycast", "[physics]")
{
    // Review Focus 2: the handle lives in Box3D's per-body user data, so a stale read
    // would hand back a plausible-looking handle for a body that no longer exists --
    // worse than a miss, because a caller would map it to an entity.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle ground = make_ground(*backend);
    const BodyHandle box = make_box(*backend, 2.0f);

    for (int i = 0; i < 120; ++i) { backend->step(1.0f / 60.0f, 4); } // box settles on ground

    const auto before = backend->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f);
    REQUIRE(before.hit);
    REQUIRE(before.body == box); // the box is what the ray meets first

    backend->destroy_body(box);
    backend->step(1.0f / 60.0f, 4);

    const auto after = backend->raycast(Vec3{0, 10, 0}, Vec3{0, -1, 0}, 20.0f);
    REQUIRE(after.hit);
    REQUIRE(after.body == ground); // now the ground, never the destroyed box
}

TEST_CASE("destroying one body leaves the handles of other bodies valid", "[physics]")
{
    // Handles are vector indices (handle == index + 1), so erasing a slot would shift
    // every later handle. Tombstoning is what keeps `high` addressable after `low` dies.
    auto backend = create_box3d_backend(WorldDef{});
    const BodyHandle low = make_box(*backend, 2.0f);
    const BodyHandle high = make_box(*backend, 6.0f);
    REQUIRE(low != high);

    backend->step(1.0f / 60.0f, 4);
    const float high_y_before = backend->body_pose(high).position.y;

    backend->destroy_body(low);
    for (int i = 0; i < 30; ++i) { backend->step(1.0f / 60.0f, 4); }

    // `high` still resolves to its own body and is still falling.
    REQUIRE(backend->body_pose(high).position.y < high_y_before);
    REQUIRE(backend->body_pose(low).position.y == 0.0f); // and `low` is gone
}
