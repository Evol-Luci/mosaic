#pragma once

#include <string>
#include <vector>

namespace mosaic::app
{
// Well-known names inside a project directory.
inline constexpr const char* kManifestName = "mosaic.toml";
inline constexpr const char* kScenesDir    = "scenes";
inline constexpr const char* kAssetsDir    = "assets";
inline constexpr const char* kPluginsDir   = "plugins";
inline constexpr const char* kStarterScene = "scenes/main.json"; // manifest default_scene

// One installed plugin package, recorded in ProjectManifest.installed_plugins.
// `files` is the exact set of *.py filenames this package placed in plugins/ at
// install time -- stored explicitly (not re-derived from the source package at
// removal time) so `mosaic plugin remove` knows precisely what to delete even if
// the source package's own file list has since changed.
struct InstalledPlugin
{
    std::string              name;    // package name
    std::string              version; // package version at install time
    std::vector<std::string> files;   // filenames this package owns in plugins/
};

template <class V>
void reflect(V& v, InstalledPlugin& p)
{
    v.member("name", p.name);
    v.member("version", p.version);
    v.member("files", p.files);
}

// The project manifest (mosaic.toml), reflected so it round-trips through the #10 TOML backend
// exactly like AssetMetadata. Minimal on purpose (version/author would be speculative).
struct ProjectManifest
{
    std::string name;          // project name (defaults to the directory basename)
    std::string default_scene; // project-relative path to the scene opened by default
    std::vector<InstalledPlugin> installed_plugins; // packages installed via `mosaic plugin install`
};

template <class V>
void reflect(V& v, ProjectManifest& m)
{
    v.member("name", m.name);
    v.member("default_scene", m.default_scene);
    v.member("installed_plugins", m.installed_plugins);
}
} // namespace mosaic::app
