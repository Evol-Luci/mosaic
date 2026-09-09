#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::World;
using mosaic::engine::ui::UiParent;

TEST_CASE("UiParent round-trips through reflection", "[ui][reflection]")
{
    World w;
    const auto e = w.create_entity();
    UiParent up; up.parent = e;
    const auto value = mosaic::foundation::serialization::to_value(up);
    UiParent back;
    mosaic::foundation::serialization::from_value(value, back);
    REQUIRE(back.parent == e);
}

TEST_CASE("UiParent is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiParent"));
}
