#include <catch2/catch_test_macros.hpp>

#include "engine/animation/animation_clip.hpp"
#include "engine/sequencer/sequence.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::animation::Track;
using mosaic::engine::sequencer::Sequence;
using mosaic::engine::sequencer::SequenceClipTrack;
using mosaic::engine::sequencer::SequenceEventTrack;

TEST_CASE("Sequence round-trips clip + event tracks through reflection", "[sequencer]")
{
    Sequence s;
    s.duration = 3.0f;
    s.loop = true;

    SequenceClipTrack ct;
    ct.target = 42;
    ct.start = 0.5f;
    ct.clip.name = "move";
    ct.clip.duration = 1.0f;
    Track tr;
    tr.target = "Transform.position.x";
    tr.keys = {{0.0f, 0.0}, {1.0f, 10.0}};
    ct.clip.tracks = {tr};
    s.clips = {ct};
    s.events = {SequenceEventTrack{2.0f, "boom"}};

    const auto value = mosaic::foundation::serialization::to_value(s);
    Sequence back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.duration == 3.0f);
    REQUIRE(back.loop == true);
    REQUIRE(back.clips.size() == 1);
    REQUIRE(back.clips[0].target == 42);
    REQUIRE(back.clips[0].start == 0.5f);
    REQUIRE(back.clips[0].clip.tracks.size() == 1);
    REQUIRE(back.clips[0].clip.tracks[0].target == "Transform.position.x");
    REQUIRE(back.events.size() == 1);
    REQUIRE(back.events[0].time == 2.0f);
    REQUIRE(back.events[0].event == "boom");
}
