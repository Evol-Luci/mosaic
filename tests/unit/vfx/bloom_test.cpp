#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/vfx/bloom.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::vfx::Bloom;

TEST_CASE("Bloom defaults are off", "[vfx][bloom]")
{
    // The spec's load-bearing defaults. intensity 0 means adding the component to a
    // scene changes nothing until someone turns it up, so every existing scene renders
    // byte-identically whether or not it has one. threshold 1.0 is display white: out of
    // the box bloom picks up exactly the light the screen cannot show, which is the
    // headroom the HDR target was built to preserve.
    const Bloom b;
    REQUIRE(b.intensity == Approx(0.0f));
    REQUIRE(b.threshold == Approx(1.0f));
    REQUIRE(b.knee == Approx(0.5f));
}

TEST_CASE("Bloom is a registered built-in component", "[vfx][bloom]")
{
    // Registration is what buys serialisation, the Studio inspector and scripting, with
    // no further code -- the same three things ScreenEffect gets. An unregistered
    // component is authorable only from C++, which is not what "authored per-camera"
    // means.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("Bloom"));
}

TEST_CASE("Bloom round-trips through reflection", "[vfx][bloom]")
{
    // Pins the member NAMES, not just their presence: a scene file written today has to
    // load tomorrow, and a rename is silent -- from_value simply leaves the field at its
    // default. Non-default values on all three, and three DISTINCT ones, so a field read
    // from the wrong member cannot pass.
    Bloom b;
    b.intensity = 2.5f;
    b.threshold = 1.75f;
    b.knee      = 0.25f;

    const auto value = mosaic::foundation::serialization::to_value(b);
    Bloom      back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.intensity == Approx(2.5f));
    REQUIRE(back.threshold == Approx(1.75f));
    REQUIRE(back.knee == Approx(0.25f));
}
