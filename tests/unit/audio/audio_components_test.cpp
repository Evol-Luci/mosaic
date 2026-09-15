#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/audio/audio_components.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::audio::AttenuationExponential;
using mosaic::engine::audio::AttenuationInverse;
using mosaic::engine::audio::AudioSource;

TEST_CASE("AudioSource round-trips through reflection", "[audio]")
{
    AudioSource s;
    s.clip = mosaic::foundation::serialization::AssetID::from_string(
        "00000000-0000-0000-0000-0000000000aa");
    s.gain    = 0.4f;
    s.pitch   = 1.5f;
    s.looping = true;
    s.playing = true;
    s.spatial = false;

    const auto  value = mosaic::foundation::serialization::to_value(s);
    AudioSource back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.clip == s.clip);
    REQUIRE(back.gain == Approx(0.4f));
    REQUIRE(back.pitch == Approx(1.5f));
    REQUIRE(back.looping == true);
    REQUIRE(back.playing == true);
    REQUIRE(back.spatial == false);
}

TEST_CASE("AudioSource defaults to inverse attenuation with sane distances", "[audio]")
{
    AudioSource s;
    REQUIRE(s.attenuation_model == AttenuationInverse);
    REQUIRE(s.min_distance == Approx(1.0f));
    REQUIRE(s.max_distance == Approx(100.0f));
    REQUIRE(s.rolloff == Approx(1.0f));
}

TEST_CASE("AudioSource attenuation fields round-trip through reflection", "[audio]")
{
    AudioSource s;
    s.attenuation_model = AttenuationExponential;
    s.min_distance       = 2.5f;
    s.max_distance        = 50.0f;
    s.rolloff             = 3.0f;

    const auto  value = mosaic::foundation::serialization::to_value(s);
    AudioSource back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.attenuation_model == AttenuationExponential);
    REQUIRE(back.min_distance == Approx(2.5f));
    REQUIRE(back.max_distance == Approx(50.0f));
    REQUIRE(back.rolloff == Approx(3.0f));
}

TEST_CASE("AudioSource and AudioListener are registered built-in components", "[audio]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("AudioSource"));
    REQUIRE(types.has("AudioListener"));
}
