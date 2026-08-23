#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/reflection/field_visitor.hpp"
#include "engine/render/camera.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::FieldVisitor;
using mosaic::engine::reflection::register_builtin_components;

namespace
{
struct WritingVisitor : FieldVisitor
{
    float value;
    explicit WritingVisitor(float v) : value(v) {}
    void field_float(std::string_view, float& f) override { f = value; }
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view) override {}
    void end_group() override {}
};

struct Fixture
{
    mosaic::foundation::reflection::TypeRegistry registry;
    ComponentReflection                          reflection;
    World                                         world;
    Fixture() { register_builtin_components(registry, reflection); }
};
} // namespace

TEST_CASE("components_on lists an entity's registered components, sorted", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(e, {});
    f.world.add_component<mosaic::engine::render::Camera>(e, {});

    const std::vector<std::string> names = f.reflection.components_on(f.world, e);
    REQUIRE(names == std::vector<std::string>{"Camera", "Transform"});

    const auto bare = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(bare, {});
    REQUIRE(f.reflection.components_on(f.world, bare) == std::vector<std::string>{"Transform"});
}

TEST_CASE("inspect edits the live component through the visitor", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    f.world.add_component<mosaic::engine::transform::Transform>(e, {});

    WritingVisitor w{7.0f};
    f.reflection.inspect(f.world, e, "Transform", w);

    const auto* t = f.world.get_component<mosaic::engine::transform::Transform>(e);
    REQUIRE(t != nullptr);
    REQUIRE(t->position.x == 7.0f);
    REQUIRE(t->scale.y == 7.0f);
}

TEST_CASE("inspect is a no-op for an unregistered name", "[reflection]")
{
    Fixture f;
    const auto e = f.world.create_entity();
    WritingVisitor w{1.0f};
    f.reflection.inspect(f.world, e, "NotAComponent", w); // must not throw
    REQUIRE(f.reflection.components_on(f.world, e).empty());
}
