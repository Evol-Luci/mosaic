#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_install.hpp"
#include "app/project/project_io.hpp"

using mosaic::app::create_project;
using mosaic::app::InstalledPlugin;
using mosaic::app::load_manifest;
using mosaic::app::resolve_disabled_plugin_files;
using mosaic::app::resolve_plugins_dir;
using mosaic::app::resolve_startup_scene;
using mosaic::app::save_manifest;

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

TEST_CASE("resolve_plugins_dir resolves a project directory's plugins/ subdirectory", "[app]")
{
    const std::string dir = temp_dir("pluginsdir");
    fs::remove_all(dir);
    REQUIRE(create_project(dir));

    const auto plugins_dir = resolve_plugins_dir(dir);
    REQUIRE(plugins_dir.has_value());
    REQUIRE(*plugins_dir == (fs::path(dir) / "plugins").string());

    fs::remove_all(dir);
}

TEST_CASE("resolve_plugins_dir returns nullopt for a bare scene file or a missing path", "[app]")
{
    REQUIRE_FALSE(resolve_plugins_dir("some/level.json").has_value());
    REQUIRE_FALSE(resolve_plugins_dir("/no/such/mosaic_path_xyz").has_value());
}

TEST_CASE("resolve_disabled_plugin_files returns only the disabled entries' files", "[app]")
{
    const std::string dir = temp_dir("disabledfiles");
    fs::remove_all(dir);
    REQUIRE(create_project(dir));

    auto manifest = load_manifest(dir);
    REQUIRE(manifest.has_value());
    InstalledPlugin on;
    on.name    = "on";
    on.version = "1.0.0";
    on.files   = {"on.py"};
    on.enabled = true;
    InstalledPlugin off;
    off.name    = "off";
    off.version = "1.0.0";
    off.files   = {"off.py", "off_extra.py"};
    off.enabled = false;
    manifest->installed_plugins = {on, off};
    REQUIRE(save_manifest(dir, *manifest));

    const std::vector<std::string> disabled = resolve_disabled_plugin_files(dir);
    REQUIRE(disabled.size() == 2);
    REQUIRE(std::find(disabled.begin(), disabled.end(), "off.py") != disabled.end());
    REQUIRE(std::find(disabled.begin(), disabled.end(), "off_extra.py") != disabled.end());
    REQUIRE(std::find(disabled.begin(), disabled.end(), "on.py") == disabled.end());

    fs::remove_all(dir);
}

TEST_CASE("resolve_disabled_plugin_files is empty with no installed plugins", "[app]")
{
    const std::string dir = temp_dir("disablednone");
    fs::remove_all(dir);
    REQUIRE(create_project(dir));

    REQUIRE(resolve_disabled_plugin_files(dir).empty());

    fs::remove_all(dir);
}

TEST_CASE("resolve_disabled_plugin_files returns empty for a bare scene file or a missing path",
         "[app]")
{
    REQUIRE(resolve_disabled_plugin_files("some/level.json").empty());
    REQUIRE(resolve_disabled_plugin_files("/no/such/mosaic_path_xyz").empty());
}
