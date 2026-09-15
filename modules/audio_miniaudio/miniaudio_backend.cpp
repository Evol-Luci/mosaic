#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "engine/audio/audio_components.hpp" // AttenuationModel
#include "modules/audio_miniaudio/miniaudio_backend.hpp"

namespace mosaic::audio::miniaudio
{
namespace
{
using mosaic::engine::audio::AttenuationExponential;
using mosaic::engine::audio::AttenuationInverse;
using mosaic::engine::audio::AttenuationLinear;
using mosaic::engine::audio::AttenuationNone;
using mosaic::engine::audio::AudioBackend;
using mosaic::engine::audio::ListenerState;
using mosaic::engine::audio::VoiceId;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

// audio::AttenuationModel (int) -> miniaudio's own enum. Values were chosen to
// match ma_attenuation_model 1:1 (see engine/audio/audio_components.hpp), but this
// explicit mapping is the single place that contract is load-bearing -- an unknown
// value falls back to the engine's own default (inverse), never undefined behavior.
ma_attenuation_model to_ma_attenuation_model(int model)
{
    switch (model)
    {
        case AttenuationNone:        return ma_attenuation_model_none;
        case AttenuationLinear:      return ma_attenuation_model_linear;
        case AttenuationExponential: return ma_attenuation_model_exponential;
        case AttenuationInverse:
        default:                     return ma_attenuation_model_inverse;
    }
}

class MiniaudioBackend : public AudioBackend
{
public:
    explicit MiniaudioBackend(bool null_device)
    {
        ma_context_config ctxConfig    = ma_context_config_init();
        ma_backend        nullList[]   = {ma_backend_null};
        ma_backend*       backends     = null_device ? nullList : nullptr;
        ma_uint32         backendCount = null_device ? 1u : 0u;
        if (ma_context_init(backends, backendCount, &ctxConfig, &context_) != MA_SUCCESS)
        {
            return;
        }
        context_ok_ = true;

        ma_engine_config engConfig = ma_engine_config_init();
        engConfig.pContext         = &context_;
        if (ma_engine_init(&engConfig, &engine_) != MA_SUCCESS) { return; }
        engine_ok_ = true;
    }

    ~MiniaudioBackend() override
    {
        for (auto& [id, v] : voices_)
        {
            ma_sound_uninit(&v->sound);
            ma_audio_buffer_uninit(&v->buffer);
        }
        voices_.clear();
        if (engine_ok_) { ma_engine_uninit(&engine_); }
        if (context_ok_) { ma_context_uninit(&context_); }
    }

    bool ok() const { return engine_ok_; }

    bool register_clip(AssetID clip, const float* interleaved, std::uint64_t frames,
                       std::uint32_t channels, std::uint32_t sample_rate) override
    {
        if (interleaved == nullptr || frames == 0 || channels == 0 || sample_rate == 0)
        {
            return false;
        }
        Clip c;
        c.channels    = channels;
        c.sample_rate = sample_rate;
        c.frames      = frames;
        c.pcm.assign(interleaved,
                     interleaved + static_cast<std::size_t>(frames) * channels);
        clips_[clip] = std::move(c);
        return true;
    }

    VoiceId play(AssetID clip, float gain, float pitch, bool looping, bool spatial) override
    {
        if (!engine_ok_) { return 0; }
        auto it = clips_.find(clip);
        if (it == clips_.end()) { return 0; }
        const Clip& c = it->second;

        auto v = std::make_unique<Voice>();
        ma_audio_buffer_config bufConfig =
            ma_audio_buffer_config_init(ma_format_f32, c.channels, c.frames, c.pcm.data(), nullptr);
        bufConfig.sampleRate = c.sample_rate;
        if (ma_audio_buffer_init(&bufConfig, &v->buffer) != MA_SUCCESS) { return 0; }

        if (ma_sound_init_from_data_source(&engine_, &v->buffer, 0, nullptr, &v->sound) != MA_SUCCESS)
        {
            ma_audio_buffer_uninit(&v->buffer);
            return 0;
        }
        ma_sound_set_volume(&v->sound, gain);
        ma_sound_set_pitch(&v->sound, pitch);
        ma_sound_set_looping(&v->sound, looping ? MA_TRUE : MA_FALSE);
        ma_sound_set_spatialization_enabled(&v->sound, spatial ? MA_TRUE : MA_FALSE);
        ma_sound_start(&v->sound);

        const VoiceId id = next_voice_++;
        voices_.emplace(id, std::move(v));
        return id;
    }

    void stop(VoiceId voice) override
    {
        auto it = voices_.find(voice);
        if (it == voices_.end()) { return; }
        ma_sound_stop(&it->second->sound);
        ma_sound_uninit(&it->second->sound);
        ma_audio_buffer_uninit(&it->second->buffer);
        voices_.erase(it);
    }

    void set_gain(VoiceId voice, float gain) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_volume(&v->sound, gain); }
    }

    void set_pitch(VoiceId voice, float pitch) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_pitch(&v->sound, pitch); }
    }

    void set_voice_position(VoiceId voice, const Vec3& p) override
    {
        if (Voice* v = find(voice)) { ma_sound_set_position(&v->sound, p.x, p.y, p.z); }
    }

    bool is_playing(VoiceId voice) const override
    {
        auto it = voices_.find(voice);
        return it != voices_.end() && ma_sound_is_playing(&it->second->sound) == MA_TRUE;
    }

    void set_attenuation(VoiceId voice, int model, float min_distance, float max_distance,
                         float rolloff) override
    {
        if (Voice* v = find(voice))
        {
            ma_sound_set_attenuation_model(&v->sound, to_ma_attenuation_model(model));
            ma_sound_set_min_distance(&v->sound, min_distance);
            ma_sound_set_max_distance(&v->sound, max_distance);
            ma_sound_set_rolloff(&v->sound, rolloff);
        }
    }

    void set_listener(const ListenerState& l) override
    {
        if (!engine_ok_) { return; }
        ma_engine_listener_set_position(&engine_, 0, l.position.x, l.position.y, l.position.z);
        ma_engine_listener_set_direction(&engine_, 0, l.forward.x, l.forward.y, l.forward.z);
        ma_engine_listener_set_world_up(&engine_, 0, l.up.x, l.up.y, l.up.z);
    }

private:
    struct Clip
    {
        std::vector<float> pcm;
        ma_uint32          channels    = 0;
        ma_uint32          sample_rate = 0;
        ma_uint64          frames      = 0;
    };
    struct Voice
    {
        ma_audio_buffer buffer{};
        ma_sound        sound{};
    };
    Voice* find(VoiceId voice)
    {
        auto it = voices_.find(voice);
        return it == voices_.end() ? nullptr : it->second.get();
    }

    ma_context context_{};
    ma_engine  engine_{};
    bool       context_ok_ = false;
    bool       engine_ok_  = false;
    VoiceId    next_voice_ = 1;
    std::unordered_map<AssetID, Clip>                   clips_;
    std::unordered_map<VoiceId, std::unique_ptr<Voice>> voices_;
};
} // namespace

std::unique_ptr<mosaic::engine::audio::AudioBackend> create_miniaudio_backend(bool null_device)
{
    auto backend = std::make_unique<MiniaudioBackend>(null_device);
    if (!backend->ok()) { return nullptr; }
    return backend;
}
} // namespace mosaic::audio::miniaudio
