#include <catch2/catch_test_macros.hpp>

#include "engine/net/net_replicated.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::net::NetReplicated;
namespace refl = mosaic::engine::reflection;
namespace ser  = mosaic::foundation::serialization;

TEST_CASE("NetReplicated registers and reflects its id", "[net][replication]")
{
    mosaic::foundation::reflection::TypeRegistry registry;
    refl::ComponentReflection types;
    refl::register_builtin_components(registry, types);

    REQUIRE(types.has("NetReplicated"));

    World world;
    const auto e = world.create_entity();
    world.add_component<NetReplicated>(e, NetReplicated{42});

    const auto value = types.component_value(world, e, "NetReplicated");
    REQUIRE(value.has_value());
    const ser::Table& table = value->as<ser::Table>();
    const ser::Value* id = table.find("id");
    REQUIRE(id != nullptr);
    // Reflected uint32 surfaces as an unsigned integer in the Value DOM.
    REQUIRE(id->as<std::uint64_t>() == 42u);
}
