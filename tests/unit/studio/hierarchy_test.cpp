#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/world/name.hpp"
#include "engine/world/world.hpp"
#include "studio/hierarchy.hpp"

using mosaic::engine::Name;
using mosaic::engine::World;
using mosaic::studio::build_hierarchy;
using mosaic::studio::HierarchyRow;

TEST_CASE("build_hierarchy on an empty world yields no rows", "[studio]")
{
    World world;
    REQUIRE(build_hierarchy(world).empty());
}

TEST_CASE("build_hierarchy yields one labelled row per entity", "[studio]")
{
    World world;
    const auto a = world.create_entity();
    const auto b = world.create_entity();
    const auto c = world.create_entity();

    const std::vector<HierarchyRow> rows = build_hierarchy(world);
    REQUIRE(rows.size() == 3);

    // Every created entity appears exactly once, labelled "Entity <index>".
    bool saw_a = false, saw_b = false, saw_c = false;
    for (const HierarchyRow& row : rows)
    {
        REQUIRE(row.label == "Entity " + std::to_string(row.id.index()));
        if (row.id == a) { saw_a = true; }
        if (row.id == b) { saw_b = true; }
        if (row.id == c) { saw_c = true; }
    }
    REQUIRE(saw_a);
    REQUIRE(saw_b);
    REQUIRE(saw_c);
}

TEST_CASE("build_hierarchy labels a named entity with its Name", "[studio]")
{
    World world;
    const auto e = world.create_entity();
    world.add_component<Name>(e, Name{"Player"});

    const std::vector<HierarchyRow> rows = build_hierarchy(world);
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].label == "Player");
}

TEST_CASE("build_hierarchy falls back to Entity <index> for unnamed entities among named ones", "[studio]")
{
    World world;
    const auto named = world.create_entity();
    world.add_component<Name>(named, Name{"Camera"});
    const auto unnamed = world.create_entity();

    const std::vector<HierarchyRow> rows = build_hierarchy(world);
    REQUIRE(rows.size() == 2);
    for (const HierarchyRow& row : rows)
    {
        if (row.id == named) { REQUIRE(row.label == "Camera"); }
        if (row.id == unnamed) { REQUIRE(row.label == "Entity " + std::to_string(row.id.index())); }
    }
}
