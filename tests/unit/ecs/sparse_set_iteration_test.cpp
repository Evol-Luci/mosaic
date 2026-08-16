#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/sparse_set.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::ecs::SparseSet;
using mosaic::foundation::EntityID;

TEST_CASE("SparseSet::each visits every stored pair in dense order", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{1, 1}, 10);
    set.add(EntityID{2, 1}, 20);

    std::vector<std::pair<std::uint32_t, int>> seen;
    set.each([&](EntityID id, int& value) { seen.emplace_back(id.index(), value); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == std::make_pair(std::uint32_t{1}, 10));
    REQUIRE(seen[1] == std::make_pair(std::uint32_t{2}, 20));
}

TEST_CASE("SparseSet::each yields mutable references", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{5, 1}, 1);
    set.each([](EntityID, int& value) { value = 99; });
    REQUIRE(*set.get(EntityID{5, 1}) == 99);
}
