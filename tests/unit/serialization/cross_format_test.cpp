#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_json;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_json;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::AssetMetadata;

namespace
{
AssetMetadata sample_metadata()
{
    AssetMetadata a;
    a.uuid = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    a.type = "texture";
    a.version = 2;
    a.source = "art/hero.png";
    a.dependencies = {AssetID::from_string("11111111-1111-1111-1111-111111111111")};

    Table meta;
    meta["author"] = Value{std::string{"luci"}};
    a.metadata = Value{std::move(meta)};

    Table imp;
    imp["srgb"] = Value{true};
    imp["mips"] = Value{std::int64_t{4}};
    a.import_settings = Value{std::move(imp)};
    return a;
}
} // namespace

TEST_CASE("TOML -> model -> JSON -> model is identity on the schema", "[crossfmt]")
{
    Value original = to_value(sample_metadata());

    std::string toml = to_toml(original);
    Value from_toml_model = from_toml(toml);

    std::string json = to_json(from_toml_model);
    Value from_json_model = from_json(json);

    REQUIRE(from_toml_model == original);
    REQUIRE(from_json_model == original);
    REQUIRE(from_toml_model == from_json_model);
}

TEST_CASE("a value authored in TOML decodes to the same struct via JSON", "[crossfmt]")
{
    AssetMetadata a = sample_metadata();

    // TOML -> Value -> struct
    AssetMetadata via_toml;
    from_value(from_toml(to_toml(to_value(a))), via_toml);

    // TOML -> Value -> JSON -> Value -> struct
    AssetMetadata via_json;
    from_value(from_json(to_json(from_toml(to_toml(to_value(a))))), via_json);

    REQUIRE(via_toml == a);
    REQUIRE(via_json == a);
}
