#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}
} // namespace

TEST_CASE("AssetRegistry enumerates registered assets", "[assets]")
{
    AssetRegistry reg;
    REQUIRE(reg.size() == 0);

    reg.register_asset(AssetMetadata{make_id(1), "texture"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(2), "mesh"}, [] { return nullptr; });
    REQUIRE(reg.size() == 2);

    std::vector<std::string> types;
    reg.for_each([&](const AssetMetadata& m) { types.push_back(m.type); });
    REQUIRE(types.size() == 2);
    REQUIRE(std::find(types.begin(), types.end(), "texture") != types.end());
    REQUIRE(std::find(types.begin(), types.end(), "mesh") != types.end());
}
