#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{

// Whether `mosaic editor` should begin in play mode rather than its default of stopped.
//
// The editor starts STOPPED, because Mosaic is editor-facing: a human who opens it gets
// authoring mode, and viewport clicks select objects rather than driving the game.
//
// That default breaks one thing, and this function is the exception that repairs it.
// `app/run/editor.cpp` calls `loop.stop()` every frame while stopped, and
// `RuntimeLoop::advance` returns 0 without touching `tick_count_` while stopped -- so
// `FramePump`'s `tick_count() >= *options.ticks` bound can never fire and
// `mosaic editor --ticks N` would run forever. Not fail: HANG, presenting as a frozen
// editor rather than an error.
//
// A tick-bounded run is non-interactive by definition, so reading the flag as "simulate N
// ticks, then exit" is the only sensible interpretation, and it keeps `--ticks` meaning
// exactly the same thing under `run` and under `editor`.
//
// Deliberately NOT "any bound implies play". `--duration` is keyed on real elapsed time
// and terminates correctly with the loop stopped, which makes it the way to bound an
// editor run that must actually observe the stopped default -- the only such escape hatch,
// so widening this policy would close it.
//
// A pure function rather than a line inside the frame loop because that loop needs GLFW, a
// window and a device: the policy would be untestable there, and "the editor hangs under
// --ticks" is precisely the kind of defect that no test could have caught.
inline bool should_start_playing(const Options& options)
{
    // has_value, not a truthiness test: `--ticks 0` is a real bound that FramePump
    // satisfies on its first check, and treating it as "no bound" would reintroduce the
    // hang for that one value.
    return options.ticks.has_value();
}

} // namespace mosaic::app
