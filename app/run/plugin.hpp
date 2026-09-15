#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic plugin list`. Returns a process exit code.
int run_plugin_list(const Options& options);
// Handle `mosaic plugin install <name> <project>`. Returns a process exit code.
int run_plugin_install(const Options& options);
// Handle `mosaic plugin remove <name> <project>`. Returns a process exit code.
int run_plugin_remove(const Options& options);
} // namespace mosaic::app
