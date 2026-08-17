#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_type_id.hpp"
#include "foundation/reflection/type_id.hpp"

using mosaic::foundation::reflection::type_id;
using mosaic::foundation::reflection::TypeID;

namespace
{
struct Alpha {};
struct Beta {};
} // namespace

TEST_CASE("type_id is stable within a run and distinct across types", "[reflection]")
{
    const TypeID a1 = type_id<Alpha>();
    const TypeID a2 = type_id<Alpha>();
    const TypeID b = type_id<Beta>();

    REQUIRE(a1 == a2);
    REQUIRE(a1 != b);
}

TEST_CASE("engine component_type_id re-exports foundation type_id", "[reflection]")
{
    REQUIRE(mosaic::engine::ecs::component_type_id<Alpha>() == type_id<Alpha>());
    REQUIRE(mosaic::engine::ecs::component_type_id<Beta>() == type_id<Beta>());
    static_assert(std::is_same_v<mosaic::engine::ecs::ComponentTypeID, TypeID>,
                  "ComponentTypeID must alias foundation TypeID");
}
