#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::AssetMetadata;
using mosaic_samples::ProjectConfig;

namespace
{
// TOML document nesting a single key's value in `levels` arrays, e.g. levels=3
// -> "a = [[[]]]". The root table already occupies one level, so the innermost
// empty array sits at depth `levels + 1`.
std::string nested_toml_arrays(int levels)
{
    return "a = " + std::string(levels, '[') + std::string(levels, ']');
}

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

TEST_CASE("asset metadata round-trips through TOML", "[toml]")
{
    AssetMetadata a = sample_metadata();
    Value v = to_value(a);

    std::string toml = to_toml(v);
    Value parsed = from_toml(toml);

    AssetMetadata out;
    from_value(parsed, out);
    REQUIRE(out == a);
}

TEST_CASE("project config round-trips through TOML", "[toml]")
{
    ProjectConfig c{"Mosaic Demo", "0.1.0", 100000, true};
    Value v = to_value(c);

    ProjectConfig out;
    from_value(from_toml(to_toml(v)), out);
    REQUIRE(out == c);
}

TEST_CASE("to_toml rejects a non-table top-level value", "[toml]")
{
    REQUIRE_THROWS_AS(to_toml(Value{std::int64_t{1}}),
                      mosaic::foundation::serialization::SerializationError);
}

TEST_CASE("from_toml throws SerializationError on malformed input", "[toml]")
{
    REQUIRE_THROWS_AS(from_toml("this is = = not toml"),
                      mosaic::foundation::serialization::SerializationError);
}

TEST_CASE("from_toml accepts nesting at the configured depth limit", "[toml]")
{
    // Root table (1) + 3 arrays == depth 4.
    REQUIRE_NOTHROW(from_toml(nested_toml_arrays(3), /*max_depth=*/4));
}

TEST_CASE("from_toml throws when nesting exceeds the configured depth limit", "[toml]")
{
    REQUIRE_THROWS_AS(from_toml(nested_toml_arrays(4), /*max_depth=*/4),
                      mosaic::foundation::serialization::SerializationError);
}

TEST_CASE("from_toml accepts nesting at the default depth limit", "[toml]")
{
    // Root table (1) + 127 arrays == depth 128.
    REQUIRE_NOTHROW(from_toml(nested_toml_arrays(127)));
}

// Adversarially deep input must surface a SerializationError rather than
// overflowing the stack (DoS / UB) before or inside the DOM builder.
TEST_CASE("from_toml rejects adversarially deep input under the default limit", "[toml]")
{
    REQUIRE_THROWS_AS(from_toml(nested_toml_arrays(1000)),
                      mosaic::foundation::serialization::SerializationError);
}
