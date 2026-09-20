#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/studio_prefs.hpp"

using mosaic::app::load_studio_prefs;
using mosaic::app::save_studio_prefs;
using mosaic::app::StudioPrefs;

namespace
{
std::filesystem::path make_fixture_prefs_path(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("mosaic_studio_prefs_test_" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir / "studio.toml";
}
} // namespace

TEST_CASE("load_studio_prefs on a missing file returns defaults", "[app]")
{
    const std::filesystem::path path = make_fixture_prefs_path("missing");
    const StudioPrefs prefs = load_studio_prefs(path);
    REQUIRE(prefs.window_width == 1280);
    REQUIRE(prefs.window_height == 720);
}

TEST_CASE("save_studio_prefs then load_studio_prefs round-trips a custom window size", "[app]")
{
    const std::filesystem::path path = make_fixture_prefs_path("roundtrip");
    StudioPrefs prefs;
    prefs.window_width  = 1920;
    prefs.window_height = 1080;

    REQUIRE(save_studio_prefs(prefs, path));

    const StudioPrefs loaded = load_studio_prefs(path);
    REQUIRE(loaded.window_width == 1920);
    REQUIRE(loaded.window_height == 1080);
}

TEST_CASE("load_studio_prefs on an unparseable file returns defaults", "[app]")
{
    const std::filesystem::path path = make_fixture_prefs_path("unparseable");
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "this is not valid toml {{{";
    }

    const StudioPrefs prefs = load_studio_prefs(path);
    REQUIRE(prefs.window_width == 1280);
    REQUIRE(prefs.window_height == 720);
}

TEST_CASE("save_studio_prefs creates its parent directory if missing", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "mosaic_studio_prefs_test_create_parent";
    std::error_code ec;
    fs::remove_all(dir, ec);
    const fs::path path = dir / "nested" / "studio.toml";

    StudioPrefs prefs;
    REQUIRE(save_studio_prefs(prefs, path));
    REQUIRE(fs::exists(path));
}
