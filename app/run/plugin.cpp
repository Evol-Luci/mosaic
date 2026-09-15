#include "app/run/plugin.hpp"

#include <iostream>

#include "app/plugin/package_install.hpp"
#include "app/plugin/package_source.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
int run_plugin_list(const Options& options)
{
    const std::string source = options.package_source.value_or(default_package_source());
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
    const std::string source = options.package_source.value_or(default_package_source());
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
} // namespace mosaic::app
