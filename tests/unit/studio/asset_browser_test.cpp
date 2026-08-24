#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_registry.hpp"
#include "studio/asset_browser.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;
using mosaic::studio::AssetBrowserModel;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}
} // namespace

TEST_CASE("AssetBrowserModel refreshes a sorted list from the registry", "[studio][assets]")
{
    AssetRegistry reg;
    reg.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(reg);

    REQUIRE(model.entries().size() == 2);
    // Sorted by id string: make_id(1) ("0100...") < make_id(2) ("0200...").
    REQUIRE(model.entries()[0].id == make_id(1));
    REQUIRE(model.entries()[0].type == "a");
    REQUIRE(model.entries()[1].id == make_id(2));
}

TEST_CASE("AssetBrowserModel tracks selection", "[studio][assets]")
{
    AssetRegistry reg;
    reg.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    reg.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(reg);
    REQUIRE_FALSE(model.has_selection());

    model.select(make_id(2));
    REQUIRE(model.has_selection());
    REQUIRE(model.selected() == make_id(2));
}

TEST_CASE("AssetBrowserModel drops a selection that no longer exists", "[studio][assets]")
{
    AssetRegistry with_two;
    with_two.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    with_two.register_asset(AssetMetadata{make_id(2), "b"}, [] { return nullptr; });

    AssetBrowserModel model;
    model.refresh(with_two);
    model.select(make_id(2));
    REQUIRE(model.has_selection());

    AssetRegistry with_one; // no id 2
    with_one.register_asset(AssetMetadata{make_id(1), "a"}, [] { return nullptr; });
    model.refresh(with_one);
    REQUIRE_FALSE(model.has_selection());
}
