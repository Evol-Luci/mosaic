#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/reflection/type_id.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::foundation::reflection::TypeID;
using mosaic::foundation::reflection::TypeInfo;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::reflection::type_id;

namespace
{
struct Health
{
    int current;
    int max;
};
template <class V> void reflect(V& visitor, Health& h)
{
    visitor.member("current", h.current);
    visitor.member("max", h.max);
}

struct Other
{
    float x;
};
template <class V> void reflect(V& visitor, Other& o)
{
    visitor.member("x", o.x);
}

struct Bag
{
    std::vector<int> items;
    Health nested;
};
template <class V> void reflect(V& visitor, Bag& b)
{
    visitor.member("items", b.items);
    visitor.member("nested", b.nested);
}
} // namespace

TEST_CASE("register_type records id and name, findable by name and id", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Health>("Health");

    REQUIRE(info.name == "Health");
    REQUIRE(info.id == type_id<Health>());
    REQUIRE(registry.find("Health") == &info);
    REQUIRE(registry.find(info.id) == &info);
}

TEST_CASE("register_type enumerates fields in declaration order with type labels", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Health>("Health");

    REQUIRE(info.fields.size() == 2);
    REQUIRE(info.fields[0].name == "current");
    REQUIRE(info.fields[0].type == "i32");
    REQUIRE(info.fields[1].name == "max");
    REQUIRE(info.fields[1].type == "i32");
}

TEST_CASE("field_type_name labels vectors and nested reflected types", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Bag>("Bag");

    REQUIRE(info.fields[0].type == "array<i32>");
    REQUIRE(info.fields[1].type == "object");
}

TEST_CASE("find returns nullptr for unknown name and id", "[reflection]")
{
    TypeRegistry registry;
    REQUIRE(registry.find("Nope") == nullptr);
    REQUIRE(registry.find(TypeID{99999}) == nullptr);
}

TEST_CASE("re-registering the same type and name is idempotent", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& a = registry.register_type<Health>("Health");
    const TypeInfo& b = registry.register_type<Health>("Health");
    REQUIRE(&a == &b);
}

TEST_CASE("a name collision with a different type throws", "[reflection]")
{
    TypeRegistry registry;
    registry.register_type<Health>("Dup");
    REQUIRE_THROWS_AS(registry.register_type<Other>("Dup"), std::logic_error);
}
