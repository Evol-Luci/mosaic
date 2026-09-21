#include "app/run/plugin.hpp"

#include <iostream>

#include "app/plugin/git.hpp"
#include "app/plugin/package_install.hpp"
#include "app/plugin/package_source.hpp"
#include "app/plugin/package_sources_registry.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
namespace
{
// A --source value goes through the named-source registry first (an exact name
// match wins), falling back to the value as a literal path; no --source at all
// means the compile-time bundled packages/.
std::string resolve_package_source(const std::optional<std::string>& raw)
{
    return raw ? resolve_source(*raw) : default_package_source();
}
} // namespace

int run_plugin_list(const Options& options)
{
    const std::string source = resolve_package_source(options.package_source);
    const std::vector<PackageInfo> packages = list_packages(source);
    if (packages.empty())
    {
        mosaic::foundation::log::info("plugin", "no packages found in {}", source);
        return 0;
    }
    for (const PackageInfo& p : packages)
    {
        std::cout << p.name << " " << p.version << " - " << p.description << "\n";
    }
    return 0;
}

int run_plugin_install(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin install <name> <project>");
        return 1;
    }
    const std::string source = resolve_package_source(options.package_source);
    const std::optional<PackageInfo> pkg = find_package(source, *options.plugin_name);
    if (!pkg)
    {
        mosaic::foundation::log::error(
            "plugin", "package '{}' not found in {}", *options.plugin_name, source);
        return 1;
    }
    return install_package(*options.plugin_project, *pkg) ? 0 : 1;
}

int run_plugin_remove(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin remove <name> <project>");
        return 1;
    }
    return remove_package(*options.plugin_project, *options.plugin_name) ? 0 : 1;
}

int run_plugin_enable(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin enable <name> <project>");
        return 1;
    }
    return set_plugin_enabled(*options.plugin_project, *options.plugin_name, true) ? 0 : 1;
}

int run_plugin_disable(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin disable <name> <project>");
        return 1;
    }
    return set_plugin_enabled(*options.plugin_project, *options.plugin_name, false) ? 0 : 1;
}

int run_plugin_updates(const Options& options)
{
    if (!options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin updates <project>");
        return 1;
    }
    const std::string source = resolve_package_source(options.package_source);
    const std::vector<PluginUpdate> updates = available_plugin_updates(*options.plugin_project, source);
    if (updates.empty())
    {
        mosaic::foundation::log::info("plugin", "all installed plugins are up to date");
        return 0;
    }
    for (const PluginUpdate& u : updates)
    {
        std::cout << u.name << " " << u.installed_version << " -> " << u.available_version << "\n";
    }
    return 0;
}

int run_plugin_source_add(const Options& options)
{
    if (!options.source_name || !options.source_path)
    {
        mosaic::foundation::log::error(
            "plugin", "usage: mosaic plugin source add <name> <path-or-git-url>");
        return 1;
    }
    if (is_git_url(*options.source_path))
    {
        return add_named_source_from_git(*options.source_name, *options.source_path) ? 0 : 1;
    }
    return add_named_source(*options.source_name, *options.source_path) ? 0 : 1;
}

int run_plugin_source_list(const Options&)
{
    const std::vector<NamedSource> sources = list_named_sources();
    if (sources.empty())
    {
        mosaic::foundation::log::info("plugin", "no package sources registered");
        return 0;
    }
    for (const NamedSource& s : sources)
    {
        std::cout << s.name << " -> " << s.path;
        if (!s.git_url.empty())
        {
            std::cout << " (git: " << s.git_url << ")";
        }
        std::cout << "\n";
    }
    return 0;
}

int run_plugin_source_remove(const Options& options)
{
    if (!options.source_name)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin source remove <name>");
        return 1;
    }
    return remove_named_source(*options.source_name) ? 0 : 1;
}

int run_plugin_source_pull(const Options& options)
{
    if (!options.source_name)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin source pull <name>");
        return 1;
    }
    return pull_named_source(*options.source_name) ? 0 : 1;
}
} // namespace mosaic::app
