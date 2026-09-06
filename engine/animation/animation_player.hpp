#pragma once

#include "engine/animation/animation_clip.hpp"

namespace mosaic::engine::animation
{
// Attached to an entity to play an animation clip. Reflected, so it serializes with the scene and
// appears in the Inspector. AnimationSystem advances `time` and writes sampled track values into
// the target components each tick.
struct AnimationPlayer
{
    AnimationClip clip;
    float         time    = 0.0f;
    bool          playing = true;
    bool          loop    = true;
    float         speed   = 1.0f;
};

template <class V>
void reflect(V& visitor, AnimationPlayer& p)
{
    visitor.member("clip", p.clip);
    visitor.member("time", p.time);
    visitor.member("playing", p.playing);
    visitor.member("loop", p.loop);
    visitor.member("speed", p.speed);
}
} // namespace mosaic::engine::animation
