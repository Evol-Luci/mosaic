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
    bool playing_        = true;
    bool step_requested_ = false;
};

} // namespace mosaic::studio
