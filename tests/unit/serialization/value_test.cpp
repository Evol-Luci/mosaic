#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "foundation/serialization/value.hpp"

using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;

TEST_CASE("Value holds and reports its alternative", "[value]")
{
    Value n;
    REQUIRE(n.is_null());

    Value i{std::int64_t{42}};
    REQUIRE(i.is<std::int64_t>());
    REQUIRE(i.as<std::int64_t>() == 42);
    REQUIRE_FALSE(i.is<double>());

    Value s{std::string{"hi"}};
    REQUIRE(s.is<std::string>());
    REQUIRE(s.as<std::string>() == "hi");
}

TEST_CASE("Value equality compares by alternative and content", "[value]")
{
    REQUIRE(Value{std::int64_t{1}} == Value{std::int64_t{1}});
    REQUIRE_FALSE(Value{std::int64_t{1}} == Value{std::uint64_t{1}}); // distinct alternatives
    REQUIRE_FALSE(Value{std::int64_t{1}} == Value{std::int64_t{2}});
}

TEST_CASE("Table is insertion-ordered on iteration but equality is order-independent", "[value]")
{
    Table a;
    a["x"] = Value{std::int64_t{1}};
    a["y"] = Value{std::int64_t{2}};

    Table b;
    b["y"] = Value{std::int64_t{2}};
    b["x"] = Value{std::int64_t{1}};

    REQUIRE(a == b);                      // same keys/values, different order
    REQUIRE(a.begin()->first == "x");     // iteration preserves insertion order
    REQUIRE(a.size() == 2);

    b["x"] = Value{std::int64_t{9}};
    REQUIRE_FALSE(a == b);
}

TEST_CASE("operator[] inserts-or-gets, find returns nullptr for missing", "[value]")
{
    Table t;
    t["k"] = Value{std::string{"v"}};
    REQUIRE(t.find("k") != nullptr);
    REQUIRE(t.find("k")->as<std::string>() == "v");
    REQUIRE(t.find("missing") == nullptr);
}

TEST_CASE("Value nests arrays and tables", "[value]")
{
    Table inner;
    inner["a"] = Value{std::int64_t{1}};
    Array arr;
    arr.push_back(Value{std::int64_t{1}});
    arr.push_back(Value{Table{inner}});

    Value v{std::move(arr)};
    REQUIRE(v.is<Array>());
    REQUIRE(v.as<Array>().size() == 2);
    REQUIRE(v.as<Array>()[1].is<Table>());
}
