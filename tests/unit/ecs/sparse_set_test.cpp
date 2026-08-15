#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/sparse_set.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::ecs::SparseSet;
using mosaic::foundation::EntityID;

TEST_CASE("add then get returns the stored value", "[sparse_set]")
{
    SparseSet<int> set;
    EntityID e{5u, 1u};
    set.add(e, 42);
    REQUIRE(set.contains(e));
    REQUIRE(set.get(e) != nullptr);
    REQUIRE(*set.get(e) == 42);
    REQUIRE(set.size() == 1u);
}

TEST_CASE("get on an absent entity returns null", "[sparse_set]")
{
    SparseSet<int> set;
    REQUIRE(set.get(EntityID{5u, 1u}) == nullptr);
    REQUIRE_FALSE(set.contains(EntityID{5u, 1u}));
}

TEST_CASE("a handle with a stale generation does not match", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{5u, 1u}, 42);
    REQUIRE(set.get(EntityID{5u, 2u}) == nullptr); // same slot, newer gen
    REQUIRE_FALSE(set.contains(EntityID{5u, 2u}));
}

TEST_CASE("add on an existing entity overwrites in place", "[sparse_set]")
{
    SparseSet<int> set;
    EntityID e{5u, 1u};
    set.add(e, 42);
    set.add(e, 99);
    REQUIRE(set.size() == 1u);
    REQUIRE(*set.get(e) == 99);
}

TEST_CASE("remove uses swap-and-pop and keeps the rest reachable", "[sparse_set]")
{
    SparseSet<int> set;
    EntityID a{1u, 1u};
    EntityID b{2u, 1u};
    EntityID c{3u, 1u};
    set.add(a, 10);
    set.add(b, 20);
    set.add(c, 30);

    REQUIRE(set.remove(b));
    REQUIRE(set.size() == 2u);
    REQUIRE_FALSE(set.contains(b));
    REQUIRE(set.get(b) == nullptr);
    REQUIRE(*set.get(a) == 10);
    REQUIRE(*set.get(c) == 30);

    REQUIRE_FALSE(set.remove(b)); // removing absent is a no-op
}

TEST_CASE("removing the only element is safe and empties the set", "[sparse_set]")
{
    SparseSet<int> set;
    EntityID a{1u, 1u};
    set.add(a, 42);
    REQUIRE(set.remove(a));
    REQUIRE(set.size() == 0u);
    REQUIRE_FALSE(set.contains(a));
    REQUIRE(set.get(a) == nullptr);
}

TEST_CASE("adding a newer generation for the same slot replaces the old entry", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{5u, 1u}, 10);
    set.add(EntityID{5u, 2u}, 20); // same slot, advanced generation, no remove
    REQUIRE(set.size() == 1u);
    REQUIRE(set.get(EntityID{5u, 2u}) != nullptr);
    REQUIRE(*set.get(EntityID{5u, 2u}) == 20);
    REQUIRE(set.get(EntityID{5u, 1u}) == nullptr); // stale handle no longer matches
}
