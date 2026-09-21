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
// Handle `mosaic plugin enable <name> <project>`. Returns a process exit code.
int run_plugin_enable(const Options& options);
// Handle `mosaic plugin disable <name> <project>`. Returns a process exit code.
int run_plugin_disable(const Options& options);
// Handle `mosaic plugin updates <project> [--source]`. Returns a process exit code.
int run_plugin_updates(const Options& options);
// Handle `mosaic plugin source add <name> <path|git-url>`. Returns a process exit code.
int run_plugin_source_add(const Options& options);
// Handle `mosaic plugin source list`. Returns a process exit code.
int run_plugin_source_list(const Options& options);
// Handle `mosaic plugin source remove <name>`. Returns a process exit code.
int run_plugin_source_remove(const Options& options);
// Handle `mosaic plugin source pull <name>`. Returns a process exit code.
int run_plugin_source_pull(const Options& options);
} // namespace mosaic::app
