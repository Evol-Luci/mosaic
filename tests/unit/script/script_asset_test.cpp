#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_component.hpp"
#include "engine/script/script_source.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::script::ScriptComponent;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("ScriptComponent round-trips its script AssetID through reflection", "[script]")
{
    ScriptComponent c;
    c.source = "inline";
    c.script = AssetID::from_string("00000000-0000-0000-0000-0000000000aa");

    const auto value = mosaic::foundation::serialization::to_value(c);
    ScriptComponent back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.source == "inline");
    REQUIRE(back.script == c.script);
    REQUIRE_FALSE(back.script.is_nil());
}
