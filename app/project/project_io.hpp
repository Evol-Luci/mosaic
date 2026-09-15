#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"
#include "app/run/scene_io.hpp"     // load_scene_file / save_scene_file
#include "engine/reflection/component_reflection.hpp"
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

// The compile-time templates/ root (see app/CMakeLists.txt's MOSAIC_TEMPLATES_DIR).
// A separate function (not the raw macro) so create_project's default argument
// can reference it, and so tests can point templates_root at a fixture directory
// instead of overriding a macro.
inline std::string default_templates_root() { return MOSAIC_TEMPLATES_DIR; }

// Scaffold a new project at `dir` by copying `templates_root/template_name/`'s
// contents into it (scenes/, and optionally assets/ and/or plugins/), then writing
// a fresh mosaic.toml (name derived from `dir`'s basename). Guarantees an assets/
// directory exists afterward (with .gitkeep if the template left it empty or
// omitted it), matching every project's existing contract. Returns false (and logs)
// if `dir` already exists, `template_name` isn't a subdirectory of `templates_root`
// (the error names every available template), or any copy/write fails — never
// leaves a half-written project. `types` is accepted for signature stability with
// existing call sites but is unused now that templates are static files, not
// in-process-built scenes (see docs/design/project-templates.md).
inline bool create_project(const std::string& dir,
                           const mosaic::engine::reflection::ComponentReflection& types,
                           const std::string& template_name = "default",
                           const std::string& templates_root = default_templates_root())
{
    namespace fs = std::filesystem;
    (void)types;
    if (fs::exists(dir))
    {
        mosaic::foundation::log::error("project", "refusing to overwrite existing path: {}", dir);
        return false;
    }

    // Reject anything that isn't a plain directory-entry name: an empty name would
    // resolve to templates_root itself (copying every template in), and a name
    // containing a path separator or ".."/"." would let --template escape
    // templates_root entirely (e.g. "--template ../assets").
    if (template_name.empty() || template_name == "." || template_name == ".." ||
        template_name.find('/') != std::string::npos ||
        template_name.find('\\') != std::string::npos)
    {
        mosaic::foundation::log::error("project", "invalid template name '{}'", template_name);
        return false;
    }

    const fs::path template_dir = fs::path(templates_root) / template_name;
    std::error_code exists_ec;
    if (!fs::is_directory(template_dir, exists_ec))
    {
        std::string available;
        std::error_code list_ec;
        for (const auto& entry : fs::directory_iterator(templates_root, list_ec))
        {
            if (!entry.is_directory()) { continue; }
            if (!available.empty()) { available += ", "; }
            available += entry.path().filename().string();
        }
        mosaic::foundation::log::error(
            "project", "unknown template '{}' (available: {})", template_name, available);
        return false;
    }

    // Create `dir` (and any missing parent directories -- fs::copy below only
    // creates the single leaf directory, not the full chain, which is a real
    // regression from the previous fs::create_directories-based implementation:
    // `mosaic create ~/games/mygame` must still work when ~/games doesn't exist yet).
    std::error_code dir_ec;
    fs::create_directories(dir, dir_ec);
    if (dir_ec)
    {
        mosaic::foundation::log::error("project", "could not create {}: {}", dir, dir_ec.message());
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }

    std::error_code copy_ec;
    fs::copy(template_dir, dir, fs::copy_options::recursive, copy_ec);
    if (copy_ec)
    {
        mosaic::foundation::log::error("project", "could not copy template '{}' into {}: {}",
                                       template_name, dir, copy_ec.message());
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }

    std::error_code mkdir_ec;
    fs::create_directories(fs::path(dir) / kAssetsDir, mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error("project", "could not create assets/ for {}", dir);
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }
    std::error_code empty_ec;
    if (fs::is_empty(fs::path(dir) / kAssetsDir, empty_ec) && !empty_ec)
    {
        if (!save_scene_file((fs::path(dir) / kAssetsDir / ".gitkeep").string(), ""))
        {
            mosaic::foundation::log::error("project", "could not write assets/.gitkeep for {}", dir);
            std::error_code rmec; fs::remove_all(dir, rmec);
            return false;
        }
    }

    // Project name = the directory's basename (robust against a trailing separator).
    fs::path p(dir);
    std::string name = p.filename().string();
    if (name.empty())
    {
        name = p.parent_path().filename().string();
    }

    // Manifest -- always freshly generated, overwriting anything a template might
    // (incorrectly) have included.
    ProjectManifest manifest;
    manifest.name = name;
    manifest.default_scene = kStarterScene;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((p / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("project", "could not write manifest for {}", dir);
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }

    mosaic::foundation::log::info("project", "created project {} from template '{}'",
                                  dir, template_name);
    return true;
}

// The "smart positional" resolver. Returns the scene file to load, or nullopt on error:
//   - `path` is a directory containing mosaic.toml -> parse it -> "<path>/<default_scene>"
//   - `path` has a .json extension                 -> `path` itself (a bare scene; existence is
//                                                     validated later by populate_world)
//   - otherwise (missing, or a dir without a manifest) -> nullopt (the caller reports).
inline std::optional<std::string> resolve_startup_scene(const std::string& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(path, ec))
    {
        const std::optional<ProjectManifest> m = load_manifest(path);
        if (!m)
        {
            return std::nullopt;
        }
        return (fs::path(path) / m->default_scene).string();
    }
    if (fs::path(path).extension() == ".json")
    {
        return path;
    }
    return std::nullopt;
}
} // namespace mosaic::app
