#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic build <project> [-o <out>]`: package a project into a standalone bundle,
// embedding this running executable as the bundle's runtime. Returns a process exit code.
int run_build(const Options& options);
} // namespace mosaic::app
