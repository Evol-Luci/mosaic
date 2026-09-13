#include <catch2/catch_test_macros.hpp>

#include "engine/transform/transform.hpp"
#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "studio/live_link.hpp"

using mosaic::engine::Name;
using mosaic::engine::World;
using mosaic::engine::transform::Transform;
using mosaic::studio::apply_live_link_update;
using mosaic::studio::LiveLinkUpdate;
using mosaic::studio::parse_live_link_message;

TEST_CASE("parse_live_link_message parses a valid line", "[studio]")
{
    const auto update = parse_live_link_message(
        R"({"name":"Cube","position":[1.0,2.0,3.0],"rotation":[0.0,0.0,0.0,1.0],"scale":[1.0,1.0,1.0]})");
    REQUIRE(update.has_value());
    REQUIRE(update->name == "Cube");
    REQUIRE(update->position.x == 1.0f);
    REQUIRE(update->position.y == 2.0f);
    REQUIRE(update->position.z == 3.0f);
    REQUIRE(update->rotation.w == 1.0f);
    REQUIRE(update->scale.x == 1.0f);
}

TEST_CASE("parse_live_link_message accepts a mix of int and float literals", "[studio]")
{
    const auto update = parse_live_link_message(
        R"({"name":"Cube","position":[1,2.5,3],"rotation":[0,0,0,1],"scale":[1,1,1]})");
    REQUIRE(update.has_value());
    REQUIRE(update->position.x == 1.0f);
    REQUIRE(update->position.y == 2.5f);
    REQUIRE(update->rotation.w == 1.0f);
}

TEST_CASE("parse_live_link_message rejects malformed JSON", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message("not json").has_value());
}

TEST_CASE("parse_live_link_message rejects a non-object root", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message("[1,2,3]").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing name", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"position":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing position", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","rotation":[0,0,0,1],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing rotation", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","position":[1,2,3],"scale":[1,1,1]})").has_value());
}

TEST_CASE("parse_live_link_message rejects a missing scale", "[studio]")
{
    REQUIRE_FALSE(parse_live_link_message(
        R"({"name":"Cube","position":[1,2,3],"rotation":[0,0,0,1]})").has_value());
}

TEST_CASE("apply_live_link_update overwrites the matching entity's Transform", "[studio]")
{
    World world;
    const auto e = world.create_entity();
    world.add_component<Name>(e, Name{"Cube"});
    world.add_component<Transform>(e, Transform{});

    LiveLinkUpdate update;
    update.name = "Cube";
    update.position = {1.0f, 2.0f, 3.0f};
    update.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    update.scale = {2.0f, 2.0f, 2.0f};

    REQUIRE(apply_live_link_update(world, update));

    const Transform* t = world.get_component<Transform>(e);
    REQUIRE(t->position.x == 1.0f);
    REQUIRE(t->position.y == 2.0f);
    REQUIRE(t->position.z == 3.0f);
    REQUIRE(t->scale.x == 2.0f);
}

TEST_CASE("apply_live_link_update returns false when no entity matches", "[studio]")
{
    World world;
    const auto e = world.create_entity();
    world.add_component<Name>(e, Name{"Cube"});
    Transform before;
    before.position = {9.0f, 9.0f, 9.0f};
    world.add_component<Transform>(e, before);

    LiveLinkUpdate update;
    update.name = "DoesNotExist";
    REQUIRE_FALSE(apply_live_link_update(world, update));

    // Untouched.
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
}

TEST_CASE("apply_live_link_update returns false and applies nothing when the name is ambiguous", "[studio]")
{
    World world;
    const auto a = world.create_entity();
    world.add_component<Name>(a, Name{"Cube"});
    Transform ta; ta.position = {1.0f, 0.0f, 0.0f};
    world.add_component<Transform>(a, ta);

    const auto b = world.create_entity();
    world.add_component<Name>(b, Name{"Cube"});
    Transform tb; tb.position = {2.0f, 0.0f, 0.0f};
    world.add_component<Transform>(b, tb);

    LiveLinkUpdate update;
    update.name = "Cube";
    update.position = {5.0f, 5.0f, 5.0f};

    REQUIRE_FALSE(apply_live_link_update(world, update));
    REQUIRE(world.get_component<Transform>(a)->position.x == 1.0f);
    REQUIRE(world.get_component<Transform>(b)->position.x == 2.0f);
}
