#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::script::ScriptEvent;
using mosaic::engine::ui::PointerState;
using mosaic::engine::ui::UiButton;
using mosaic::engine::ui::UiSystem;
using mosaic::foundation::math::Vec2;

namespace
{
// A World with one UiButton at TopLeft {0,0}..{0.2,0.2}, a UiSystem driven by a
// mutable pointer, and a sync capture of emitted ScriptEvents.
struct Harness
{
    World world;
    PointerState ptr;
    std::vector<ScriptEvent> events;

    Harness()
    {
        auto e = world.create_entity();
        UiButton b;
        b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
        b.event = "test_click";
        world.add_component<UiButton>(e, b);
        world.add_system<UiSystem>([this]() { return ptr; });
        world.events().subscribe<ScriptEvent>(
            [this](const ScriptEvent& ev) { events.push_back(ev); });
    }
    void tick() { world.tick(0.016f); }

    // A second button overlapping the first, at a caller-chosen order and event name, so a
    // test can assert WHICH of two stacked buttons answered.
    mosaic::foundation::EntityID add_overlapping(int order, const std::string& event)
    {
        auto e = world.create_entity();
        UiButton b;
        b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
        b.event = event;
        b.order = order;
        world.add_component<UiButton>(e, b);
        return e;
    }
};
} // namespace

TEST_CASE("UiSystem emits a click on press+release inside the same button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press inside (down edge)
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // release inside (up edge) -> click
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "test_click");
}

TEST_CASE("UiSystem does not click when release is outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press inside
    h.ptr = {0.9f, 0.9f, false};   h.tick(); // release outside -> no click
    REQUIRE(h.events.empty());
}

TEST_CASE("UiSystem does not click when press starts outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.9f, 0.9f, true};    h.tick(); // press outside
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // release inside -> no click
    REQUIRE(h.events.empty());
}

TEST_CASE("UiSystem emits nothing while the pointer stays up", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.empty());
}

TEST_CASE("a click goes to the topmost of two overlapping buttons", "[ui][input]")
{
    // The behaviour change, and it is a fix to shipped code rather than a new feature: the
    // press used to take the FIRST button under the pointer. It gets its own case rather
    // than riding inside a hover test.
    //
    // The harness's own button has order 0 and is created FIRST, so an implementation that
    // kept taking the first match would emit "test_click" here.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "on_top");
}

TEST_CASE("a click goes to the later of two buttons sharing an order", "[ui][input]")
{
    // The equal-order rule through the real system. The harness's button is order 0 and
    // first; the added one is order 0 and second, so "later wins" picks the added one --
    // which is also the one the panel pass would draw on top.
    Harness h;
    h.add_overlapping(0, "added_second");

    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "added_second");
}

TEST_CASE("a press on the topmost button still requires release inside it", "[ui][input]")
{
    // The press/release pairing must survive the z-order change: picking the topmost on
    // press must not make the release path match a different button.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, true}; h.tick();  // press on the topmost
    h.ptr = {0.9f, 0.9f, false};  h.tick();  // release far away -> no click at all
    REQUIRE(h.events.empty());
}
