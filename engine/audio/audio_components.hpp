#pragma once

#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::audio
{
using mosaic::foundation::serialization::AssetID;

// Distance-attenuation model for a spatial AudioSource, mirroring miniaudio's
// ma_attenuation_model 1:1 (see modules/audio_miniaudio/miniaudio_backend.cpp's
// mapping). Serialized as int (the reflection layer supports int, not enum class,
// same convention as engine/particles/particle_emitter.hpp's Easing) -- append new
// values, do not renumber.
enum AttenuationModel : int
{
    AttenuationNone        = 0, // no distance falloff
    AttenuationInverse     = 1, // physically-based falloff, shaped by rolloff/min/max distance
    AttenuationLinear      = 2,
    AttenuationExponential = 3,
};

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
    int     attenuation_model = AttenuationInverse; // AttenuationModel; spatial sources only
    float   min_distance = 1.0f;   // full gain at or below this distance
    float   max_distance = 100.0f; // backend-defined clamp beyond this distance
    float   rolloff      = 1.0f;   // falloff steepness
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
    visitor.member("attenuation_model", s.attenuation_model);
    visitor.member("min_distance", s.min_distance);
    visitor.member("max_distance", s.max_distance);
    visitor.member("rolloff", s.rolloff);
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
