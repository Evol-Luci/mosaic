#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/run/scene_io.hpp" // load_scene_file / save_scene_file
#include "app/user_config_dir.hpp"
#include "engine/render/render_device.hpp" // UiTheme
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/reflect_value.hpp" // to_value/from_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml/from_toml

namespace mosaic::app
{

// StudioPrefs::theme's on-disk values -- the reflection layer supports int, not
// enum class (see engine/particles/particle_emitter.hpp's Easing for the same
// convention), so the field itself is a plain int; these names are the stable
// on-disk contract for it. Kept in exact correspondence with
// mosaic::engine::render::UiTheme (studio_theme_to_ui_theme() below maps one to
// the other) -- append new themes to both together, do not renumber either.
enum StudioTheme : int
{
    ThemeDark    = 0,
    ThemeLight   = 1,
    ThemeClassic = 2,
};

// Maps a persisted/menu-selected StudioTheme to the engine-level UiTheme
// RenderDevice::init_ui()/set_ui_theme() actually apply. An out-of-range value
// (a hand-edited or future-version studio.toml) falls back to Dark rather than
// throwing -- matches load_studio_prefs()'s own "never fail startup over a
// preferences file" philosophy.
inline mosaic::engine::render::UiTheme studio_theme_to_ui_theme(int theme)
{
    switch (theme)
    {
    case ThemeLight: return mosaic::engine::render::UiTheme::Light;
    case ThemeClassic: return mosaic::engine::render::UiTheme::Classic;
    case ThemeDark:
    default: return mosaic::engine::render::UiTheme::Dark;
    }
}

// The inverse of studio_theme_to_ui_theme() -- StudioApp tracks the live theme as a
// UiTheme (it only knows about the engine-level type, not app/'s StudioTheme
// constants); the editor reads it back via this at exit to persist into StudioPrefs.
inline int ui_theme_to_studio_theme(mosaic::engine::render::UiTheme theme)
{
    switch (theme)
    {
    case mosaic::engine::render::UiTheme::Light: return ThemeLight;
    case mosaic::engine::render::UiTheme::Classic: return ThemeClassic;
    case mosaic::engine::render::UiTheme::Dark:
    default: return ThemeDark;
    }
}

// Most projects opened via `mosaic editor <project-dir>` a user will realistically want
// back on a startup picker -- capped well short of becoming its own scrollable-list
// design problem.
inline constexpr std::size_t kMaxRecentProjects = 10;

// Studio's persisted window size, theme, and recently-opened project directories, read
// at startup and written back at exit (see app/run/editor.cpp). Panel layout is a
// separate concern -- ImGui already persists that itself, this just relocates its ini
// file next to this one via studio_layout_ini_path() below.
struct StudioPrefs
{
    int                      window_width  = 1280;
    int                      window_height = 720;
    int                      theme         = ThemeDark;
    std::vector<std::string> recent_projects; // most-recently-opened first, capped, deduped
};

template <class V>
void reflect(V& v, StudioPrefs& p)
{
    v.member("window_width", p.window_width);
    v.member("window_height", p.window_height);
    v.member("theme", p.theme);
    v.member("recent_projects", p.recent_projects);
}

// Records `path` as the most-recently-opened project in `prefs.recent_projects`:
// removes any existing equal entry (so reopening a project moves it to the front
// rather than duplicating it) then inserts at the front, capping the list at
// kMaxRecentProjects by dropping the oldest entries. Does not save to disk itself --
// the caller (app/run/editor.cpp) is responsible for calling save_studio_prefs()
// afterward, the same "caller owns persistence timing" split load_studio_prefs()/
// save_studio_prefs() already have.
inline void record_recent_project(StudioPrefs& prefs, const std::string& path)
{
    auto& recent = prefs.recent_projects;
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
    recent.insert(recent.begin(), path);
    if (recent.size() > kMaxRecentProjects)
    {
        recent.resize(kMaxRecentProjects);
    }
}

// Whether `mosaic editor` should offer the "Recent Projects" startup picker:
// only when the user gave no project-dir/scene-file argument at all
// (`open_path_given` is false) AND there's actually something to pick from.
// Explicitly opening a project or a bare scene file always skips the picker,
// even if recent_projects is non-empty -- the picker is a fallback for "no idea
// what to open," not an interruption of an explicit choice.
inline bool should_show_recent_projects_picker(bool                             open_path_given,
                                               const std::vector<std::string>& recent_projects)
{
    return !open_path_given && !recent_projects.empty();
}

// The default studio.toml path, under the shared user-config directory
// (see docs/design/user-config-directory.md) -- a separate function (not
// calling user_config_dir() inline at every call site) so tests can override
// it with a fixture path, mirroring sources_registry_path()'s own role.
inline std::filesystem::path studio_prefs_path() { return user_config_dir() / "studio.toml"; }

// ImGui's own ini-format panel-layout file, alongside studio.toml. Not
// loaded/saved by this file -- ImGui reads/writes it directly once its
// IniFilename is pointed here (see RenderDevice::init_ui()'s ini_path
// parameter).
inline std::filesystem::path studio_layout_ini_path() { return user_config_dir() / "studio_layout.ini"; }

// Loads `path` (default: the real studio.toml). A missing or unparseable
// file is not an error -- returns default-constructed prefs (matches
// load_sources_registry's own "nothing configured yet" precedent).
inline StudioPrefs load_studio_prefs(const std::filesystem::path& path = studio_prefs_path())
{
    const std::optional<std::string> text = load_scene_file(path.string());
    if (!text)
    {
        return StudioPrefs{};
    }
    try
    {
        StudioPrefs prefs;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), prefs);
        return prefs;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        mosaic::foundation::log::error("studio", "could not parse {}, using defaults", path.string());
        return StudioPrefs{};
    }
}

// Writes `prefs` to `path` (default: the real studio.toml), creating its
// parent directory if needed. Returns false (and logs) on failure.
inline bool save_studio_prefs(StudioPrefs& prefs, const std::filesystem::path& path = studio_prefs_path())
{
    namespace fs = std::filesystem;
    std::error_code mkdir_ec;
    fs::create_directories(path.parent_path(), mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error("studio", "could not create {}: {}",
                                       path.parent_path().string(), mkdir_ec.message());
        return false;
    }
    const std::string toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(prefs));
    if (!save_scene_file(path.string(), toml))
    {
        mosaic::foundation::log::error("studio", "could not write {}", path.string());
        return false;
    }
    return true;
}

} // namespace mosaic::app
