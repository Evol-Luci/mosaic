#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic import <project> <file>`: cook the asset into the project. Returns an exit code.
int run_import(const Options& options);
} // namespace mosaic::app
