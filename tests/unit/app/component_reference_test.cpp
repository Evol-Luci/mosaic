#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/docs/component_reference.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::generate_component_reference_markdown;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace
{
// Synthetic components with known field shapes, local to this test file —
// not the real 28-component registry (that's covered by the integration
// case below).

struct FloatOnly
{
    float speed = 3.5f;
};

template <class Visitor>
void reflect(Visitor& v, FloatOnly& c)
{
    v.member("speed", c.speed);
}

struct StringOnly
{
    std::string label = "hello";
};

template <class Visitor>
void reflect(Visitor& v, StringOnly& c)
{
    v.member("label", c.label);
}

struct BoolOnly
{
    bool enabled = true;
};

template <class Visitor>
void reflect(Visitor& v, BoolOnly& c)
{
    v.member("enabled", c.enabled);
}

struct Nested
{
    float x = 1.0f;
    float y = 2.0f;
};

template <class Visitor>
void reflect(Visitor& v, Nested& c)
{
    v.member("x", c.x);
    v.member("y", c.y);
}

struct WithGroup
{
    Nested offset{};
};

template <class Visitor>
void reflect(Visitor& v, WithGroup& c)
{
    v.member("offset", c.offset);
}
} // namespace

TEST_CASE("component reference renders a bare float field", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<FloatOnly>(registry, "FloatOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## FloatOnly") != std::string::npos);
    REQUIRE(markdown.find("`speed`: 3.500000") != std::string::npos);
}

TEST_CASE("component reference renders a string field", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<StringOnly>(registry, "StringOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## StringOnly") != std::string::npos);
    REQUIRE(markdown.find("`label`: hello") != std::string::npos);
}

TEST_CASE("component reference renders a bool field via field_readonly", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<BoolOnly>(registry, "BoolOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## BoolOnly") != std::string::npos);
    REQUIRE(markdown.find("`enabled`: true") != std::string::npos);
}

TEST_CASE("component reference indents a nested group's fields", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<WithGroup>(registry, "WithGroup");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## WithGroup") != std::string::npos);
    REQUIRE(markdown.find("`offset`") != std::string::npos);
    // Nested fields render indented one level deeper than the group line.
    REQUIRE(markdown.find("  - `x`: 1.000000") != std::string::npos);
    REQUIRE(markdown.find("  - `y`: 2.000000") != std::string::npos);
}

TEST_CASE("component reference opens with an auto-generated banner naming the type-label limitation", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<FloatOnly>(registry, "FloatOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("<!-- AUTO-GENERATED") != std::string::npos);
    REQUIRE(markdown.find("do not edit by hand") != std::string::npos);
}

TEST_CASE("component reference covers every real built-in component", "[docs][integration]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE_FALSE(markdown.empty());
    REQUIRE(markdown.find("<!-- AUTO-GENERATED") != std::string::npos);
    for (const std::string& name : types.registered_names())
    {
        const std::string heading = "## " + name;
        REQUIRE(markdown.find(heading) != std::string::npos);
    }
}
