#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"
#include "sample_schemas.hpp"

using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_value;
using mosaic_samples::Transform;
using mosaic_samples::Vec3;

namespace
{
struct Scalars
{
    bool flag = false;
    std::int32_t count = 0;
    double ratio = 0.0;
    std::string label;
    std::vector<std::int32_t> nums;
    friend bool operator==(const Scalars&, const Scalars&) = default;
};
template <class V> void reflect(V& v, Scalars& s)
{
    v.member("flag", s.flag);
    v.member("count", s.count);
    v.member("ratio", s.ratio);
    v.member("label", s.label);
    v.member("nums", s.nums);
}
} // namespace

TEST_CASE("to_value emits a Table keyed by field name", "[reflect]")
{
    Scalars s{true, -5, 1.5, "hi", {1, 2, 3}};
    Value v = to_value(s);
    REQUIRE(v.is<Table>());
    const Table& t = v.as<Table>();
    REQUIRE(t.find("flag")->as<bool>() == true);
    REQUIRE(t.find("count")->as<std::int64_t>() == -5);
    REQUIRE(t.find("ratio")->as<double>() == 1.5);
    REQUIRE(t.find("label")->as<std::string>() == "hi");
    REQUIRE(t.find("nums")->as<Array>().size() == 3);
    REQUIRE(t.find("nums")->as<Array>()[2].as<std::int64_t>() == 3);
}

TEST_CASE("scalar/vector round-trip is identity", "[reflect]")
{
    Scalars s{true, 42, 2.25, "round", {7, 8}};
    Value v = to_value(s);
    Scalars out;
    from_value(v, out);
    REQUIRE(out == s);
}

TEST_CASE("nested reflected structs recurse", "[reflect]")
{
    Transform tr;
    tr.position = Vec3{1.0, 2.0, 3.0};
    tr.rotation = mosaic_samples::Quat{0.0, 0.0, 0.0, 1.0};
    tr.scale = Vec3{4.0, 5.0, 6.0};

    Value v = to_value(tr);
    REQUIRE(v.as<Table>().find("position")->is<Table>());
    REQUIRE(v.as<Table>().find("position")->as<Table>().find("y")->as<double>() == 2.0);

    Transform out;
    from_value(v, out);
    REQUIRE(out == tr);
}

TEST_CASE("from_value leaves fields absent from the table at their default", "[reflect]")
{
    Table t;
    t["count"] = Value{std::int64_t{9}};
    Value v{std::move(t)};

    Scalars out;
    from_value(v, out);
    REQUIRE(out.count == 9);
    REQUIRE(out.label.empty()); // "label" absent -> default
}

TEST_CASE("from_value throws on type mismatch", "[reflect]")
{
    Table t;
    t["count"] = Value{std::string{"not a number"}};
    Value v{std::move(t)};

    Scalars out;
    REQUIRE_THROWS_AS(from_value(v, out),
                      mosaic::foundation::serialization::SerializationError);
}
