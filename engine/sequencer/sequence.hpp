#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/animation/animation_clip.hpp"

namespace mosaic::engine::sequencer
{
// An animation clip bound to a target entity, starting at `start` on the master timeline.
struct SequenceClipTrack
{
    std::uint64_t                     target = 0; // target entity id
    float                             start  = 0.0f;
    mosaic::engine::animation::AnimationClip clip;
};

template <class V>
void reflect(V& visitor, SequenceClipTrack& t)
{
    visitor.member("target", t.target);
    visitor.member("start", t.start);
    visitor.member("clip", t.clip);
}

// A timeline marker: emits a named script event once, when the timeline crosses `time`.
struct SequenceEventTrack
{
    float       time = 0.0f;
    std::string event; // ScriptEvent name (empty payload)
};

template <class V>
void reflect(V& visitor, SequenceEventTrack& t)
{
    visitor.member("time", t.time);
    visitor.member("event", t.event);
}

// A director timeline on an entity: drives clip tracks (any target entity) off one master clock
// and fires event markers. Reflected, so it serializes with the scene.
struct Sequence
{
    float                           time     = 0.0f;
    float                           duration = 0.0f;
    bool                            playing  = true;
    bool                            loop     = false; // cutscenes usually play once
    std::vector<SequenceClipTrack>  clips;
    std::vector<SequenceEventTrack> events;
};

template <class V>
void reflect(V& visitor, Sequence& s)
{
    visitor.member("time", s.time);
    visitor.member("duration", s.duration);
    visitor.member("playing", s.playing);
    visitor.member("loop", s.loop);
    visitor.member("clips", s.clips);
    visitor.member("events", s.events);
}
} // namespace mosaic::engine::sequencer
