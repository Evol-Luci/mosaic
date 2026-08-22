#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{

// Opens the Studio editor window and runs its frame loop until the window closes,
// File > Exit, --duration/--ticks, or SIGINT. Returns a process exit code.
int run_editor(const Options& options);

} // namespace mosaic::app
