#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/run/scene_io.hpp" // load_scene_file
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/reflect_value.hpp" // from_value
#include "foundation/serialization/toml_backend.hpp"  // from_toml

namespace mosaic::app
{

// A package's own package.toml contents.
struct PackageManifest
{
    std::string name;
    std::string version;
    std::string description;
};

template <class V>
void reflect(V& v, PackageManifest& m)
{
    v.member("name", m.name);
    v.member("version", m.version);
    v.member("description", m.description);
}

// One discovered package: its manifest fields plus the directory it lives in
// (holding package.toml and its *.py files).
struct PackageInfo
{
    std::string           name;
    std::string           version;
    std::string           description;
    std::filesystem::path dir;
};

// The compile-time packages/ root (see app/CMakeLists.txt's MOSAIC_PACKAGES_DIR).
// A separate function (not the raw macro), mirroring default_templates_root() /
// default_docs_dir()-style helpers elsewhere in app/ -- lets tests point a source
// directory at a fixture instead of overriding a macro.
inline std::string default_package_source() { return MOSAIC_PACKAGES_DIR; }

// Parse `dir`/package.toml into a PackageManifest. nullopt if the file is missing
// or unparseable (the caller logs and skips).
inline std::optional<PackageManifest> load_package_manifest(const std::filesystem::path& dir)
{
    const std::optional<std::string> text = load_scene_file((dir / "package.toml").string());
    if (!text)
    {
        return std::nullopt;
    }
    try
    {
        PackageManifest m;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), m);
        return m;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        return std::nullopt;
    }
}

// Every valid package directly under `source_dir` (immediate subdirectories with a
// parseable package.toml), sorted alphabetically by name for deterministic output.
// A missing source_dir, or one with no valid packages, returns an empty vector --
// not an error (mirrors create_project's "no plugins/ folder is the common case,
// not an error" precedent). A subdirectory with a missing or unparseable
// package.toml is skipped with a logged warning, never treated as a fatal error
// for the whole listing.
inline std::vector<PackageInfo> list_packages(const std::string& source_dir)
{
    namespace fs = std::filesystem;
    std::vector<PackageInfo> packages;
    std::error_code dir_ec;
    if (!fs::is_directory(source_dir, dir_ec))
    {
        return packages;
    }
    std::error_code iter_ec;
    for (const auto& entry : fs::directory_iterator(source_dir, iter_ec))
    {
        if (iter_ec)
        {
            break;
        }
        if (!entry.is_directory())
        {
            continue;
        }
        // Skip dotfile/dotdir entries silently -- a git-backed source always
        // has a ".git" subdirectory, which isn't a package and shouldn't warn
        // on every listing.
        if (entry.path().filename().string().starts_with('.'))
        {
            continue;
        }
        const std::optional<PackageManifest> m = load_package_manifest(entry.path());
        if (!m)
        {
            mosaic::foundation::log::warn(
                "plugin", "skipping {} (missing or unparseable package.toml)",
                entry.path().string());
            continue;
        }
        packages.push_back(PackageInfo{m->name, m->version, m->description, entry.path()});
    }
    std::sort(packages.begin(), packages.end(),
             [](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; });
    return packages;
}

// The single package named `name` under `source_dir`, or nullopt if not found.
inline std::optional<PackageInfo> find_package(const std::string& source_dir, const std::string& name)
{
    for (const PackageInfo& p : list_packages(source_dir))
    {
        if (p.name == name)
        {
            return p;
        }
    }
    return std::nullopt;
}

} // namespace mosaic::app
