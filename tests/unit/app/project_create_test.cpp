#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/project/project.hpp"
#include "app/project/project_io.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"

using mosaic::app::create_project;
using mosaic::app::load_manifest;
using mosaic::engine::World;

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
    const std::string dir = temp_project_dir("create");
    fs::remove_all(dir); // clean slate

    REQUIRE(create_project(dir));
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
    const std::string dir = temp_project_dir("exists");
    fs::remove_all(dir);
    fs::create_directories(dir); // pre-existing

    REQUIRE_FALSE(create_project(dir)); // refuses
    REQUIRE_FALSE(fs::exists(fs::path(dir) / "mosaic.toml")); // nothing written

    fs::remove_all(dir);
}

TEST_CASE("create_project fails cleanly for an unknown template name", "[app]")
{
    const std::string dir = temp_project_dir("badtemplate");
    fs::remove_all(dir);

    REQUIRE_FALSE(create_project(dir, "does-not-exist"));
    REQUIRE_FALSE(fs::exists(dir));
}

TEST_CASE("create_project copies a template's assets/ and plugins/ content", "[app]")
{
    const std::string root = temp_project_dir("tmplroot");
    fs::remove_all(root);
    fs::create_directories(fs::path(root) / "mine" / "scenes");
    fs::create_directories(fs::path(root) / "mine" / "assets");
    fs::create_directories(fs::path(root) / "mine" / "plugins");
    std::ofstream(fs::path(root) / "mine" / "scenes" / "main.json") << "{\"entities\":[]}";
    std::ofstream(fs::path(root) / "mine" / "assets" / "note.txt") << "hello";
    std::ofstream(fs::path(root) / "mine" / "plugins" / "hook.py") << "MOSAIC_MENU_ITEMS = {}\n";

    const std::string dir = temp_project_dir("fromtmpl");
    fs::remove_all(dir);
    REQUIRE(create_project(dir, "mine", root));

    REQUIRE(fs::exists(fs::path(dir) / "scenes" / "main.json"));
    REQUIRE(fs::exists(fs::path(dir) / "assets" / "note.txt"));
    REQUIRE(fs::exists(fs::path(dir) / "plugins" / "hook.py"));
    REQUIRE(fs::exists(fs::path(dir) / "mosaic.toml")); // always freshly generated

    fs::remove_all(root); fs::remove_all(dir);
}

TEST_CASE("create_project still writes assets/.gitkeep when a template has no assets/",
         "[app]")
{
    const std::string root = temp_project_dir("tmplroot2");
    fs::remove_all(root);
    fs::create_directories(fs::path(root) / "bare" / "scenes");
    std::ofstream(fs::path(root) / "bare" / "scenes" / "main.json") << "{\"entities\":[]}";

    const std::string dir = temp_project_dir("fromtmpl2");
    fs::remove_all(dir);
    REQUIRE(create_project(dir, "bare", root));
    REQUIRE(fs::exists(fs::path(dir) / "assets" / ".gitkeep"));

    fs::remove_all(root); fs::remove_all(dir);
}

TEST_CASE("create_project creates missing parent directories", "[app]")
{
    // Regression test: fs::copy only creates the single leaf directory, not the
    // full chain -- `mosaic create ~/games/mygame` must still work when ~/games
    // doesn't exist yet, matching the pre-template-mechanism behavior.
    const std::string top = temp_project_dir("parentmissing");
    fs::remove_all(top);
    const std::string dir = (fs::path(top) / "nested" / "project").string();

    REQUIRE_FALSE(fs::exists(top));
    REQUIRE(create_project(dir));
    REQUIRE(fs::exists(fs::path(dir) / "mosaic.toml"));

    fs::remove_all(top);
}

TEST_CASE("create_project rejects a template name that would escape templates_root",
         "[app]")
{
    const std::string dir = temp_project_dir("escape1");
    fs::remove_all(dir);
    REQUIRE_FALSE(create_project(dir, "../assets"));
    REQUIRE_FALSE(fs::exists(dir));

    const std::string dir2 = temp_project_dir("escape2");
    fs::remove_all(dir2);
    REQUIRE_FALSE(create_project(dir2, ""));
    REQUIRE_FALSE(fs::exists(dir2));
}
