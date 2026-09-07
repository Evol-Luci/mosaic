#pragma once

#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::audio
{
using mosaic::foundation::serialization::AssetID;

// A sound emitter attached to an entity. Reflected + serialized; the live playing
// voice lives in AudioSystem, not here.
struct AudioSource
{
    AssetID clip{};          // sound asset (nil = nothing)
    float   gain    = 1.0f;  // linear volume
    float   pitch   = 1.0f;  // playback-rate multiplier
    bool    looping = false;
    bool    playing = false; // authored intent: should this be playing?
    bool    spatial = true;  // positioned at the entity's Transform (3D), vs 2D/UI
};

template <class V>
void reflect(V& visitor, AudioSource& s)
{
    visitor.member("clip", s.clip);
    visitor.member("gain", s.gain);
    visitor.member("pitch", s.pitch);
    visitor.member("looping", s.looping);
    visitor.member("playing", s.playing);
    visitor.member("spatial", s.spatial);
}

// Marker: the entity carrying this (with a Transform) is the audio listener.
struct AudioListener
{
};

template <class V>
void reflect(V& /*visitor*/, AudioListener& /*l*/)
{
}
} // namespace mosaic::engine::audio
