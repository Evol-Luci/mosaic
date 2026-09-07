#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/audio/audio_components.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
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

TEST_CASE("AudioSource and AudioListener are registered built-in components", "[audio]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("AudioSource"));
    REQUIRE(types.has("AudioListener"));
}
