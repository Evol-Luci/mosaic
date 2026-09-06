#pragma once

#include <string>
#include <vector>

namespace mosaic::engine::animation
{
// A single (time, value) sample on a track.
struct Keyframe
{
    float  time  = 0.0f;
    double value = 0.0;
};

template <class V>
void reflect(V& visitor, Keyframe& k)
{
    visitor.member("time", k.time);
    visitor.member("value", k.value);
}

// Animates one scalar property, addressed by a reflection property path
// (e.g. "Transform.position.x"). Keyframes are sorted by time (author's responsibility).
struct Track
{
    std::string           target;
    bool                  step = false; // false = linear interpolation, true = hold (step)
    std::vector<Keyframe> keys;
};

template <class V>
void reflect(V& visitor, Track& t)
{
    visitor.member("target", t.target);
    visitor.member("step", t.step);
    visitor.member("keys", t.keys);
}

// A named set of tracks with a duration (used for looping).
struct AnimationClip
{
    std::string        name;
    float              duration = 0.0f;
    std::vector<Track> tracks;
};

template <class V>
void reflect(V& visitor, AnimationClip& c)
{
    visitor.member("name", c.name);
    visitor.member("duration", c.duration);
    visitor.member("tracks", c.tracks);
}

// Sample a track at `time`. Empty track -> 0. Outside [first, last] clamps to the nearest
// key's value. `step` holds the left key's value across the interval; otherwise linear.
inline double sample(const Track& track, float time)
{
    if (track.keys.empty()) { return 0.0; }
    if (time <= track.keys.front().time) { return track.keys.front().value; }
    if (time >= track.keys.back().time) { return track.keys.back().value; }

    // Find the first key strictly after `time`; the pair (prev, k) brackets it.
    for (std::size_t i = 1; i < track.keys.size(); ++i)
    {
        const Keyframe& k = track.keys[i];
        if (time < k.time)
        {
            const Keyframe& prev = track.keys[i - 1];
            if (track.step) { return prev.value; }
            const float span = k.time - prev.time;
            if (span <= 0.0f) { return prev.value; } // coincident keys: hold
            const double alpha = static_cast<double>((time - prev.time) / span);
            return prev.value + (k.value - prev.value) * alpha;
        }
    }
    return track.keys.back().value; // unreachable (time < back handled above)
}
} // namespace mosaic::engine::animation
