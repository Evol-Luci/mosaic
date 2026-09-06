#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/animation/animation_player.hpp"
#include "engine/animation/animation_system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::animation::AnimationPlayer;
using mosaic::engine::animation::AnimationSystem;
using mosaic::engine::animation::set_scalar_at;
using mosaic::engine::animation::Track;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;

TEST_CASE("set_scalar_at writes a nested leaf and rejects bad paths", "[animation]")
{
    Table pos;
    pos["x"] = Value(1.0); pos["y"] = Value(2.0); pos["z"] = Value(3.0);
    Table root;
    root["position"] = Value(std::move(pos));
    Value v(std::move(root));

    REQUIRE(set_scalar_at(v, {"position", "x"}, 9.0));
    REQUIRE(v.as<Table>().find("position")->as<Table>().find("x")->as<double>() == Approx(9.0));

    REQUIRE_FALSE(set_scalar_at(v, {"missing", "x"}, 1.0)); // no such component sub-table
    REQUIRE_FALSE(set_scalar_at(v, {"position", "w"}, 1.0)); // no such leaf key
    REQUIRE_FALSE(set_scalar_at(v, {}, 1.0));                // empty path
}

TEST_CASE("AnimationSystem animates a Transform property over time", "[animation]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<AnimationSystem>(types);

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    AnimationPlayer p;
    p.loop = true;
    p.clip.duration = 1.0f;
    Track t;
    t.target = "Transform.position.x";
    t.keys = {{0.0f, 0.0}, {1.0f, 10.0}};
    p.clip.tracks = {t};
    world.add_component<AnimationPlayer>(e, p);

    world.tick(0.5f); // t = 0.5 -> x = 5
    REQUIRE(world.get_component<Transform>(e)->position.x == Approx(5.0));
    world.tick(0.25f); // t = 0.75 -> x = 7.5
    REQUIRE(world.get_component<Transform>(e)->position.x == Approx(7.5));
}

TEST_CASE("a paused AnimationPlayer does not advance", "[animation]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    world.add_system<AnimationSystem>(types);

    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    AnimationPlayer p;
    p.playing = false;
    p.clip.duration = 1.0f;
    Track t;
    t.target = "Transform.position.x";
    t.keys = {{0.0f, 0.0}, {1.0f, 10.0}};
    p.clip.tracks = {t};
    world.add_component<AnimationPlayer>(e, p);

    world.tick(0.5f);
    REQUIRE(world.get_component<Transform>(e)->position.x == Approx(0.0)); // frozen
}
