#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "foundation/serialization/reference.hpp"

using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::FieldVisitorAdapter;
using mosaic::foundation::serialization::AssetID;
using mosaic::foundation::serialization::Handle;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

// No real component uses Handle<R> yet (confirmed by grep across engine/), so this is a
// throwaway struct exercising the reflection path directly -- the resource type R is never
// actually resolved/loaded, matching how Handle<R> itself only ever carries an AssetID at
// the reflection layer.
struct HandleHolder
{
    Handle<int> resource;
};
template <class V>
void reflect(V& v, HandleHolder& h)
{
    v.member("resource", h.resource);
}

// Records every field_handle_asset call it sees.
class CapturingVisitor : public FieldVisitor
{
public:
    std::vector<std::string> seen_names;
    AssetID                  seen_id;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
    void field_handle_asset(std::string_view name, const AssetID& asset_id) override
    {
        seen_names.emplace_back(name);
        seen_id = asset_id;
    }
};

// Does NOT override field_handle_asset, so the base-class default (read-only) applies.
class ReadonlyCapture : public FieldVisitor
{
public:
    std::vector<std::string> readonly_names;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view name, const std::string&) override
    {
        readonly_names.emplace_back(name);
    }
    void begin_group(std::string_view) override {}
    void end_group() override {}
};
} // namespace

TEST_CASE("field_handle_asset lets a visitor read a Handle<R> field's AssetID", "[reflection][assets]")
{
    HandleHolder h;
    h.resource = Handle<int>(make_id(7));

    CapturingVisitor visitor;
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, h);

    REQUIRE(visitor.seen_names.size() == 1);
    REQUIRE(visitor.seen_names[0] == "resource");
    REQUIRE(visitor.seen_id == make_id(7));
}

TEST_CASE("default field_handle_asset falls back to read-only", "[reflection][assets]")
{
    HandleHolder h;
    h.resource = Handle<int>(make_id(3));

    ReadonlyCapture visitor;
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, h);

    // The Handle<R> field 'resource' is presented read-only by the default field_handle_asset,
    // exactly as it was before this field type had its own visitor hook.
    REQUIRE(std::find(visitor.readonly_names.begin(), visitor.readonly_names.end(), "resource") !=
            visitor.readonly_names.end());
}
