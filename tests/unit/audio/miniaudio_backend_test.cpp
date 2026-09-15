#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "engine/audio/audio_components.hpp"
#include "engine/audio/audio_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/reference.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"

using mosaic::audio::miniaudio::create_miniaudio_backend;
using mosaic::engine::World;
using mosaic::engine::audio::AudioBackend;
using mosaic::engine::audio::AudioSource;
using mosaic::engine::audio::AudioSystem;
using mosaic::engine::audio::VoiceId;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

namespace
{
// A short generated stereo f32 clip so tests have real PCM to play.
std::vector<float> sine_pcm(std::uint32_t channels, std::uint64_t frames)
{
    std::vector<float> pcm(static_cast<std::size_t>(frames) * channels);
    for (std::uint64_t i = 0; i < frames; ++i)
    {
        const float s = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
        for (std::uint32_t ch = 0; ch < channels; ++ch) { pcm[i * channels + ch] = s; }
    }
    return pcm;
}
const AssetID kClip = AssetID::from_string("00000000-0000-0000-0000-0000000000a1");
}

TEST_CASE("miniaudio backend initializes on the null device", "[audio]")
{
    auto backend = create_miniaudio_backend(/*null_device=*/true);
    REQUIRE(backend != nullptr);
}

TEST_CASE("miniaudio backend plays a registered clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    const auto pcm = sine_pcm(2, 12000);
    REQUIRE(backend->register_clip(kClip, pcm.data(), 12000, 2, 48000));

    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(v != 0);
    REQUIRE(backend->is_playing(v));
}

TEST_CASE("miniaudio backend returns no voice for an unregistered clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    const AssetID unknown = AssetID::from_string("00000000-0000-0000-0000-0000000000ff");
    REQUIRE(backend->play(unknown, 1.0f, 1.0f, false, true) == 0);
}

TEST_CASE("miniaudio backend stops a voice", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(backend->is_playing(v));
    backend->stop(v);
    REQUIRE_FALSE(backend->is_playing(v));
}

TEST_CASE("miniaudio backend voice ops and unknown-voice calls are safe", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    const VoiceId v = backend->play(kClip, 1.0f, 1.0f, false, true);

    backend->set_gain(v, 0.5f);
    backend->set_pitch(v, 1.5f);
    backend->set_voice_position(v, Vec3{1.0f, 2.0f, 3.0f});
    backend->set_attenuation(v, mosaic::engine::audio::AttenuationLinear, 2.0f, 50.0f, 1.5f);

    // Unknown / zero voice must be safe.
    backend->set_gain(0, 0.5f);
    backend->set_attenuation(0, mosaic::engine::audio::AttenuationInverse, 1.0f, 100.0f, 1.0f);
    backend->stop(0);
    REQUIRE_FALSE(backend->is_playing(0));
    REQUIRE_FALSE(backend->is_playing(999999));
}

TEST_CASE("miniaudio backend plays two independent voices of one clip", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);

    const VoiceId a = backend->play(kClip, 1.0f, 1.0f, false, true);
    const VoiceId b = backend->play(kClip, 1.0f, 1.0f, false, true);
    REQUIRE(a != 0);
    REQUIRE(b != 0);
    REQUIRE(a != b);
    REQUIRE(backend->is_playing(a));
    REQUIRE(backend->is_playing(b));
    backend->stop(a);
    REQUIRE_FALSE(backend->is_playing(a));
    REQUIRE(backend->is_playing(b));
}

TEST_CASE("miniaudio backend rejects invalid clip data", "[audio]")
{
    auto backend = create_miniaudio_backend(true);
    const auto pcm = sine_pcm(2, 4);
    REQUIRE_FALSE(backend->register_clip(kClip, nullptr, 4, 2, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 0, 2, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 4, 0, 48000));
    REQUIRE_FALSE(backend->register_clip(kClip, pcm.data(), 4, 2, 0));
}

TEST_CASE("AudioSystem drives the real miniaudio backend", "[audio]")
{
    World world;
    auto  backend = create_miniaudio_backend(true);
    REQUIRE(backend != nullptr);
    auto* raw = backend.get(); // stays valid after the move (the system owns it, same object)
    const auto pcm = sine_pcm(2, 12000);
    backend->register_clip(kClip, pcm.data(), 12000, 2, 48000);
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s;
    s.clip    = kClip;
    s.playing = true;
    s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f); // start edge -> backend->play assigns the first voice id (1)
    REQUIRE(raw->is_playing(1)); // the system actually started a voice on the real backend
}
