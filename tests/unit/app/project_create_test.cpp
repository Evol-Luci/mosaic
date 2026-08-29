#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/project/project.hpp"
#include "app/project/project_io.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::load_manifest;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string temp_project_dir(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t3_") + tag + std::to_string(::getpid()))).string();
}
} // namespace

TEST_CASE("create_project scaffolds a manifest + starter scene + assets dir", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_project_dir("create");
    fs::remove_all(dir); // clean slate

    REQUIRE(create_project(dir, types));
    REQUIRE(fs::exists(fs::path(dir) / "mosaic.toml"));
    REQUIRE(fs::exists(fs::path(dir) / "scenes" / "main.json"));
    REQUIRE(fs::is_directory(fs::path(dir) / "assets"));

    // The manifest parses back with the expected default scene, named after the dir.
    auto m = load_manifest(dir);
    REQUIRE(m.has_value());
    REQUIRE(m->default_scene == "scenes/main.json");
    REQUIRE(m->name == std::filesystem::path(dir).filename().string());

    fs::remove_all(dir);
}

TEST_CASE("create_project refuses to overwrite an existing directory", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_project_dir("exists");
    fs::remove_all(dir);
    fs::create_directories(dir); // pre-existing

    REQUIRE_FALSE(create_project(dir, types)); // refuses
    REQUIRE_FALSE(fs::exists(fs::path(dir) / "mosaic.toml")); // nothing written

    fs::remove_all(dir);
}
