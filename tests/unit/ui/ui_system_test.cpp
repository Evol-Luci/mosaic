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
using mosaic::foundation::serialization::Table;

namespace
{
// A World with one UiButton at TopLeft {0,0}..{0.2,0.2}, a UiSystem driven by a
// mutable pointer, and a sync capture of emitted ScriptEvents.
struct Harness
{
    World world;
    PointerState ptr;
    std::vector<ScriptEvent> events;
    mosaic::foundation::EntityID button; // the entity created in the constructor

    // What the UiSystem currently considers hovered, for the destroyed-while-hovered case.
    mosaic::foundation::EntityID hovered_entity() const { return button; }

    Harness()
    {
        auto e = world.create_entity();
        UiButton b;
        b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
        b.event = "test_click";
        world.add_component<UiButton>(e, b);
        button = e;
        world.add_system<UiSystem>([this]() { return ptr; });
        world.events().subscribe<ScriptEvent>(
            [this](const ScriptEvent& ev) { events.push_back(ev); });
    }
    void tick() { world.tick(0.016f); }

    // The names of events carrying `action`, in emission order.
    //
    // The button's event channel now carries hover as well as click, so a test that means
    // "a click happened" must SAY so. Counting every event on the channel was only ever a
    // proxy for that, and it is now a proxy for "a click happened and nothing else did" --
    // a different and weaker claim, since a hover enter is not a defect.
    std::vector<std::string> names_with_action(const std::string& action) const
    {
        std::vector<std::string> out;
        for (const auto& ev : events)
        {
            if (!ev.payload.is<Table>()) { continue; }
            const auto* a = ev.payload.as<Table>().find("action");
            if (a != nullptr && a->is<std::string>() && a->as<std::string>() == action)
            {
                out.push_back(ev.name);
            }
        }
        return out;
    }

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
    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"test_click"});
}

TEST_CASE("UiSystem does not click when release is outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press inside
    h.ptr = {0.9f, 0.9f, false};   h.tick(); // release outside -> no click
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("UiSystem does not click when press starts outside the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.9f, 0.9f, true};    h.tick(); // press outside
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // release inside -> no click
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("UiSystem emits nothing while the pointer stays up", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.names_with_action("click").empty());
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

    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"on_top"});
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

    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"added_second"});
}

TEST_CASE("a press on the topmost button still requires release inside it", "[ui][input]")
{
    // The press/release pairing must survive the z-order change: picking the topmost on
    // press must not make the release path match a different button.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, true}; h.tick();  // press on the topmost
    h.ptr = {0.9f, 0.9f, false};  h.tick();  // release far away -> no click at all
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("hover enter fires once when the pointer moves onto a button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "test_click"); // the button's own channel, not a new one
}

TEST_CASE("hover enter does not re-fire while the pointer sits still", "[ui][input]")
{
    // The rule that makes enter/exit worth more than a polled flag, and the natural wrong
    // implementation -- emit whenever the pointer is inside -- would look CORRECT in the
    // demo (the panel would simply stay bright) while flooding any script that counts.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.06f, 0.06f, false}; h.tick(); // moved, still inside
    h.ptr = {0.06f, 0.06f, false}; h.tick(); // not moved at all
    REQUIRE(h.events.size() == 1);
}

TEST_CASE("hover exit fires once when the pointer leaves the button", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter
    h.ptr = {0.9f, 0.9f, false};   h.tick(); // leave
    h.ptr = {0.95f, 0.95f, false}; h.tick(); // still outside -> nothing more
    REQUIRE(h.events.size() == 2);
}

TEST_CASE("the hover payload names the entity and the action", "[ui][input]")
{
    // Same payload shape the click path already emits, so a script filters on `action`
    // rather than learning a second convention.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].payload.is<Table>());
    const Table& table = h.events[0].payload.as<Table>();
    REQUIRE(table.find("action") != nullptr);
    REQUIRE(table.find("action")->as<std::string>() == "hover_enter");
    // Written as a uint64_t by the emitter, so read as one -- as<std::int64_t>() on a
    // uint64_t variant would throw rather than convert.
    REQUIRE(table.find("entity") != nullptr);
    REQUIRE(table.find("entity")->is<std::uint64_t>());
}

TEST_CASE("moving between two buttons emits exit before enter", "[ui][input]")
{
    // ORDER asserted, not just presence. Enter-before-exit leaves a script that tracks
    // "the currently hovered thing" holding B and then being told to clear A -- i.e.
    // clearing the wrong one, which presents as a lost exit event.
    //
    // The second button is placed so the two do NOT overlap, so a single pointer move
    // crosses from one into the other.
    Harness h;
    auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.5f, 0.5f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "second";
    h.world.add_component<UiButton>(e, b);

    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter the first
    h.events.clear();
    h.ptr = {0.55f, 0.55f, false}; h.tick(); // directly into the second

    REQUIRE(h.events.size() == 2);
    REQUIRE(h.events[0].name == "test_click"); // exit from the first, on ITS channel
    REQUIRE(h.events[0].payload.as<Table>().find("action")->as<std::string>() == "hover_exit");
    REQUIRE(h.events[1].name == "second");
    REQUIRE(h.events[1].payload.as<Table>().find("action")->as<std::string>() == "hover_enter");
}

TEST_CASE("hover tracks the pointer during a press that began on empty space", "[ui][input]")
{
    // Hover is independent of `down`. Coupling them would mean a drag starting on empty
    // space suppresses all hover, which reads as the UI freezing mid-gesture.
    Harness h;
    h.ptr = {0.9f, 0.9f, true};    h.tick(); // press outside any button
    h.events.clear();
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // drag onto the button, still down
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].payload.as<Table>().find("action")->as<std::string>() == "hover_enter");
}

TEST_CASE("a click and its hover both land on the topmost button", "[ui][input]")
{
    // press and hover share one candidate pass, so they cannot disagree. If they could, a
    // button would light up while a different one answered the click.
    Harness h;
    h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false}; h.tick();           // hover
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "on_top");

    h.events.clear();
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    // the click, on the same channel the hover used
    REQUIRE(!h.events.empty());
    REQUIRE(h.events.back().name == "on_top");
}

TEST_CASE("a button covered after the press no longer receives the click", "[ui][input]")
{
    // A behaviour change this task introduces deliberately, so it is pinned rather than
    // left as prose. The release path used to ask "is the pointer inside the button it
    // pressed"; it now asks "is that button still the TOPMOST under the pointer". If a
    // higher-order button appears over it between press and release, clicking the one
    // underneath would be clicking through something the user can see.
    //
    // Reachable because a scene can add or reorder UI at runtime -- which is exactly what
    // the hover subscriber in Task 5 does to a sibling component.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();   // press: only one button exists, so it wins
    h.add_overlapping(5, "on_top");            // a higher button appears over it
    h.ptr = {0.05f, 0.05f, false}; h.tick();   // release

    // No click on either: the pressed button is no longer topmost, and the new button was
    // never pressed.
    const bool any_click = [&] {
        for (const auto& ev : h.events)
        {
            const auto* a = ev.payload.as<Table>().find("action");
            if (a != nullptr && a->as<std::string>() == "click") { return true; }
        }
        return false;
    }();
    REQUIRE_FALSE(any_click);
}

TEST_CASE("a UiSystem with no pointer provider emits no hover at all", "[ui][input]")
{
    // The constructor takes a std::function that may be EMPTY, and update falls back to
    // PointerState{} -- which is {0, 0, false}, a real position inside any button anchored
    // TopLeft at {0,0}. The harness's button is exactly that. Harmless before this slice
    // (no press ever arrives); with hover it would emit a phantom hover_enter for a pointer
    // that does not exist.
    World world;
    std::vector<ScriptEvent> events;
    auto e = world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "phantom";
    world.add_component<UiButton>(e, b);
    world.add_system<UiSystem>(std::function<PointerState()>{}); // no provider
    world.events().subscribe<ScriptEvent>(
        [&](const ScriptEvent& ev) { events.push_back(ev); });

    world.tick(0.016f);
    world.tick(0.016f);
    REQUIRE(events.empty());
}

TEST_CASE("destroying a hovered button emits no exit and clears the state", "[ui][input]")
{
    // hovered_ holds an EntityID, not a component. If the entity goes away while hovered,
    // the exit event's channel name (b.event) is unknowable -- so emitting nothing is the
    // only honest option. What must NOT happen is hovered_ keeping the stale id, because
    // then the next genuine hover of a different button would be treated as "no change"
    // and emit no enter.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // enter the harness's button
    h.events.clear();

    h.world.destroy_entity(h.hovered_entity());
    h.tick();                                 // notices the hovered entity is gone
    REQUIRE(h.events.empty());                // no channel to emit on

    // A fresh button in the same place must still produce an enter.
    auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.0f, 0.0f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "replacement";
    h.world.add_component<UiButton>(e, b);
    h.tick();
    REQUIRE(h.events.size() == 1);
    REQUIRE(h.events[0].name == "replacement");
}

TEST_CASE("a provider reporting no pointer emits no hover for a button at the origin",
          "[ui][input]")
{
    // The end-to-end shape of the headless-run bug: the provider is NON-empty (so the
    // `if (!pointer_)` guard does not fire) but reports no pointer, and the button is a
    // DEFAULT UiButton whose rect (0,0)..(0.1,0.1) contains the origin.
    World world;
    std::vector<ScriptEvent> events;
    auto e = world.create_entity();
    UiButton b; // defaults: anchor TopLeft, pos {0,0}, size {0.1,0.1}
    b.event = "phantom";
    world.add_component<UiButton>(e, b);

    PointerState absent{};
    absent.inside = false;
    world.add_system<UiSystem>([absent] { return absent; });
    world.events().subscribe<ScriptEvent>(
        [&](const ScriptEvent& ev) { events.push_back(ev); });

    world.tick(0.016f);
    world.tick(0.016f);
    REQUIRE(events.empty());
}
