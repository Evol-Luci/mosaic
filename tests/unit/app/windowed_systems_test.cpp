// Pins the registration decision that a shipped-dead feature slipped through.
//
// ClickNudgeSystem was once registered behind a guard that read a variable which was
// still null at that point in main(), so the guard was always false and click-to-nudge
// was dead code in the real app -- while the whole suite stayed green, because every
// test constructed ClickNudgeSystem directly and nothing exercised the wiring. These
// tests assert the wiring itself: that the windowed systems ARE registered when there
// is a window, and are NOT when there isn't.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/run/click_nudge.hpp"
#include "app/run/windowed_systems.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_system.hpp"
#include "engine/world/world.hpp"
#include "engine/script/script_event.hpp"
#include "studio/viewport.hpp"

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

// ---------------------------------------------------------------------------------------
// The editor's pointer path. Same class of bug as the one above, one caller along: before
// the viewport-pointer slice, `register_windowed_systems` had exactly one caller (main.cpp),
// so `mosaic editor` had no ClickNudgeSystem at all -- and setup_runtime's `pointer`
// parameter defaults to {}, so the editor's UiSystem held an empty provider and saw
// PointerState{} forever. Nothing failed, because nothing exercised the wiring.
// ---------------------------------------------------------------------------------------

TEST_CASE("a ViewportPanel-derived provider clicks a UiButton through UiSystem",
          "[app][pointer]")
{
    // End-to-end through the REAL UiSystem: press, then RELEASE, then assert the
    // ScriptEvent. Both halves matter and the first version of this case had neither.
    //
    // It asserted press_consumed() after a single tick with down=true. UiSystem only emits
    // the event on the RELEASE edge inside the same button, so the path that actually makes
    // a button work went untested. And its button was pos={0,0} size={1,1} -- the whole
    // panel -- so the assertion held for ANY position in [0,1]: a provider returning (0,0)
    // unconditionally, or swapping x and y, or flipping Y, passed unchanged. Found in
    // review 2026-10-07.
    //
    // So: a SUB-RECT button, a press inside it, a release inside it, and the event checked.
    // The negative case below puts the cursor outside the same button.
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);

    World world;
    world.add_system<mosaic::engine::ui::UiSystem>(
        [&panel] { return panel.pointer_state(/*play_active=*/true); });

    std::vector<std::string> fired;
    world.events().subscribe<mosaic::engine::script::ScriptEvent>(
        [&fired](const mosaic::engine::script::ScriptEvent& e) { fired.push_back(e.name); });

    // Centred 0.2x0.2 button: covers (0.4,0.4)..(0.6,0.6) of the panel.
    const auto e = world.create_entity();
    mosaic::engine::ui::UiButton b;
    b.pos = mosaic::foundation::math::Vec2{0.4f, 0.4f};
    b.size = mosaic::foundation::math::Vec2{0.2f, 0.2f};
    b.event = "pointer_test_click";
    world.add_component<mosaic::engine::ui::UiButton>(e, b);

    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f; // panel centre -> (0.5, 0.5), inside the button
    s.hovered = true;

    s.button_down = true;                                  // press
    panel.set_pointer_sample(s, /*play_active=*/true);
    world.tick(1.0f / 60.0f);
    REQUIRE(world.get_system<mosaic::engine::ui::UiSystem>()->press_consumed());

    s.button_down = false;                                 // release, still inside
    panel.set_pointer_sample(s, /*play_active=*/true);
    world.tick(1.0f / 60.0f);

    INFO("If this fails, the provider is not reaching UiSystem, or the reported position "
         "is not the panel centre -- check editor.cpp passes one to setup_runtime.");
    REQUIRE(fired.size() == 1);
    REQUIRE(fired[0] == "pointer_test_click");
}

TEST_CASE("the provider's position decides which UiButton is clicked", "[app][pointer]")
{
    // What the full-panel button could not test: that x and y are the RIGHT values, not
    // merely in range. The cursor sits in the panel's top-left quadrant, well outside the
    // centred button, so a press-and-release there must emit nothing. A provider that
    // reported (0.5, 0.5) regardless, or clamped everything to a corner that happened to
    // land inside the rect, would fail here.
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);

    World world;
    world.add_system<mosaic::engine::ui::UiSystem>(
        [&panel] { return panel.pointer_state(/*play_active=*/true); });

    std::vector<std::string> fired;
    world.events().subscribe<mosaic::engine::script::ScriptEvent>(
        [&fired](const mosaic::engine::script::ScriptEvent& e) { fired.push_back(e.name); });

    const auto e = world.create_entity();
    mosaic::engine::ui::UiButton b;
    b.pos = mosaic::foundation::math::Vec2{0.4f, 0.4f};
    b.size = mosaic::foundation::math::Vec2{0.2f, 0.2f};
    b.event = "should_not_fire";
    world.add_component<mosaic::engine::ui::UiButton>(e, b);

    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 10.0f; s.mouse_y = 10.0f; // -> (0.1, 0.1), outside the button
    s.hovered = true;

    s.button_down = true;
    panel.set_pointer_sample(s, true);
    world.tick(1.0f / 60.0f);
    s.button_down = false;
    panel.set_pointer_sample(s, true);
    world.tick(1.0f / 60.0f);

    REQUIRE(fired.empty());
}

TEST_CASE("the same provider reports no press in edit mode", "[app][pointer]")
{
    // The other half of the behaviour decision, through the same path: identical sample,
    // play mode off, so UiSystem must see nothing. Without this, the case above would pass
    // equally well with the play gate deleted.
    mosaic::studio::ViewportPanel panel;
    panel.set_measured_size(100.0f, 100.0f);
    mosaic::studio::ViewportPointerSample s;
    s.mouse_x = 50.0f; s.mouse_y = 50.0f; s.hovered = true; s.button_down = true;
    panel.set_pointer_sample(s, /*play_active=*/false);

    World world;
    world.add_system<mosaic::engine::ui::UiSystem>(
        [&panel] { return panel.pointer_state(/*play_active=*/false); });
    const auto e = world.create_entity();
    mosaic::engine::ui::UiButton b;
    b.pos = mosaic::foundation::math::Vec2{0.4f, 0.4f};
    b.size = mosaic::foundation::math::Vec2{0.2f, 0.2f};
    world.add_component<mosaic::engine::ui::UiButton>(e, b);

    world.tick(1.0f / 60.0f);
    REQUIRE_FALSE(world.get_system<mosaic::engine::ui::UiSystem>()->press_consumed());
}
