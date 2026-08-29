#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"
#include "app/run/demo_scene.hpp"   // build_default_scene
#include "app/run/scene_io.hpp"     // load_scene_file / save_scene_file
#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"   // save_to_json
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value / from_value
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/toml_backend.hpp"  // to_toml / from_toml

namespace mosaic::app
{
// Parse <project_dir>/mosaic.toml into a ProjectManifest. nullopt if the file is missing or
// unparseable (the caller reports).
inline std::optional<ProjectManifest> load_manifest(const std::string& project_dir)
{
    const std::string path = (std::filesystem::path(project_dir) / kManifestName).string();
    const std::optional<std::string> text = load_scene_file(path); // generic text read
    if (!text)
    {
        return std::nullopt;
    }
    try
    {
        ProjectManifest m;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), m);
        return m;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        return std::nullopt;
    }
}

// Scaffold a new project at `dir`: mosaic.toml + scenes/main.json (the default demo scene) + an
// empty assets/ (with a .gitkeep). Returns false (and logs) if `dir` already exists or a write
// fails — never clobbers existing work.
inline bool create_project(const std::string& dir,
                           const mosaic::engine::reflection::ComponentReflection& types)
{
    namespace fs = std::filesystem;
    if (fs::exists(dir))
    {
        mosaic::foundation::log::error("project", "refusing to overwrite existing path: {}", dir);
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(dir) / kScenesDir, ec);
    fs::create_directories(fs::path(dir) / kAssetsDir, ec);
    if (ec)
    {
        mosaic::foundation::log::error("project", "could not create project dirs: {}", dir);
        return false;
    }

    // Project name = the directory's basename (robust against a trailing separator).
    fs::path p(dir);
    std::string name = p.filename().string();
    if (name.empty())
    {
        name = p.parent_path().filename().string();
    }

    // Manifest.
    ProjectManifest manifest;
    manifest.name = name;
    manifest.default_scene = kStarterScene;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((p / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("project", "could not write manifest for {}", dir);
        return false;
    }

    // Starter scene = the default demo (reuses T2).
    mosaic::engine::World world;
    build_default_scene(world);
    const std::string scene_json = mosaic::engine::scene::save_to_json(world, types);
    if (!save_scene_file((p / kStarterScene).string(), scene_json))
    {
        mosaic::foundation::log::error("project", "could not write starter scene for {}", dir);
        return false;
    }

    // Keep the empty assets/ dir under version control.
    save_scene_file((p / kAssetsDir / ".gitkeep").string(), "");

    mosaic::foundation::log::info("project", "created project {}", dir);
    return true;
}
} // namespace mosaic::app
