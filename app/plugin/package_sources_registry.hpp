#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/run/scene_io.hpp" // load_scene_file / save_scene_file
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/reflect_value.hpp" // to_value/from_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml/from_toml

namespace mosaic::app
{

// One registered named source: a friendly name -> a local filesystem path holding
// packages (see app/plugin/package_source.hpp's list_packages/find_package). Mosaic
// never runs git itself -- the user clones/pulls that path however they like; this
// registry only remembers where it is.
struct NamedSource
{
    std::string name;
    std::string path;
};

template <class V>
void reflect(V& v, NamedSource& s)
{
    v.member("name", s.name);
    v.member("path", s.path);
}

// The persisted contents of <user_config_dir>/sources.toml.
struct SourcesRegistry
{
    std::vector<NamedSource> sources;
};

template <class V>
void reflect(V& v, SourcesRegistry& r)
{
    v.member("sources", r.sources);
}

// $XDG_CONFIG_HOME/mosaic if set (and non-empty), else $HOME/.config/mosaic. This is
// the first user-level (not project-level, not compile-time-baked) directory anywhere
// in this codebase -- see docs/design/user-config-directory.md for the rationale and
// forward-looking notes on what else might live here later.
inline std::filesystem::path user_config_dir()
{
    namespace fs = std::filesystem;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0')
    {
        return fs::path(xdg) / "mosaic";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home != nullptr ? home : "") / ".config" / "mosaic";
}

// The default sources.toml path. A separate function (not calling user_config_dir()
// inline at every call site) so every registry function below can default to it while
// tests still override it with a fixture path -- mirrors default_package_source()'s
// role for MOSAIC_PACKAGES_DIR.
inline std::filesystem::path sources_registry_path() { return user_config_dir() / "sources.toml"; }

// Loads `registry_path` (default: the real sources.toml). A missing or unparseable
// file is not an error -- returns an empty registry (matches every other "nothing
// configured yet" default in this codebase, e.g. list_packages on a missing source
// directory).
inline SourcesRegistry load_sources_registry(
    const std::filesystem::path& registry_path = sources_registry_path())
{
    const std::optional<std::string> text = load_scene_file(registry_path.string());
    if (!text)
    {
        return SourcesRegistry{};
    }
    try
    {
        SourcesRegistry r;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), r);
        return r;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        mosaic::foundation::log::error(
            "plugin", "could not parse {}, treating as empty", registry_path.string());
        return SourcesRegistry{};
    }
}

// Writes `registry` to `registry_path` (default: the real sources.toml), creating its
// parent directory if needed. Returns false (and logs) on failure.
inline bool save_sources_registry(
    SourcesRegistry& registry, const std::filesystem::path& registry_path = sources_registry_path())
{
    namespace fs = std::filesystem;
    std::error_code mkdir_ec;
    fs::create_directories(registry_path.parent_path(), mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error("plugin", "could not create {}: {}",
                                       registry_path.parent_path().string(), mkdir_ec.message());
        return false;
    }
    const std::string toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(registry));
    if (!save_scene_file(registry_path.string(), toml))
    {
        mosaic::foundation::log::error("plugin", "could not write {}", registry_path.string());
        return false;
    }
    return true;
}

// Registers `name` -> `path`. Fails (nothing written) if `name` is already registered
// (refuse rather than silently overwrite, matching create_project's "refuse to
// overwrite an existing path" precedent), or if `path` isn't a real, existing local
// directory.
inline bool add_named_source(const std::string& name, const std::string& path,
                             const std::filesystem::path& registry_path = sources_registry_path())
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(path, ec))
    {
        mosaic::foundation::log::error("plugin", "not a directory: {}", path);
        return false;
    }
    SourcesRegistry registry = load_sources_registry(registry_path);
    for (const NamedSource& s : registry.sources)
    {
        if (s.name == name)
        {
            mosaic::foundation::log::error(
                "plugin", "source '{}' is already registered (path: {})", name, s.path);
            return false;
        }
    }
    registry.sources.push_back(NamedSource{name, path});
    return save_sources_registry(registry, registry_path);
}

// Removes `name` if present. Not registered is not an error -- returns true, a no-op
// (matches remove_package's precedent).
inline bool remove_named_source(const std::string& name,
                                const std::filesystem::path& registry_path = sources_registry_path())
{
    SourcesRegistry registry = load_sources_registry(registry_path);
    auto it = std::find_if(registry.sources.begin(), registry.sources.end(),
                           [&](const NamedSource& s) { return s.name == name; });
    if (it == registry.sources.end())
    {
        mosaic::foundation::log::info("plugin", "source '{}' is not registered", name);
        return true;
    }
    registry.sources.erase(it);
    return save_sources_registry(registry, registry_path);
}

// Every registered named source, in registration order.
inline std::vector<NamedSource> list_named_sources(
    const std::filesystem::path& registry_path = sources_registry_path())
{
    return load_sources_registry(registry_path).sources;
}

// Resolves a --source value: an exact match against a registered name wins and
// returns that name's path; anything else is returned unchanged, to be treated as a
// literal filesystem path by the caller.
inline std::string resolve_source(const std::string& value,
                                  const std::filesystem::path& registry_path = sources_registry_path())
{
    for (const NamedSource& s : list_named_sources(registry_path))
    {
        if (s.name == value)
        {
            return s.path;
        }
    }
    return value;
}

} // namespace mosaic::app
