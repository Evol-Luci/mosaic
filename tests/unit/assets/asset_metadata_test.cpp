#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

TEST_CASE("AssetMetadata round-trips through TOML", "[assets]")
{
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-0000000000aa");
    meta.type = "mesh";
    meta.version = 3;
    meta.source = "models/robot.blend";
    meta.dependencies.push_back(AssetID::from_string("00000000-0000-0000-0000-0000000000bb"));

    Table extra;
    extra["author"] = Value(std::string("luci"));
    meta.metadata = Value(std::move(extra));

    Table imp;
    imp["scale"] = Value(std::int64_t{2});
    meta.import_settings = Value(std::move(imp));

    const std::string toml = to_toml(to_value(meta));

    AssetMetadata back;
    from_value(from_toml(toml), back);

    REQUIRE(back.uuid == meta.uuid);
    REQUIRE(back.type == "mesh");
    REQUIRE(back.version == 3u);
    REQUIRE(back.source == "models/robot.blend");
    REQUIRE(back.dependencies.size() == 1);
    REQUIRE(back.dependencies[0] == meta.dependencies[0]);
    REQUIRE(back.metadata == meta.metadata);
    REQUIRE(back.import_settings == meta.import_settings);
}
