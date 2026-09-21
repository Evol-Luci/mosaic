#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/studio_prefs.hpp"

using mosaic::app::kMaxRecentProjects;
using mosaic::app::load_studio_prefs;
using mosaic::app::record_recent_project;
using mosaic::app::save_studio_prefs;
using mosaic::app::should_show_recent_projects_picker;
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

TEST_CASE("a fresh StudioPrefs has an empty recent_projects list", "[app]")
{
    REQUIRE(StudioPrefs{}.recent_projects.empty());
}

TEST_CASE("record_recent_project inserts a new path at the front", "[app]")
{
    StudioPrefs prefs;
    record_recent_project(prefs, "/projects/a");
    REQUIRE(prefs.recent_projects == std::vector<std::string>{"/projects/a"});

    record_recent_project(prefs, "/projects/b");
    REQUIRE(prefs.recent_projects == std::vector<std::string>{"/projects/b", "/projects/a"});
}

TEST_CASE("record_recent_project moves a re-opened path to the front instead of duplicating it",
         "[app]")
{
    StudioPrefs prefs;
    record_recent_project(prefs, "/projects/a");
    record_recent_project(prefs, "/projects/b");
    record_recent_project(prefs, "/projects/c");
    record_recent_project(prefs, "/projects/a"); // reopen the first one

    REQUIRE(prefs.recent_projects ==
           std::vector<std::string>{"/projects/a", "/projects/c", "/projects/b"});
}

TEST_CASE("record_recent_project caps the list at kMaxRecentProjects, dropping the oldest",
         "[app]")
{
    StudioPrefs prefs;
    for (std::size_t i = 0; i < kMaxRecentProjects + 3; ++i)
    {
        record_recent_project(prefs, "/projects/" + std::to_string(i));
    }

    REQUIRE(prefs.recent_projects.size() == kMaxRecentProjects);
    // Most recent (highest index) first; the three oldest (0, 1, 2) were dropped.
    REQUIRE(prefs.recent_projects.front() ==
           "/projects/" + std::to_string(kMaxRecentProjects + 2));
    for (const std::string& path : prefs.recent_projects)
    {
        REQUIRE(path != "/projects/0");
        REQUIRE(path != "/projects/1");
        REQUIRE(path != "/projects/2");
    }
}

TEST_CASE("save_studio_prefs then load_studio_prefs round-trips recent_projects", "[app]")
{
    const std::filesystem::path path = make_fixture_prefs_path("recent_roundtrip");
    StudioPrefs prefs;
    record_recent_project(prefs, "/projects/a");
    record_recent_project(prefs, "/projects/b");

    REQUIRE(save_studio_prefs(prefs, path));

    const StudioPrefs loaded = load_studio_prefs(path);
    REQUIRE(loaded.recent_projects == std::vector<std::string>{"/projects/b", "/projects/a"});
}

TEST_CASE("should_show_recent_projects_picker is false when an open path was given, "
         "regardless of recent_projects",
         "[app]")
{
    REQUIRE_FALSE(should_show_recent_projects_picker(true, {}));
    REQUIRE_FALSE(should_show_recent_projects_picker(true, {"/projects/a"}));
}

TEST_CASE("should_show_recent_projects_picker is false with no open path but nothing recent",
         "[app]")
{
    REQUIRE_FALSE(should_show_recent_projects_picker(false, {}));
}

TEST_CASE("should_show_recent_projects_picker is true with no open path and a non-empty list",
         "[app]")
{
    REQUIRE(should_show_recent_projects_picker(false, {"/projects/a"}));
}
