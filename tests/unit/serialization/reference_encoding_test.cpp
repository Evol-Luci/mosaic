#include <catch2/catch_test_macros.hpp>

#include <string>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Handle;
using mosaic::foundation::serialization::ReferenceResolver;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_value;

namespace
{
struct Refs
{
    EntityID target;
    AssetID material;
    Handle<int> texture;
    friend bool operator==(const Refs&, const Refs&) = default;
};
template <class V> void reflect(V& v, Refs& r)
{
    v.member("target", r.target);
    v.member("material", r.material);
    v.member("texture", r.texture);
}

// Swaps a specific asset/entity for another, to prove the resolver runs.
class RemapResolver : public ReferenceResolver
{
public:
    EntityID resolve_entity(EntityID serialized) const override
    {
        return serialized == EntityID{5, 1} ? EntityID{99, 2} : serialized;
    }
    AssetID resolve_asset(AssetID serialized) const override
    {
        AssetID from = AssetID::from_string("11111111-1111-1111-1111-111111111111");
        AssetID to = AssetID::from_string("22222222-2222-2222-2222-222222222222");
        return serialized == from ? to : serialized;
    }
};
} // namespace

TEST_CASE("references serialize as their id primitives", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("33333333-3333-3333-3333-333333333333")};

    Value v = to_value(r);
    const Table& t = v.as<Table>();
    REQUIRE(t.find("target")->is<std::string>());               // EntityID -> decimal string
    REQUIRE(t.find("target")->as<std::string>() == std::to_string(EntityID{5, 1}.value()));
    REQUIRE(t.find("material")->as<std::string>() == "11111111-1111-1111-1111-111111111111");
    REQUIRE(t.find("texture")->as<std::string>() == "33333333-3333-3333-3333-333333333333");
}

TEST_CASE("reference round-trip with default resolver is identity", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("33333333-3333-3333-3333-333333333333")};

    Value v = to_value(r);
    Refs out;
    from_value(v, out);
    REQUIRE(out == r);
}

TEST_CASE("resolver re-resolves entity and asset references at load", "[refenc]")
{
    Refs r;
    r.target = EntityID{5, 1};
    r.material = AssetID::from_string("11111111-1111-1111-1111-111111111111");
    r.texture = Handle<int>{AssetID::from_string("11111111-1111-1111-1111-111111111111")};

    Value v = to_value(r);
    Refs out;
    RemapResolver resolver;
    from_value(v, out, resolver);

    REQUIRE(out.target == EntityID{99, 2});
    REQUIRE(out.material == AssetID::from_string("22222222-2222-2222-2222-222222222222"));
    REQUIRE(out.texture == Handle<int>{AssetID::from_string("22222222-2222-2222-2222-222222222222")});
}
