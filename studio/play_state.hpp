#pragma once

namespace mosaic::studio
{

// The editor's simulation run-state. The toolbar mutates it; the editor frame loop
// reads it each frame to drive the RuntimeLoop. Default is playing (the viewport is
// live on launch, matching 37d). stop() is a pause: it does not reset the world.
class PlayState
{
public:
    bool is_playing() const { return playing_; }
    void play() { playing_ = true; }
    void stop() { playing_ = false; }
    void toggle() { playing_ = !playing_; }

    // Queue a single tick, honored while paused (RuntimeLoop::step). One-shot.
    void request_step() { step_requested_ = true; }
    bool consume_step_request()
    {
        const bool requested = step_requested_;
        step_requested_ = false;
        return requested;
    }

private:
    // STOPPED by default: Mosaic is editor-facing, so opening the editor puts the user in
    // authoring mode, where a viewport click selects an object instead of driving the game.
    // `mosaic editor --ticks N` overrides this at startup -- see
    // app/run/editor_play_policy.hpp for why it must.
    bool playing_        = false;
    bool step_requested_ = false;
};

} // namespace mosaic::studio
