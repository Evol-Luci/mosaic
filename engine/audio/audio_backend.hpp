#pragma once

#include <cstdint>

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::audio
{
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

// Handle to a playing sound instance. 0 = none/invalid.
using VoiceId = std::uint64_t;

// Listener (the "ears") pose for spatialization.
struct ListenerState
{
    Vec3 position{};
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

// Neutral audio backend. A device module (miniaudio/OpenAL/...) implements this
// later; A1 ships only a recording fake used in tests. Concrete device types never
// leak past this wall.
class AudioBackend
{
public:
    virtual ~AudioBackend() = default;
    virtual VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial) = 0;
    // Register decoded interleaved f32 PCM for a clip id so play(clip,...) can use it.
    // Returns false on invalid data. Re-registering an id replaces it.
    virtual bool register_clip(AssetID clip, const float* interleaved, std::uint64_t frames,
                               std::uint32_t channels, std::uint32_t sample_rate) = 0;
    virtual void    stop(VoiceId voice) = 0;
    virtual void    set_gain(VoiceId voice, float gain) = 0;
    virtual void    set_pitch(VoiceId voice, float pitch) = 0;
    virtual void    set_voice_position(VoiceId voice, const Vec3& position) = 0;
    virtual bool    is_playing(VoiceId voice) const = 0;
    virtual void    set_listener(const ListenerState& listener) = 0;
};
} // namespace mosaic::engine::audio
