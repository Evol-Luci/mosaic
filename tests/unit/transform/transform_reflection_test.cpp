#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;
using EntityID = World::EntityID;

TEST_CASE("Transform reflects and generically constructs", "[transform]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Transform>(registry, "Transform");

    World world;
    const EntityID e = world.create_entity();

    Transform t;
    t.position = Vec3{1.0f, 2.0f, 3.0f};
    t.rotation = Quat{0.0f, 0.0f, 0.0f, 1.0f};
    t.scale = Vec3{2.0f, 2.0f, 2.0f};

    const Value value = to_value(t);
    REQUIRE(types.construct(world, e, "Transform", value));

    const Transform* got = world.get_component<Transform>(e);
    REQUIRE(got != nullptr);
    REQUIRE(got->position == Vec3{1.0f, 2.0f, 3.0f});
    REQUIRE(got->scale == Vec3{2.0f, 2.0f, 2.0f});
    REQUIRE(got->rotation.w == Approx(1.0f));
}
