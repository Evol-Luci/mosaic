#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "engine/ecs/entity_registry.hpp"
#include "registry_test_access.hpp"

using mosaic::engine::ecs::EntityRegistry;
using mosaic::foundation::EntityID;
using RegistryTestAccess = mosaic::engine::ecs::detail::RegistryTestAccess;

TEST_CASE("created entities are valid and generations start at 1", "[registry]")
{
    EntityRegistry registry;
    EntityID a = registry.create();
    REQUIRE(registry.is_valid(a));
    REQUIRE(a.generation() == 1u);
    REQUIRE(registry.alive_count() == 1u);
}

TEST_CASE("destroyed handles become invalid", "[registry]")
{
    EntityRegistry registry;
    EntityID a = registry.create();
    REQUIRE(registry.destroy(a));
    REQUIRE_FALSE(registry.is_valid(a));
    REQUIRE_FALSE(registry.destroy(a)); // double-destroy rejected
    REQUIRE(registry.alive_count() == 0u);
}

TEST_CASE("the null handle is never valid", "[registry]")
{
    EntityRegistry registry;
    REQUIRE_FALSE(registry.is_valid(EntityID{}));
}

TEST_CASE("out-of-range indices are not valid", "[registry]")
{
    EntityRegistry registry;
    REQUIRE_FALSE(registry.is_valid(EntityID{9999u, 1u}));
}

TEST_CASE("slots recycle with a bumped generation, invalidating old handles", "[registry]")
{
    EntityRegistry registry;
    EntityID a = registry.create();
    registry.destroy(a);
    EntityID b = registry.create();

    REQUIRE(b.index() == a.index());       // same slot reused
    REQUIRE(b.generation() == a.generation() + 1u);
    REQUIRE(registry.is_valid(b));
    REQUIRE_FALSE(registry.is_valid(a));   // stale handle rejected
}

TEST_CASE("generation exhaustion retires the slot instead of recycling", "[registry]")
{
    EntityRegistry registry;
    EntityID a = registry.create();        // slot 0, gen 1

    // Force slot 0 to the last usable generation and build a matching handle.
    constexpr std::uint32_t brink_gen = 0xFFFFFFFEu;
    RegistryTestAccess::set_generation(registry, a.index(), brink_gen);
    EntityID brink{a.index(), brink_gen};
    REQUIRE(registry.is_valid(brink));

    // Destroying it bumps to 0xFFFFFFFF (the retired sentinel): retire, no recycle.
    REQUIRE(registry.destroy(brink));
    REQUIRE_FALSE(registry.is_valid(brink));
    REQUIRE(registry.alive_count() == 0u);

    // The retired slot is never handed back out.
    EntityID next = registry.create();
    REQUIRE(next.index() != a.index());
}

TEST_CASE("a handle bearing the retired sentinel generation is never valid", "[registry]")
{
    EntityRegistry registry;
    EntityID a = registry.create();
    EntityID crafted{a.index(), EntityRegistry::kRetiredGeneration};
    REQUIRE_FALSE(registry.is_valid(crafted));
}
