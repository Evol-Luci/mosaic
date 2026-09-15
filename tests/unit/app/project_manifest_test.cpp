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

TEST_CASE("ProjectManifest round-trips installed_plugins through TOML", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";
    m.installed_plugins.push_back(
        mosaic::app::InstalledPlugin{"hello-plugin", "0.1.0", {"hello.py"}});

    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.installed_plugins.size() == 1);
    REQUIRE(back.installed_plugins[0].name == "hello-plugin");
    REQUIRE(back.installed_plugins[0].version == "0.1.0");
    REQUIRE(back.installed_plugins[0].files == std::vector<std::string>{"hello.py"});
}

TEST_CASE("ProjectManifest with no installed_plugins key deserializes to an empty vector", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";
    // installed_plugins left default-constructed (empty) -- simulates a project
    // manifest written before this feature existed.
    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.installed_plugins.empty());
}
