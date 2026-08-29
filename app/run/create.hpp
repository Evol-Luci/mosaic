#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic create <name>`: scaffold a project directory. Returns a process exit code.
int run_create(const Options& options);
} // namespace mosaic::app
