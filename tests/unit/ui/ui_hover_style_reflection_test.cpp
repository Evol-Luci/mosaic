#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_hover_style.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiHoverStyle;
using mosaic::foundation::math::Vec3;

TEST_CASE("UiHoverStyle round-trips through reflection", "[ui][reflection]")
{
    UiHoverStyle s;
    // Non-default on purpose: a field left at its default round-trips whether or not it is
    // serialised at all. Asymmetric per channel so a transposed axis fails.
    s.tint = Vec3{1.5f, 0.5f, 2.0f};
    s.alpha_scale = 0.25f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    UiHoverStyle back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.tint.x == Approx(1.5f));
    REQUIRE(back.tint.y == Approx(0.5f));
    REQUIRE(back.tint.z == Approx(2.0f));
    REQUIRE(back.alpha_scale == Approx(0.25f));
}

TEST_CASE("UiHoverStyle is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiHoverStyle"));
}

TEST_CASE("UiHovered is deliberately NOT a registered component", "[ui][reflection]")
{
    // The absence is load-bearing, so it is asserted rather than left to be "fixed" by
    // someone adding the missing-looking registration line.
    //
    // ComponentReflection::save iterates every REGISTERED type, so registering UiHovered
    // would write a phantom hover into any scene saved while the cursor rested on a button.
    // Loading that scene would then start up already hovered, with no pointer near it -- and
    // the highlight would never clear, because no hover_exit is coming.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE_FALSE(types.has("UiHovered"));
}
