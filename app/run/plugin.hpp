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
// Handle `mosaic plugin source add <name> <path>`. Returns a process exit code.
int run_plugin_source_add(const Options& options);
// Handle `mosaic plugin source list`. Returns a process exit code.
int run_plugin_source_list(const Options& options);
// Handle `mosaic plugin source remove <name>`. Returns a process exit code.
int run_plugin_source_remove(const Options& options);
} // namespace mosaic::app
