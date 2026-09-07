#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "engine/audio/audio_backend.hpp"
#include "engine/audio/audio_components.hpp"
#include "engine/audio/audio_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::audio::AudioBackend;
using mosaic::engine::audio::AudioListener;
using mosaic::engine::audio::AudioSource;
using mosaic::engine::audio::AudioSystem;
using mosaic::engine::audio::ListenerState;
using mosaic::engine::audio::VoiceId;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::serialization::AssetID;

// Records every backend call so tests can assert what the AudioSystem drove.
class FakeAudioBackend : public AudioBackend
{
public:
    struct PlayCall { AssetID clip; float gain; float pitch; bool looping; };
    std::vector<PlayCall>                 plays;
    std::vector<VoiceId>                  stops;
    std::vector<std::pair<VoiceId, float>> gains;
    std::vector<std::pair<VoiceId, float>> pitches;
    std::vector<std::pair<VoiceId, Vec3>>  positions;
    bool                                   listener_set = false;
    ListenerState                          listener{};

    VoiceId play(AssetID clip, float gain, float pitch, bool looping) override
    {
        plays.push_back(PlayCall{clip, gain, pitch, looping});
        return next_voice_++;
    }
    void stop(VoiceId voice) override { stops.push_back(voice); }
    void set_gain(VoiceId voice, float gain) override { gains.emplace_back(voice, gain); }
    void set_pitch(VoiceId voice, float pitch) override { pitches.emplace_back(voice, pitch); }
    void set_voice_position(VoiceId voice, const Vec3& position) override
    {
        positions.emplace_back(voice, position);
    }
    bool is_playing(VoiceId) const override { return true; }
    void set_listener(const ListenerState& l) override { listener_set = true; listener = l; }

private:
    VoiceId next_voice_ = 1;
};

TEST_CASE("AudioSystem plays a source on the start edge", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s;
    s.playing = true;
    s.gain    = 0.5f;
    s.pitch   = 1.2f;
    s.looping = true;
    s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    REQUIRE(fake->plays.size() == 1);
    REQUIRE(fake->plays[0].gain == Approx(0.5f));
    REQUIRE(fake->plays[0].pitch == Approx(1.2f));
    REQUIRE(fake->plays[0].looping == true);
}

TEST_CASE("AudioSystem does not replay while playing stays true", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s; s.playing = true; s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    world.tick(0.0f);
    world.tick(0.0f);
    REQUIRE(fake->plays.size() == 1); // one start edge, no repeats
}

TEST_CASE("AudioSystem stops a source on the stop edge", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s; s.playing = true; s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    world.get_component<AudioSource>(e)->playing = false;
    world.tick(0.0f);
    REQUIRE(fake->stops.size() == 1);
}

TEST_CASE("AudioSystem pushes a spatial source's position each active tick", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    Transform tf;
    tf.position = Vec3{1.0f, 2.0f, 3.0f};
    world.add_component<Transform>(e, tf);
    AudioSource s; s.playing = true; s.spatial = true;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    REQUIRE(fake->positions.size() >= 1);
    REQUIRE(fake->positions.back().second.x == Approx(1.0f));
    REQUIRE(fake->positions.back().second.y == Approx(2.0f));
    REQUIRE(fake->positions.back().second.z == Approx(3.0f));
}

TEST_CASE("AudioSystem does not position a non-spatial source", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{1.0f, 2.0f, 3.0f};
    world.add_component<Transform>(e, tf);
    AudioSource s; s.playing = true; s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    REQUIRE(fake->positions.empty());
}

TEST_CASE("AudioSystem pushes a gain change while playing", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s; s.playing = true; s.spatial = false; s.gain = 1.0f;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f);
    world.get_component<AudioSource>(e)->gain = 0.25f;
    world.tick(0.0f);
    REQUIRE(fake->gains.size() == 1);
    REQUIRE(fake->gains.back().second == Approx(0.25f));
}

TEST_CASE("AudioSystem sets the listener from an AudioListener entity's Transform", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    Transform tf; tf.position = Vec3{4.0f, 5.0f, 6.0f};
    world.add_component<Transform>(e, tf);
    world.add_component<AudioListener>(e, AudioListener{});

    world.tick(0.0f);
    REQUIRE(fake->listener_set == true);
    REQUIRE(fake->listener.position.x == Approx(4.0f));
    REQUIRE(fake->listener.position.y == Approx(5.0f));
    REQUIRE(fake->listener.position.z == Approx(6.0f));
}

TEST_CASE("AudioSystem stops a voice when its entity is destroyed", "[audio]")
{
    World world;
    auto  backend = std::make_unique<FakeAudioBackend>();
    auto* fake    = backend.get();
    world.add_system<AudioSystem>(std::move(backend));

    const auto e = world.create_entity();
    AudioSource s; s.playing = true; s.spatial = false;
    world.add_component<AudioSource>(e, s);

    world.tick(0.0f); // start the voice
    world.destroy_entity(e);
    world.tick(0.0f); // systems still see e this tick; e is destroyed at end of tick
    world.tick(0.0f); // e is gone now -> prune stops its voice
    REQUIRE(fake->stops.size() == 1);
}
