#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/plugin/package_source.hpp" // PackageInfo
#include "app/project/project.hpp"       // ProjectManifest, InstalledPlugin, kManifestName, kPluginsDir
#include "app/project/project_io.hpp"    // load_manifest
#include "app/run/scene_io.hpp"          // save_scene_file
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml

namespace mosaic::app
{

// Write `manifest` to <project_dir>/mosaic.toml. Returns false (and logs) on
// write failure. Takes a non-const reference because to_value()/reflect() are
// generically non-const throughout this codebase's serialization stack (matching
// every other to_value(manifest) call site, e.g. create_project in project_io.hpp).
inline bool save_manifest(const std::string& project_dir, ProjectManifest& manifest)
{
    namespace fs = std::filesystem;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((fs::path(project_dir) / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("plugin", "could not write manifest for {}", project_dir);
        return false;
    }
    return true;
}

// Install `pkg` into `project_dir`: copies every *.py file directly under pkg.dir
// into <project_dir>/plugins/ (created if missing) and records the install in
// mosaic.toml's installed_plugins. Fails (nothing written) if project_dir isn't a
// Mosaic project, or any of the package's filenames already exists in plugins/ and
// isn't already tracked as belonging to this same package. Reinstalling the same
// package at the same version is a no-op success; at a different version, the
// package's own files are overwritten and the manifest entry's version updated --
// never touches a file this package doesn't own.
inline bool install_package(const std::string& project_dir, const PackageInfo& pkg)
{
    namespace fs = std::filesystem;

    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("plugin", "not a Mosaic project: {}", project_dir);
        return false;
    }

    std::vector<std::string> files;
    std::error_code list_ec;
    for (const auto& entry : fs::directory_iterator(pkg.dir, list_ec))
    {
        if (list_ec)
        {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".py")
        {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());

    auto existing = std::find_if(
        manifest->installed_plugins.begin(), manifest->installed_plugins.end(),
        [&](const InstalledPlugin& p) { return p.name == pkg.name; });
    const bool already_installed = existing != manifest->installed_plugins.end();
    if (already_installed && existing->version == pkg.version)
    {
        mosaic::foundation::log::info(
            "plugin", "{} {} already installed in {}", pkg.name, pkg.version, project_dir);
        return true;
    }

    const fs::path plugins_dir = fs::path(project_dir) / kPluginsDir;
    std::error_code mkdir_ec;
    fs::create_directories(plugins_dir, mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error(
            "plugin", "could not create {}: {}", plugins_dir.string(), mkdir_ec.message());
        return false;
    }

    // Guard: refuse to clobber any file this package doesn't already own.
    for (const std::string& file : files)
    {
        const bool owned_by_this_package =
            already_installed &&
            std::find(existing->files.begin(), existing->files.end(), file) != existing->files.end();
        std::error_code exists_ec;
        if (fs::exists(plugins_dir / file, exists_ec) && !owned_by_this_package)
        {
            mosaic::foundation::log::error(
                "plugin", "refusing to overwrite untracked file {} while installing {}",
                (plugins_dir / file).string(), pkg.name);
            return false;
        }
    }

    for (const std::string& file : files)
    {
        std::error_code copy_ec;
        fs::copy_file(pkg.dir / file, plugins_dir / file, fs::copy_options::overwrite_existing, copy_ec);
        if (copy_ec)
        {
            mosaic::foundation::log::error(
                "plugin", "could not copy {}: {}", file, copy_ec.message());
            return false;
        }
    }

    if (already_installed)
    {
        existing->version = pkg.version;
        existing->files   = files;
    }
    else
    {
        manifest->installed_plugins.push_back(InstalledPlugin{pkg.name, pkg.version, files});
    }

    if (!save_manifest(project_dir, *manifest))
    {
        return false;
    }
    mosaic::foundation::log::info("plugin", "installed {} {} into {}", pkg.name, pkg.version, project_dir);
    return true;
}

// Remove package `name` from `project_dir`: deletes the *.py files its manifest
// entry says it owns from plugins/, then removes the manifest entry. Not
// installed is not an error -- returns true, a no-op.
inline bool remove_package(const std::string& project_dir, const std::string& name)
{
    namespace fs = std::filesystem;

    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("plugin", "not a Mosaic project: {}", project_dir);
        return false;
    }

    auto it = std::find_if(
        manifest->installed_plugins.begin(), manifest->installed_plugins.end(),
        [&](const InstalledPlugin& p) { return p.name == name; });
    if (it == manifest->installed_plugins.end())
    {
        mosaic::foundation::log::info("plugin", "{} is not installed in {}", name, project_dir);
        return true;
    }

    const fs::path plugins_dir = fs::path(project_dir) / kPluginsDir;
    for (const std::string& file : it->files)
    {
        std::error_code rm_ec;
        fs::remove(plugins_dir / file, rm_ec);
    }

    manifest->installed_plugins.erase(it);
    if (!save_manifest(project_dir, *manifest))
    {
        return false;
    }
    mosaic::foundation::log::info("plugin", "removed {} from {}", name, project_dir);
    return true;
}

} // namespace mosaic::app
