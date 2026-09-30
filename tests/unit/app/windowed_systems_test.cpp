// Pins the registration decision that a shipped-dead feature slipped through.
//
// ClickNudgeSystem was once registered behind a guard that read a variable which was
// still null at that point in main(), so the guard was always false and click-to-nudge
// was dead code in the real app -- while the whole suite stayed green, because every
// test constructed ClickNudgeSystem directly and nothing exercised the wiring. These
// tests assert the wiring itself: that the windowed systems ARE registered when there
// is a window, and are NOT when there isn't.

#include <catch2/catch_test_macros.hpp>

#include "app/run/click_nudge.hpp"
#include "app/run/windowed_systems.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"

using mosaic::app::ClickNudgeSystem;
using mosaic::app::WindowedSystemDeps;
using mosaic::app::register_windowed_systems;
using mosaic::engine::World;

namespace
{
WindowedSystemDeps stub_deps()
{
    WindowedSystemDeps d;
    d.pointer = [] { return mosaic::engine::ui::PointerState{}; };
    d.aspect = [] { return 1.0f; };
    d.ui_claimed = [] { return false; };
    return d;
}
} // namespace

TEST_CASE("register_windowed_systems registers ClickNudgeSystem for a windowed run", "[app]")
{
    World world;
    REQUIRE(world.get_system<ClickNudgeSystem>() == nullptr);

    register_windowed_systems(world, true, stub_deps());

    REQUIRE(world.get_system<ClickNudgeSystem>() != nullptr);
}

TEST_CASE("register_windowed_systems registers nothing for a headless run", "[app]")
{
    World world;

    register_windowed_systems(world, false, stub_deps());

    REQUIRE(world.get_system<ClickNudgeSystem>() == nullptr);
}

TEST_CASE("register_windowed_systems leaves a windowed world tickable with no scene", "[app]")
{
    // The registered system must survive a tick against an empty world: no camera, no
    // physics, no meshes. pick_entity returns a miss and the nudge must absorb it.
    World world;
    register_windowed_systems(world, true, stub_deps());

    REQUIRE_NOTHROW(world.tick(1.0f / 60.0f));
}
