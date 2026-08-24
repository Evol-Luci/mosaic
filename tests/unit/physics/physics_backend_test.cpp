#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/physics/physics_backend.hpp"

using Catch::Approx;
using mosaic::engine::physics::BodyDef;
using mosaic::engine::physics::BodyType;
using mosaic::engine::physics::Shape;
using mosaic::engine::physics::ShapeKind;
using mosaic::engine::physics::WorldDef;

TEST_CASE("physics value-type defaults", "[physics]")
{
    WorldDef world;
    REQUIRE(world.gravity.y == Approx(-9.81f));
    REQUIRE(world.gravity.x == Approx(0.0f));

    BodyDef body;
    REQUIRE(body.type == BodyType::Dynamic);
    REQUIRE(body.density == Approx(1.0f));

    Shape shape;
    REQUIRE(shape.kind == ShapeKind::Box);
    REQUIRE(shape.radius == Approx(0.5f));
    REQUIRE(shape.half_height == Approx(0.5f));
    REQUIRE(shape.half_extents.x == Approx(0.5f));
    REQUIRE(shape.half_extents.z == Approx(0.5f));
}
