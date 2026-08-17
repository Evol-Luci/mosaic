#include <functional>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "foundation/serialization/reference.hpp"

using mosaic::foundation::serialization::AssetID;

TEST_CASE("AssetID is usable as an unordered_map key", "[assets]")
{
    std::unordered_map<AssetID, int> map;
    const AssetID a = AssetID::from_string("00000000-0000-0000-0000-000000000001");
    const AssetID b = AssetID::from_string("00000000-0000-0000-0000-000000000002");
    map[a] = 1;
    map[b] = 2;

    REQUIRE(map.size() == 2);
    REQUIRE(map.at(a) == 1);
    REQUIRE(map.at(b) == 2);
    REQUIRE(std::hash<AssetID>{}(a) == std::hash<AssetID>{}(a));
    REQUIRE(std::hash<AssetID>{}(a) != std::hash<AssetID>{}(b));
}
