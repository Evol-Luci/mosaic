#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <unordered_set>

#include "foundation/core/entity_id.hpp"

using mosaic::foundation::EntityID;

TEST_CASE("default EntityID is the null handle", "[entity_id]")
{
    EntityID null;
    REQUIRE_FALSE(static_cast<bool>(null));
    REQUIRE(null.value() == 0u);
    REQUIRE(null.index() == 0u);
    REQUIRE(null.generation() == 0u);
}

TEST_CASE("EntityID packs index in the low 32 bits and generation in the high 32", "[entity_id]")
{
    EntityID id{7u, 3u};
    REQUIRE(id.index() == 7u);
    REQUIRE(id.generation() == 3u);
    REQUIRE(id.value() == ((std::uint64_t{3} << 32) | 7u));
    REQUIRE(static_cast<bool>(id));
}

TEST_CASE("EntityID equality compares the full 64-bit value", "[entity_id]")
{
    REQUIRE(EntityID{1u, 1u} == EntityID{1u, 1u});
    REQUIRE(EntityID{1u, 1u} != EntityID{1u, 2u});
    REQUIRE(EntityID{1u, 1u} != EntityID{2u, 1u});
}

TEST_CASE("EntityID is usable as a hash key", "[entity_id]")
{
    std::unordered_set<EntityID> set;
    set.insert(EntityID{1u, 1u});
    set.insert(EntityID{1u, 1u});
    set.insert(EntityID{1u, 2u});
    REQUIRE(set.size() == 2u);
}
