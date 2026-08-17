#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;

namespace
{
struct DummyResource
{
    int payload;
};
} // namespace

TEST_CASE("register + load returns the resource and caches it", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000101");
    meta.type = "dummy";

    int loader_calls = 0;
    auto resource = std::make_shared<DummyResource>(DummyResource{42});
    registry.register_asset(meta, [&loader_calls, resource]() -> std::shared_ptr<void>
    {
        ++loader_calls;
        return resource;
    });

    std::shared_ptr<DummyResource> first = registry.load<DummyResource>(meta.uuid);
    REQUIRE(first != nullptr);
    REQUIRE(first->payload == 42);

    std::shared_ptr<DummyResource> second = registry.load<DummyResource>(meta.uuid);
    REQUIRE(second == first);   // same cached instance
    REQUIRE(loader_calls == 1); // loader invoked exactly once
}

TEST_CASE("metadata is retrievable by AssetID", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000102");
    meta.type = "mesh";
    meta.version = 7;
    registry.register_asset(meta, []() -> std::shared_ptr<void> { return nullptr; });

    const AssetMetadata* got = registry.metadata(meta.uuid);
    REQUIRE(got != nullptr);
    REQUIRE(got->type == "mesh");
    REQUIRE(got->version == 7u);
    REQUIRE(registry.contains(meta.uuid));
}

TEST_CASE("register_resource stores an eager resource", "[assets]")
{
    AssetRegistry registry;
    AssetMetadata meta;
    meta.uuid = AssetID::from_string("00000000-0000-0000-0000-000000000103");
    auto resource = std::make_shared<DummyResource>(DummyResource{9});

    registry.register_resource(meta, resource);
    REQUIRE(registry.load<DummyResource>(meta.uuid) == resource);
}

TEST_CASE("unknown AssetID resolves to null/false", "[assets]")
{
    AssetRegistry registry;
    const AssetID unknown = AssetID::from_string("00000000-0000-0000-0000-0000000001ff");

    REQUIRE(registry.load<DummyResource>(unknown) == nullptr);
    REQUIRE(registry.metadata(unknown) == nullptr);
    REQUIRE_FALSE(registry.contains(unknown));
}
