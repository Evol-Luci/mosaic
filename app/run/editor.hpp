#pragma once

#include <memory>

#include "app/cli/options.hpp"
#include "engine/audio/audio_backend.hpp"

namespace mosaic::app
{

// Opens the Studio editor window and runs its frame loop until the window closes,
// File > Exit, --duration/--ticks, or SIGINT. Returns a process exit code.
// `audio_backend` is injected (rather than constructed inside editor.cpp) because
// mosaic_app -- the static library editor.cpp lives in -- deliberately doesn't link
// mosaic_audio_miniaudio; only the final `mosaic` executable (main.cpp) does, the
// same way `mosaic run --window` already constructs its own backend. nullptr means
// silent, matching every other caller's default before this parameter existed.
int run_editor(const Options& options,
               std::unique_ptr<mosaic::engine::audio::AudioBackend> audio_backend = nullptr);

} // namespace mosaic::app
