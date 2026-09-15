#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic docs`: generate docs/reference/components.md from the
// built-in component registry. Returns a process exit code.
int run_docs(const Options& options);
} // namespace mosaic::app
