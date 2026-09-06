#include <catch2/catch_test_macros.hpp>

#include "engine/navigation/nav_grid.hpp"

using mosaic::engine::navigation::find_path;
using mosaic::engine::navigation::GridCell;
using mosaic::engine::navigation::NavGrid;

TEST_CASE("find_path returns a straight line on open ground", "[navigation]")
{
    NavGrid grid(5, 1);
    const auto path = find_path(grid, {0, 0}, {4, 0});
    REQUIRE(path.size() == 5);
    REQUIRE(path.front() == GridCell{0, 0});
    REQUIRE(path.back() == GridCell{4, 0});
}

TEST_CASE("find_path routes around a wall", "[navigation]")
{
    NavGrid grid(5, 5);
    for (int y = 0; y < 4; ++y) { grid.block(2, y); } // wall on column x=2, gap at (2,4)

    const auto path = find_path(grid, {0, 0}, {4, 0});
    REQUIRE_FALSE(path.empty());
    REQUIRE(path.front() == GridCell{0, 0});
    REQUIRE(path.back() == GridCell{4, 0});
    for (const GridCell& c : path)
    {
        REQUIRE(grid.walkable(c.x, c.y)); // never steps on a blocked cell
    }
}

TEST_CASE("find_path returns empty when the goal is boxed in", "[navigation]")
{
    NavGrid grid(5, 5);
    grid.block(3, 2); grid.block(1, 2); grid.block(2, 3); grid.block(2, 1); // orthogonal walls
    grid.block(1, 1); grid.block(3, 1); grid.block(1, 3); grid.block(3, 3); // diagonals (no cutting)

    const auto path = find_path(grid, {0, 0}, {2, 2}); // (2,2) fully enclosed
    REQUIRE(path.empty());
}

TEST_CASE("find_path handles degenerate and out-of-bounds cases", "[navigation]")
{
    NavGrid grid(3, 3);
    REQUIRE(find_path(grid, {1, 1}, {1, 1}).size() == 1);   // start == goal
    REQUIRE(find_path(grid, {-1, 0}, {2, 2}).empty());      // start out of bounds
    REQUIRE(find_path(grid, {0, 0}, {9, 9}).empty());       // goal out of bounds
    grid.block(2, 2);
    REQUIRE(find_path(grid, {0, 0}, {2, 2}).empty());       // goal blocked
}
