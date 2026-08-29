#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/project/project.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"

using mosaic::app::ProjectManifest;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;

TEST_CASE("ProjectManifest round-trips through TOML", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";

    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.name == "mygame");
    REQUIRE(back.default_scene == "scenes/main.json");
}
