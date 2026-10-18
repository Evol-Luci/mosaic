#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/ui/ui_hover_style.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/json_backend.hpp"
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

    // UiSystem is owned by the World, so reach it the way the production caller does.
    // NOT const: World::get_system<S>() is a non-const member returning S*.
    bool press_consumed_for_test()
    {
        UiSystem* s = world.get_system<UiSystem>();
        return s != nullptr && s->press_consumed();
    }

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

    // The first event carrying `action`, or nullptr. For asserting on a payload rather than
    // only on which channel fired.
    const ScriptEvent* find_event_with_action(const std::string& action) const
    {
        for (const auto& ev : events)
        {
            if (!ev.payload.is<Table>()) { continue; }
            const auto* a = ev.payload.as<Table>().find("action");
            if (a != nullptr && a->is<std::string>() && a->as<std::string>() == action)
            {
                return &ev;
            }
        }
        return nullptr;
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

TEST_CASE("the hovered button carries a UiHovered tag", "[ui][input]")
{
    Harness h;
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.ptr = {0.05f, 0.05f, false}; h.tick(); // hover it
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.ptr = {0.9f, 0.9f, false};   h.tick(); // leave
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("only the topmost of two overlapping buttons is tagged", "[ui][input]")
{
    // The tag must follow the SAME resolution the click does, or an element would highlight
    // while a different one answered the click -- the UI lying rather than a bug.
    Harness h;
    const auto top = h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(top));
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("moving between buttons moves the tag", "[ui][input]")
{
    // Two non-overlapping buttons, so one pointer move crosses from one into the other. The
    // tag must come OFF the first in the same update it goes onto the second; a hover_exit
    // that forgets the removal leaves two elements highlighted at once.
    Harness h;
    const auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.5f, 0.5f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "second";
    h.world.add_component<UiButton>(e, b);

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.55f, 0.55f, false}; h.tick();

    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(e));
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("a button that loses its UiButton while hovered leaves no stale tag",
          "[ui][input]")
{
    // Extends the U6 edge case. The hover_exit EVENT is deliberately silent there, because
    // the channel name lived on the component that is gone -- but the TAG must still come
    // off, or the element stays highlighted forever with nothing able to clear it.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.world.remove_component<UiButton>(h.button);
    h.tick();
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("a saved scene contains no UiHovered even while hovering", "[ui][input]")
{
    // The persistence trap, asserted end to end rather than trusted to the missing
    // registration line: serialize a world WHILE an element is hovered and confirm the
    // marker does not appear in the output.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    const auto value = mosaic::engine::scene::to_value(h.world, types);

    const std::string text = mosaic::foundation::serialization::to_json(value);
    // POSITIVE CONTROL FIRST. Without it, `find(...) == npos` is satisfied by any output
    // that lacks the string -- including empty output -- so a regression in to_value or
    // to_json would leave this test passing while claiming to have verified the trap.
    REQUIRE(text.find("UiButton") != std::string::npos);
    REQUIRE(text.find("UiHovered") == std::string::npos);
}

TEST_CASE("a captured button stops another from hovering", "[ui][input]")
{
    // CAPTURE IS A ROUTING RULE. While a press is held, the pointer belongs to the button
    // it landed on, so dragging onto a neighbour must not light the neighbour up -- the
    // click is still going to the captured one, and feedback that says otherwise lies.
    //
    // The two buttons must overlap only PARTIALLY, which add_overlapping cannot express:
    // it places its button on the harness button's own rect, so a higher order would steal
    // the press itself and a lower one would never be topmost anywhere. A disjoint neighbour
    // at a higher order is the only arrangement where the press lands on A while B is
    // genuinely topmost later -- which is exactly the case capture has to suppress.
    using mosaic::engine::ui::UiHovered;
    Harness    h;
    const auto other = h.world.create_entity();
    UiButton   b;
    b.anchor = 0;
    b.pos    = Vec2{0.3f, 0.0f};
    b.size   = Vec2{0.2f, 0.2f}; // x 0.3..0.5, clear of the harness button's 0..0.2
    b.event  = "neighbour";
    b.order  = 5; // topmost where it sits
    h.world.add_component<UiButton>(other, b);

    // Press where ONLY the harness button is, so it takes the capture.
    h.ptr = {0.05f, 0.05f, true};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    // Drag onto the neighbour. Without capture it would hover, being topmost there.
    h.ptr = {0.35f, 0.05f, true};
    h.tick();
    REQUIRE_FALSE(h.world.has_component<UiHovered>(other));
    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button)); // off its own rect
    REQUIRE(h.names_with_action("hover_enter") == std::vector<std::string>{"test_click"});
}

TEST_CASE("a captured button un-hovers off its rect and re-hovers on return", "[ui][input]")
{
    // The affordance that tells a user whether releasing here will fire the button. The
    // capture is kept either way -- only the hover tag follows the cursor in and out.
    using mosaic::engine::ui::UiHovered;
    Harness h;

    h.ptr = {0.05f, 0.05f, true};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    h.ptr = {0.9f, 0.9f, true}; // dragged well outside, still held
    h.tick();
    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));

    h.ptr = {0.05f, 0.05f, true}; // dragged back in
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(h.button));

    // And the capture survived the round trip: releasing inside still clicks.
    h.ptr = {0.05f, 0.05f, false};
    h.tick();
    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"test_click"});
}

TEST_CASE("with no press hover still follows the cursor across buttons", "[ui][input]")
{
    // The shipped behaviour, unchanged. Capture only applies while a press is held.
    using mosaic::engine::ui::UiHovered;
    Harness    h;
    const auto other = h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false};
    h.tick();
    REQUIRE(h.world.has_component<UiHovered>(other)); // order 5 is topmost
    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));
}

TEST_CASE("an absent pointer never captures", "[ui][input]")
{
    // Headless `mosaic run` reports inside == false forever, and PointerState's `inside`
    // defaults to TRUE -- so a provider meaning "no pointer" must say so, and this pins that
    // the capture path honours it. Without it, a down-but-absent pointer would capture a
    // button nobody is pointing at.
    using mosaic::engine::ui::UiHovered;
    Harness h;
    h.ptr = PointerState{0.05f, 0.05f, true, false}; // down, but no pointer present
    h.tick();

    REQUIRE_FALSE(h.world.has_component<UiHovered>(h.button));
    REQUIRE_FALSE(h.press_consumed_for_test());
}

TEST_CASE("a press that never moves emits a click and no drag", "[ui][input]")
{
    // THE MOST COMMON INPUT THERE IS. If drag_start fired on press instead of on movement,
    // every click in the engine would also emit drag_start and drag_end, and no handler
    // could tell a click from a one-pixel drag.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("click") == std::vector<std::string>{"test_click"});
    REQUIRE(h.names_with_action("drag_start").empty());
    REQUIRE(h.names_with_action("drag").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
}

TEST_CASE("movement past the threshold emits one drag_start then a drag per frame",
          "[ui][input]")
{
    // drag_start is an EDGE, drag is a stream. A drag_start per frame would make a handler
    // re-initialise its state on every move, which is the bug this ordering prevents.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press, no movement yet
    h.ptr = {0.10f, 0.05f, true};  h.tick(); // 0.05 > 0.01 threshold
    h.ptr = {0.15f, 0.05f, true};  h.tick();

    REQUIRE(h.names_with_action("drag_start").size() == 1);
    REQUIRE(h.names_with_action("drag").size() == 1); // the second move only
}

TEST_CASE("movement below the threshold emits nothing until it is crossed", "[ui][input]")
{
    // The threshold exists so a hand tremor is still a click. Crossing it later must still
    // start the drag -- measured from the PRESS point, not from the previous frame, or a
    // slow drag would never accumulate enough to cross.
    Harness h;
    h.ptr = {0.050f, 0.050f, true}; h.tick();
    h.ptr = {0.055f, 0.050f, true}; h.tick(); // 0.005 < 0.01
    REQUIRE(h.names_with_action("drag_start").empty());

    h.ptr = {0.070f, 0.050f, true}; h.tick(); // 0.020 from the press point
    REQUIRE(h.names_with_action("drag_start").size() == 1);
}

TEST_CASE("a frame with no movement emits no drag", "[ui][input]")
{
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // identical position
    REQUIRE(h.names_with_action("drag").empty());
}

TEST_CASE("holding still after a drag frame emits no further drag", "[ui][input]")
{
    // The case above stops one frame too early to pin anything: it only ever compares
    // against the position drag_start recorded. This one moves TWICE, so the comparison has
    // to be against the previous DRAG frame -- which is the assignment a mutation probe
    // showed was unguarded.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick(); // press
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    h.ptr = {0.30f, 0.05f, true}; h.tick(); // drag
    h.ptr = {0.30f, 0.05f, true}; h.tick(); // held still -- must be silent
    h.ptr = {0.30f, 0.05f, true}; h.tick();

    REQUIRE(h.names_with_action("drag").size() == 1);
}

TEST_CASE("drag actions carry the pointer position and click does not", "[ui][input]")
{
    // A drag without coordinates cannot drive anything. A click WITH them would be a payload
    // change to a shape three shipped handlers already read.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.10f, true}; h.tick();

    const auto* start = h.find_event_with_action("drag_start");
    REQUIRE(start != nullptr);
    REQUIRE(start->payload.as<Table>().find("x") != nullptr);
    REQUIRE(start->payload.as<Table>().find("y") != nullptr);

    h.ptr = {0.20f, 0.10f, false}; h.tick();
    const auto* click = h.find_event_with_action("click");
    REQUIRE(click != nullptr);
    REQUIRE(click->payload.as<Table>().find("x") == nullptr);
}

TEST_CASE("releasing inside after a drag emits drag_end and then click", "[ui][input]")
{
    // Press-wiggle-release is a click to any user, so the drag does NOT suppress it. A
    // handler that wants drag-only behaviour filters on `action`, which every handler must
    // already do.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.15f, 0.05f, true};  h.tick(); // drag_start
    h.ptr = {0.15f, 0.05f, false}; h.tick(); // release, still inside the button

    REQUIRE(h.names_with_action("drag_end").size() == 1);
    REQUIRE(h.names_with_action("click").size() == 1);
    REQUIRE(h.names_with_action("drag_cancel").empty());
}

TEST_CASE("releasing outside the captured button emits drag_end and no click", "[ui][input]")
{
    // The pointer is still on the surface -- it just is not on the button any more. That is
    // a completed drag that happened to end elsewhere, not an abort.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.90f, 0.90f, true};  h.tick(); // dragged off the button, still present
    h.ptr = {0.90f, 0.90f, false}; h.tick();

    REQUIRE(h.names_with_action("drag_end").size() == 1);
    REQUIRE(h.names_with_action("click").empty());
    REQUIRE(h.names_with_action("drag_cancel").empty());
}

TEST_CASE("the pointer leaving the surface mid-drag cancels it", "[ui][input]")
{
    // down false WITH inside false is the editor provider's way of saying the cursor left
    // the viewport panel -- deliberately, so a drag cannot keep running while the user
    // clicks in the Inspector. Aborting and completing are different outcomes, and
    // conflating them is where drag code goes wrong.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    h.ptr = PointerState{0.20f, 0.05f, false, false}; // left the surface
    h.tick();

    REQUIRE(h.names_with_action("drag_cancel").size() == 1);
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("leaving the surface without dragging emits nothing", "[ui][input]")
{
    // No drag started, so there is none to cancel. Without the dragging_ gate this would
    // fire drag_cancel for a press the user simply moved away from.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = PointerState{0.05f, 0.05f, false, false}; h.tick();

    REQUIRE(h.names_with_action("drag_cancel").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("a press after a cancel starts a clean capture", "[ui][input]")
{
    // State leaking between presses would make the second drag inherit the first's threshold
    // origin, so a short second drag would start instantly or never.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick();                     // drag_start #1
    h.ptr = PointerState{0.20f, 0.05f, false, false}; h.tick(); // cancel

    h.ptr = {0.05f, 0.05f, true};  h.tick();                // fresh press
    h.ptr = {0.055f, 0.05f, true}; h.tick();                // below threshold
    REQUIRE(h.names_with_action("drag_start").size() == 1); // still just the first

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.names_with_action("click").size() == 1);
}

TEST_CASE("destroying the captured button mid-drag emits nothing further", "[ui][input]")
{
    // emit_button_action already returns false when the entity lost its UiButton -- the
    // channel name lived on the component, so there is no honest name to emit on. Capture
    // inherits that discipline rather than inventing a fallback channel.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start
    const auto before = h.names_with_action("drag_start").size();

    h.world.destroy_entity(h.button);
    h.tick(); // commits the deferred destruction, and runs UiSystem with a stale capture

    h.ptr = {0.30f, 0.05f, true};  h.tick();
    h.ptr = {0.30f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("drag_start").size() == before);
    REQUIRE(h.names_with_action("drag").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("a plain click after a completed drag emits no drag action", "[ui][input]")
{
    // dragging_ must be cleared when the press ends, or it leaks into the NEXT press: that
    // press would skip the threshold entirely (emitting drag without drag_start) and its
    // release would emit a second drag_end for a click that never dragged.
    //
    // Found by a mutation probe. The after-cancel test counts drag_starts, which a stale
    // flag leaves untouched -- it is the ABSENCE of the other two actions that pins this.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.20f, 0.05f, true};  h.tick(); // drag_start
    h.ptr = {0.20f, 0.05f, false}; h.tick(); // drag_end + click
    REQUIRE(h.names_with_action("drag_end").size() == 1);

    // A second press that never moves: a click, and nothing else.
    h.ptr = {0.05f, 0.05f, true};  h.tick();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("drag_end").size() == 1); // still just the first
    REQUIRE(h.names_with_action("drag").empty());
    REQUIRE(h.names_with_action("drag_start").size() == 1);
    REQUIRE(h.names_with_action("click").size() == 2);
}

TEST_CASE("a destroyed captured button releases the capture", "[ui][input]")
{
    // THE SPEC SAYS CAPTURE IS DROPPED when the captured entity is no longer valid, and the
    // first implementation did not do it -- it relied on emit_button_action returning false
    // and ignored the return, so captured_ stayed set until release.
    //
    // The cost was not a crash but a dead UI: with capture still held by a ghost, hover
    // resolves against an entity that no longer exists, so NOTHING can highlight for the
    // rest of the press. A user drags across a UI that has stopped responding, with no event
    // explaining why. press_consumed() also stayed true, so world-space picking kept
    // declining a press the UI no longer owned.
    using mosaic::engine::ui::UiHovered;
    Harness    h;
    const auto other = h.world.create_entity();
    UiButton   b;
    b.anchor = 0;
    b.pos    = Vec2{0.3f, 0.0f};
    b.size   = Vec2{0.2f, 0.2f}; // x 0.3..0.5, disjoint from the harness button
    b.event  = "neighbour";
    b.order  = 5;
    h.world.add_component<UiButton>(other, b);

    h.ptr = {0.05f, 0.05f, true}; h.tick(); // captures the harness button
    h.ptr = {0.20f, 0.05f, true}; h.tick(); // drag_start

    h.world.destroy_entity(h.button);
    h.tick(); // commits the destruction, and runs UiSystem with a dead capture

    // Still held, now over the neighbour. With the capture dropped this is ordinary hover.
    h.ptr = {0.35f, 0.05f, true}; h.tick();

    REQUIRE(h.world.has_component<UiHovered>(other));
    REQUIRE_FALSE(h.press_consumed_for_test());
}

TEST_CASE("resetting the press state abandons a gesture silently", "[ui][input]")
{
    // THE STALE-GESTURE BUG, and it is reachable in the shipped editor. While the editor is
    // stopped, World::tick never runs, so UiSystem freezes mid-press with was_down_ true.
    // Press the HUD button, drag past the threshold, hit Stop, then later Play or Step: the
    // first tick takes the RELEASE edge and emits drag_cancel -- or drag_end plus click if
    // the cursor happens to be back over the button -- for a gesture that ended seconds ago.
    //
    // The fix belongs in the editor, which knows about the play-state transition; UiSystem
    // only has to offer a way to abandon the gesture. SILENTLY, deliberately: the drag did
    // not end, it stopped existing, and emitting drag_cancel at reset time would fire a
    // handler into a world that is being stopped or started.
    Harness h;
    h.ptr = {0.05f, 0.05f, true};  h.tick(); // press, captures the button
    h.ptr = {0.15f, 0.05f, true};  h.tick(); // past the threshold -> drag_start
    REQUIRE(h.names_with_action("drag_start").size() == 1);

    auto* ui = h.world.get_system<UiSystem>();
    REQUIRE(ui != nullptr);
    ui->reset_press_state();
    h.events.clear();

    // The release the frozen system would otherwise have taken on its next tick.
    h.ptr = {0.15f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("drag_cancel").empty());
    REQUIRE(h.names_with_action("drag_end").empty());
    REQUIRE(h.names_with_action("click").empty());
}

TEST_CASE("resetting the press state releases the click claim", "[ui][input]")
{
    // press_consumed() is how ClickNudgeSystem declines a press the UI already took. If the
    // reset left captured_ set, the world would keep refusing world-space clicks after a
    // gesture that no longer exists -- the same stale state, in the other consumer.
    Harness h;
    h.ptr = {0.05f, 0.05f, true}; h.tick();
    auto* ui = h.world.get_system<UiSystem>();
    REQUIRE(ui != nullptr);
    REQUIRE(ui->press_consumed());

    ui->reset_press_state();
    REQUIRE_FALSE(ui->press_consumed());
}

TEST_CASE("resetting the press state does not disturb hover", "[ui][input]")
{
    // Hover is not part of the gesture. Clearing it would emit a spurious hover_enter on the
    // next tick for a cursor that never moved -- and if the cursor HAS moved away, the
    // ordinary hover path already emits hover_exit, which is correct rather than stale.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick(); // hover, no press
    REQUIRE(h.names_with_action("hover_enter").size() == 1);
    h.events.clear();

    auto* ui = h.world.get_system<UiSystem>();
    ui->reset_press_state();
    h.ptr = {0.05f, 0.05f, false}; h.tick();

    REQUIRE(h.names_with_action("hover_enter").empty());
    REQUIRE(h.names_with_action("hover_exit").empty());
}
