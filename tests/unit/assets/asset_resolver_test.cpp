#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::assets::AssetMetadata;
using mosaic::engine::assets::AssetReferenceResolver;
using mosaic::engine::assets::AssetRegistry;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

namespace
{
struct DummyResource
{
    int payload;
};

struct MeshRef
{
    Handle<DummyResource> mesh;
};
template <class V> void reflect(V& visitor, MeshRef& m)
{
    visitor.member("mesh", m.mesh);
}
} // namespace

TEST_CASE("a registered Handle survives deserialize; an unknown one resolves to nil",
          "[assets]")
{
    AssetRegistry registry;
    const AssetID known = AssetID::from_string("00000000-0000-0000-0000-0000000000a1");
    AssetMetadata meta;
    meta.uuid = known;
    meta.type = "dummy";
    registry.register_resource(meta, std::make_shared<DummyResource>(DummyResource{1}));

    AssetReferenceResolver resolver(registry);

    // Known reference passes through validation.
    MeshRef ref;
    ref.mesh = Handle<DummyResource>(known);
    Value value = to_value(ref);
    MeshRef loaded;
    from_value(value, loaded, resolver);
    REQUIRE(loaded.mesh.asset_id() == known);

    // Unknown reference resolves to a nil handle.
    MeshRef ref2;
    ref2.mesh = Handle<DummyResource>(
        AssetID::from_string("00000000-0000-0000-0000-0000000000ff"));
    Value value2 = to_value(ref2);
    MeshRef loaded2;
    from_value(value2, loaded2, resolver);
    REQUIRE(loaded2.mesh.asset_id().is_nil());
}
