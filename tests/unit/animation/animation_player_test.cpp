#include <catch2/catch_test_macros.hpp>

#include "engine/animation/animation_player.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::animation::AnimationPlayer;
using mosaic::engine::animation::Track;

TEST_CASE("AnimationPlayer round-trips a multi-track clip through reflection", "[animation]")
{
    AnimationPlayer p;
    p.time = 0.25f;
    p.playing = false;
    p.loop = false;
    p.speed = 2.0f;
    p.clip.name = "spin";
    p.clip.duration = 1.0f;

    Track t0;
    t0.target = "Transform.position.x";
    t0.keys = {{0.0f, 0.0}, {1.0f, 10.0}};
    Track t1;
    t1.target = "Transform.position.y";
    t1.step = true;
    t1.keys = {{0.0f, 1.0}};
    p.clip.tracks = {t0, t1};

    const auto value = mosaic::foundation::serialization::to_value(p);
    AnimationPlayer back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.playing == false);
    REQUIRE(back.speed == 2.0f);
    REQUIRE(back.clip.name == "spin");
    REQUIRE(back.clip.duration == 1.0f);
    REQUIRE(back.clip.tracks.size() == 2);
    REQUIRE(back.clip.tracks[0].target == "Transform.position.x");
    REQUIRE(back.clip.tracks[0].keys.size() == 2);
    REQUIRE(back.clip.tracks[0].keys[1].value == 10.0);
    REQUIRE(back.clip.tracks[1].step == true);
    REQUIRE(back.clip.tracks[1].keys.size() == 1);
}
