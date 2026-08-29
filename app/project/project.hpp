#pragma once

#include <string>

namespace mosaic::app
{
// Well-known names inside a project directory.
inline constexpr const char* kManifestName = "mosaic.toml";
inline constexpr const char* kScenesDir    = "scenes";
inline constexpr const char* kAssetsDir    = "assets";
inline constexpr const char* kStarterScene = "scenes/main.json"; // manifest default_scene

// The project manifest (mosaic.toml), reflected so it round-trips through the #10 TOML backend
// exactly like AssetMetadata. Minimal on purpose (version/author would be speculative).
struct ProjectManifest
{
    std::string name;          // project name (defaults to the directory basename)
    std::string default_scene; // project-relative path to the scene opened by default
};

template <class V>
void reflect(V& v, ProjectManifest& m)
{
    v.member("name", m.name);
    v.member("default_scene", m.default_scene);
}
} // namespace mosaic::app
