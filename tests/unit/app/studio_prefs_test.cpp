#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/studio_prefs.hpp"

using mosaic::app::load_studio_prefs;
using mosaic::app::save_studio_prefs;
using mosaic::app::StudioPrefs;
using mosaic::app::studio_theme_to_ui_theme;
using mosaic::app::ThemeClassic;
using mosaic::app::ThemeDark;
using mosaic::app::ThemeLight;
using mosaic::app::ui_theme_to_studio_theme;
using mosaic::engine::render::UiTheme;

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
    REQUIRE(prefs.theme == ThemeDark);
}

TEST_CASE("save_studio_prefs then load_studio_prefs round-trips a custom window size and theme",
         "[app]")
{
    const std::filesystem::path path = make_fixture_prefs_path("roundtrip");
    StudioPrefs prefs;
    prefs.window_width  = 1920;
    prefs.window_height = 1080;
    prefs.theme         = ThemeLight;

    REQUIRE(save_studio_prefs(prefs, path));

    const StudioPrefs loaded = load_studio_prefs(path);
    REQUIRE(loaded.window_width == 1920);
    REQUIRE(loaded.window_height == 1080);
    REQUIRE(loaded.theme == ThemeLight);
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
    REQUIRE(prefs.theme == ThemeDark);
}

TEST_CASE("load_studio_prefs on a file predating the theme field defaults theme to Dark",
         "[app]")
{
    // A studio.toml written before this feature shipped has no `theme` key at all --
    // the ReadVisitor leaves an unmatched field untouched, so this must resolve to
    // the struct's own default rather than erroring or leaving it uninitialized.
    const std::filesystem::path path = make_fixture_prefs_path("pre_theme_field");
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "window_width = 1600\nwindow_height = 900\n";
    }

    const StudioPrefs prefs = load_studio_prefs(path);
    REQUIRE(prefs.window_width == 1600);
    REQUIRE(prefs.window_height == 900);
    REQUIRE(prefs.theme == ThemeDark);
}

TEST_CASE("studio_theme_to_ui_theme maps every StudioTheme to its UiTheme counterpart", "[app]")
{
    REQUIRE(studio_theme_to_ui_theme(ThemeDark) == UiTheme::Dark);
    REQUIRE(studio_theme_to_ui_theme(ThemeLight) == UiTheme::Light);
    REQUIRE(studio_theme_to_ui_theme(ThemeClassic) == UiTheme::Classic);
}

TEST_CASE("studio_theme_to_ui_theme falls back to Dark for an out-of-range value", "[app]")
{
    REQUIRE(studio_theme_to_ui_theme(99) == UiTheme::Dark);
    REQUIRE(studio_theme_to_ui_theme(-1) == UiTheme::Dark);
}

TEST_CASE("ui_theme_to_studio_theme is the exact inverse of studio_theme_to_ui_theme", "[app]")
{
    REQUIRE(ui_theme_to_studio_theme(UiTheme::Dark) == ThemeDark);
    REQUIRE(ui_theme_to_studio_theme(UiTheme::Light) == ThemeLight);
    REQUIRE(ui_theme_to_studio_theme(UiTheme::Classic) == ThemeClassic);
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
