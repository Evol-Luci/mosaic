#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/field_visitor.hpp"
#include "engine/render/material.hpp"

using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::FieldVisitorAdapter;
using mosaic::engine::render::Material;
using mosaic::foundation::serialization::AssetID;

namespace
{
AssetID make_id(std::uint8_t tag)
{
    std::array<std::uint8_t, 16> b{};
    b[0] = tag;
    return AssetID{b};
}

// Reassigns any asset field to new_id and records that it saw one.
class AssignVisitor : public FieldVisitor
{
public:
    AssetID new_id;
    bool    saw_asset = false;

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
    void field_asset(std::string_view, AssetID& value) override
    {
        saw_asset = true;
        value = new_id;
    }
};

// Does NOT override field_asset, so the base-class default (read-only) applies.
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

TEST_CASE("field_asset lets a visitor reassign an AssetID field", "[reflection][assets]")
{
    Material m; // texture starts nil
    REQUIRE(m.texture.is_nil());

    AssignVisitor visitor;
    visitor.new_id = make_id(9);
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, m);

    REQUIRE(visitor.saw_asset);
    REQUIRE(m.texture == make_id(9));
}

TEST_CASE("default field_asset falls back to read-only", "[reflection][assets]")
{
    Material m;
    ReadonlyCapture visitor;
    FieldVisitorAdapter adapter{visitor};
    reflect(adapter, m);

    // The AssetID field 'texture' is presented read-only by the default field_asset.
    REQUIRE(std::find(visitor.readonly_names.begin(), visitor.readonly_names.end(), "texture") !=
            visitor.readonly_names.end());
}
