#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/animation/animation_clip.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_system.hpp"
#include "engine/sequencer/sequence.hpp"
#include "engine/sequencer/sequence_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::animation::Track;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::script::ScriptSystem;
using mosaic::engine::sequencer::Sequence;
using mosaic::engine::sequencer::SequenceClipTrack;
using mosaic::engine::sequencer::SequenceEventTrack;
using mosaic::engine::sequencer::SequenceSystem;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace
{
SequenceClipTrack move_x_clip(std::uint64_t target, float start)
{
    SequenceClipTrack ct;
    ct.target = target;
    ct.start = start;
    ct.clip.duration = 1.0f;
    Track tr;
    tr.target = "Transform.position.x";
    tr.keys = {{0.0f, 0.0}, {1.0f, 10.0}};
    ct.clip.tracks = {tr};
    return ct;
}
} // namespace

TEST_CASE("SequenceSystem drives a clip onto a target entity", "[sequencer]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<SequenceSystem>(types);

    const auto target = world.create_entity();
    world.add_component<Transform>(target, Transform{});

    const auto director = world.create_entity();
    Sequence seq;
    seq.duration = 2.0f;
    seq.clips = {move_x_clip(target.value(), 0.0f)};
    world.add_component<Sequence>(director, seq);

    world.tick(0.5f); // master t=0.5, clip local=0.5 -> x=5 on the TARGET
    REQUIRE(world.get_component<Transform>(target)->position.x == Approx(5.0));
}

TEST_CASE("SequenceSystem clip track with a start offset is inactive early", "[sequencer]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<SequenceSystem>(types);

    const auto target = world.create_entity();
    world.add_component<Transform>(target, Transform{});

    const auto director = world.create_entity();
    Sequence seq;
    seq.duration = 3.0f;
    seq.clips = {move_x_clip(target.value(), 1.0f)}; // starts at master t=1.0
    world.add_component<Sequence>(director, seq);

    world.tick(0.5f); // master t=0.5 < start 1.0 -> local negative -> inactive
    REQUIRE(world.get_component<Transform>(target)->position.x == Approx(0.0));
}

TEST_CASE("SequenceSystem event marker fires a script event once", "[sequencer]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    world.add_system<SequenceSystem>(types);

    // A scripted probe that counts 'boom' markers via on_event.
    const auto probe = world.create_entity();
    world.add_component<Transform>(probe, Transform{});
    world.add_component<ScriptComponent>(probe, ScriptComponent{
        "def on_update(dt):\n"
        "    pass\n"
        "def on_event(name, payload):\n"
        "    if name == 'boom':\n"
        "        t = get_component(entity, 'Transform')\n"
        "        t['position']['x'] += 1.0\n"
        "        set_component(entity, 'Transform', t)\n"});

    const auto director = world.create_entity();
    Sequence seq;
    seq.duration = 5.0f;
    seq.events = {SequenceEventTrack{0.3f, "boom"}};
    world.add_component<Sequence>(director, seq);

    // t=0.2,0.4,... crosses 0.3 on tick 2; delivered to on_event one tick later.
    for (int i = 0; i < 5; ++i) { world.tick(0.2f); }
    REQUIRE(world.get_component<Transform>(probe)->position.x == Approx(1.0)); // fired exactly once
}
