#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"

using Catch::Approx;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::BoxShape;
using mosaic::engine::physics::WorldDef;

TEST_CASE("physics value-type defaults", "[physics]")
{
    WorldDef world;
    REQUIRE(world.gravity.y == Approx(-9.81f));
    REQUIRE(world.gravity.x == Approx(0.0f));

    BodyDef body;
    REQUIRE(body.type == BodyType::Dynamic);
    REQUIRE(body.density == Approx(1.0f));

    BoxShape shape;
    REQUIRE(shape.half_extents.x == Approx(0.5f));
    REQUIRE(shape.half_extents.z == Approx(0.5f));
}
