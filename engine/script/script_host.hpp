#pragma once

#include <functional>

namespace mosaic::engine::script
{
// Host hooks a script may call to drive the run-state. Callbacks are optional:
// an unset (empty) function is a silent no-op. Supplied by the host
// (runtime/editor); ScriptSystem binds them into every engine.
struct ScriptHost
{
    std::function<void(bool)> set_playing;  // play(true) / pause(false)
    std::function<void()>     request_stop; // end the run
};
} // namespace mosaic::engine::script
