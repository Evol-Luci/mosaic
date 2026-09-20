#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/project/project_io.hpp"

using mosaic::app::create_project;
using mosaic::app::resolve_startup_scene;

namespace fs = std::filesystem;

namespace
{
std::string temp_dir(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t3r_") + tag + std::to_string(::getpid()))).string();
}
} // namespace

TEST_CASE("resolve_startup_scene maps a project dir to its default scene", "[app]")
{
    const std::string dir = temp_dir("proj");
    fs::remove_all(dir);
    REQUIRE(create_project(dir));

    auto scene = resolve_startup_scene(dir);
    REQUIRE(scene.has_value());
    REQUIRE(*scene == (fs::path(dir) / "scenes" / "main.json").string());
    REQUIRE(fs::exists(*scene));

    fs::remove_all(dir);
}

TEST_CASE("resolve_startup_scene returns a .json path unchanged", "[app]")
{
    auto scene = resolve_startup_scene("some/level.json");
    REQUIRE(scene.has_value());
    REQUIRE(*scene == "some/level.json");
}

TEST_CASE("resolve_startup_scene errors on a missing path or a dir without a manifest", "[app]")
{
    REQUIRE_FALSE(resolve_startup_scene("/no/such/mosaic_path_xyz").has_value()); // missing

    const std::string dir = temp_dir("nomani");
    fs::remove_all(dir);
    fs::create_directories(dir); // a dir but no mosaic.toml
    REQUIRE_FALSE(resolve_startup_scene(dir).has_value());
    fs::remove_all(dir);
}
