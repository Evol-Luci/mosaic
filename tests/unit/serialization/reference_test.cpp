#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::ReferenceResolver;
using mosaic::foundation::serialization::SerializationError;

TEST_CASE("AssetID round-trips through its canonical string", "[reference]")
{
    const std::string text = "12345678-9abc-def0-1234-56789abcdef0";
    AssetID id = AssetID::from_string(text);
    REQUIRE(id.to_string() == text);
    REQUIRE_FALSE(id.is_nil());
}

TEST_CASE("Default AssetID is nil", "[reference]")
{
    AssetID id;
    REQUIRE(id.is_nil());
    REQUIRE(id.to_string() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("AssetID::from_string rejects malformed input", "[reference]")
{
    REQUIRE_THROWS_AS(AssetID::from_string("too-short"), SerializationError);
    REQUIRE_THROWS_AS(AssetID::from_string("12345678x9abc-def0-1234-56789abcdef0"), SerializationError); // missing '-'
    REQUIRE_THROWS_AS(AssetID::from_string("g2345678-9abc-def0-1234-56789abcdef0"), SerializationError); // non-hex
}

TEST_CASE("AssetID uppercase parses and re-emits lowercase", "[reference]")
{
    AssetID id = AssetID::from_string("ABCDEF01-2345-6789-ABCD-EF0123456789");
    REQUIRE(id.to_string() == "abcdef01-2345-6789-abcd-ef0123456789");
}

TEST_CASE("Handle exposes its AssetID and compares by it", "[reference]")
{
    AssetID id = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    Handle<int> h{id};
    REQUIRE(h.asset_id() == id);
    REQUIRE(h == Handle<int>{id});
    REQUIRE_FALSE(h == Handle<int>{});
}

TEST_CASE("Default ReferenceResolver is identity", "[reference]")
{
    ReferenceResolver r;
    AssetID id = AssetID::from_string("12345678-9abc-def0-1234-56789abcdef0");
    REQUIRE(r.resolve_asset(id) == id);
    EntityID e{7, 3};
    REQUIRE(r.resolve_entity(e) == e);
}
