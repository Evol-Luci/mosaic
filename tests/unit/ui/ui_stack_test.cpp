#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::layout_slot;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::UiStack;

namespace
{
// The whole screen, so the arithmetic reads in the same units the design doc uses.
const Rect kFull{0.0f, 0.0f, 1.0f, 1.0f};
} // namespace

TEST_CASE("UiStack defaults divide the frame evenly with no gap or inset", "[ui][layout]")
{
    // Defaults are NOT a no-op, deliberately: adding the component is what asks for a
    // layout. What must be a no-op is a parent WITHOUT the component, which is
    // ui_layout's business rather than this function's.
    const UiStack s;
    REQUIRE(s.axis == 0);
    REQUIRE(s.spacing == Approx(0.0f));
    REQUIRE(s.padding == Approx(0.0f));
}

TEST_CASE("one child fills the whole frame", "[ui][layout]")
{
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 1);
    REQUIRE(r.x == Approx(0.0f));
    REQUIRE(r.y == Approx(0.0f));
    REQUIRE(r.w == Approx(1.0f));
    REQUIRE(r.h == Approx(1.0f));
}

TEST_CASE("a vertical stack splits the frame along y", "[ui][layout]")
{
    // Three children, no spacing: each gets a third, and together they EXACTLY tile the
    // frame. The tiling is the property worth asserting -- a seam or an overlap is the
    // visible failure, and both show up as the thirds not summing to 1.
    const Rect a = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 3);
    const Rect b = layout_slot(kFull, 0, 0.0f, 0.0f, 1, 3);
    const Rect c = layout_slot(kFull, 0, 0.0f, 0.0f, 2, 3);
    REQUIRE(a.h == Approx(1.0f / 3.0f));
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(b.y == Approx(1.0f / 3.0f));
    REQUIRE(c.y == Approx(2.0f / 3.0f));
    REQUIRE(a.w == Approx(1.0f));
    REQUIRE(a.y + a.h == Approx(b.y));
    REQUIRE(b.y + b.h == Approx(c.y));
    REQUIRE(c.y + c.h == Approx(1.0f));
}

TEST_CASE("a horizontal stack is the vertical result with the axes swapped", "[ui][layout]")
{
    // Pinned as a PROPERTY rather than as two independent expectations, so the two axes
    // cannot drift apart: a bug fixed on one and not the other is the likeliest way this
    // function goes wrong.
    for (std::size_t i = 0; i < 4; ++i)
    {
        const Rect v = layout_slot(kFull, 0, 0.1f, 0.05f, i, 4);
        const Rect h = layout_slot(kFull, 1, 0.1f, 0.05f, i, 4);
        INFO("index " << i);
        REQUIRE(h.x == Approx(v.y));
        REQUIRE(h.y == Approx(v.x));
        REQUIRE(h.w == Approx(v.h));
        REQUIRE(h.h == Approx(v.w));
    }
}

TEST_CASE("spacing eats from between the slots, not from the ends", "[ui][layout]")
{
    // Two children with a 0.2 gap: each slot is 0.4, the first starts flush at 0 and the
    // second ends flush at 1. A gap counted at the ends too would leave the stack floating
    // inside its frame, which looks like a padding bug and is not one.
    const Rect a = layout_slot(kFull, 0, 0.2f, 0.0f, 0, 2);
    const Rect b = layout_slot(kFull, 0, 0.2f, 0.0f, 1, 2);
    REQUIRE(a.y == Approx(0.0f));
    REQUIRE(a.h == Approx(0.4f));
    REQUIRE(b.y == Approx(0.6f));
    REQUIRE(b.h == Approx(0.4f));
    REQUIRE(b.y + b.h == Approx(1.0f));
}

TEST_CASE("padding insets all four sides of the frame", "[ui][layout]")
{
    // A fraction of the frame on each axis, like pos and size -- not absolute screen
    // units. One child so the inset is the only thing under test.
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.1f, 0, 1);
    REQUIRE(r.x == Approx(0.1f));
    REQUIRE(r.y == Approx(0.1f));
    REQUIRE(r.w == Approx(0.8f));
    REQUIRE(r.h == Approx(0.8f));
}

TEST_CASE("padding is applied within the frame, not the screen", "[ui][layout]")
{
    // A half-size frame offset into the screen. Everything scales with the frame, which is
    // what makes a stack nest correctly -- the arithmetic never refers to the screen.
    const Rect frame{0.5f, 0.25f, 0.5f, 0.5f};
    const Rect r = layout_slot(frame, 0, 0.0f, 0.1f, 0, 1);
    REQUIRE(r.x == Approx(0.5f + 0.05f));
    REQUIRE(r.y == Approx(0.25f + 0.05f));
    REQUIRE(r.w == Approx(0.4f));
    REQUIRE(r.h == Approx(0.4f));
}

TEST_CASE("an over-large padding gives a zero extent, never a negative one", "[ui][layout]")
{
    // padding 0.6 insets 1.2 of a 1.0 frame. A negative w/h would not merely look wrong:
    // resolve_in MULTIPLIES by it, so every descendant rect becomes garbage -- inverted,
    // off-screen, and hit-testing against nothing. Clamped to zero the element simply does
    // not appear, which is legible.
    const Rect r = layout_slot(kFull, 0, 0.0f, 0.6f, 0, 1);
    REQUIRE(r.w >= 0.0f);
    REQUIRE(r.h >= 0.0f);
    REQUIRE(r.w == Approx(0.0f));
    REQUIRE(r.h == Approx(0.0f));
}

TEST_CASE("an over-large spacing gives a zero slot, never a negative one", "[ui][layout]")
{
    // The same hazard on the other knob: three children with spacing 0.9 asks for 1.8 of
    // gap in a 1.0 frame.
    const Rect r = layout_slot(kFull, 0, 0.9f, 0.0f, 1, 3);
    REQUIRE(r.h >= 0.0f);
    REQUIRE(r.h == Approx(0.0f));
}

TEST_CASE("a negative spacing or padding is treated as zero", "[ui][layout]")
{
    // Both are reflected floats on unbounded inspector DragFloats, so a negative is one
    // drag away. A negative padding would OUTSET the slot past its frame -- a child
    // escaping its container, which reads as a layout engine bug rather than as a typo.
    const Rect pad = layout_slot(kFull, 0, 0.0f, -0.5f, 0, 1);
    REQUIRE(pad.x == Approx(0.0f));
    REQUIRE(pad.w == Approx(1.0f));
    const Rect sp = layout_slot(kFull, 0, -0.5f, 0.0f, 0, 2);
    REQUIRE(sp.h == Approx(0.5f));
}

TEST_CASE("a count of zero or an index past the end gives an empty rect", "[ui][layout]")
{
    // count == 0 is a division by zero if unguarded, and reachable: a stack parent whose
    // children were all destroyed this frame.
    const Rect none = layout_slot(kFull, 0, 0.0f, 0.0f, 0, 0);
    REQUIRE(none.w == Approx(0.0f));
    REQUIRE(none.h == Approx(0.0f));
    const Rect past = layout_slot(kFull, 0, 0.0f, 0.0f, 5, 3);
    REQUIRE(past.w == Approx(0.0f));
    REQUIRE(past.h == Approx(0.0f));
}

TEST_CASE("an out-of-range axis falls back to vertical", "[ui][layout]")
{
    // `axis` is a reflected int and a scene file can hold anything -- the same reason
    // to_anchor clamps. Falling back to a REAL layout rather than to an empty rect means a
    // bad scene value shows a stacked column, not an invisible UI.
    const Rect expected = layout_slot(kFull, 0, 0.0f, 0.0f, 1, 2);
    for (const int bad : {-1, 2, 7, 1000})
    {
        const Rect r = layout_slot(kFull, bad, 0.0f, 0.0f, 1, 2);
        INFO("axis = " << bad);
        REQUIRE(r.x == Approx(expected.x));
        REQUIRE(r.y == Approx(expected.y));
        REQUIRE(r.w == Approx(expected.w));
        REQUIRE(r.h == Approx(expected.h));
    }
}

TEST_CASE("UiStack is a registered built-in component", "[ui][layout]")
{
    // Registration is what makes a stack authorable from a scene file and from the
    // Inspector, which is the whole point of putting the knobs on a component rather than
    // in code. Unregistered, it would be reachable only from C++.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiStack"));
}

TEST_CASE("UiStack round-trips through reflection", "[ui][layout]")
{
    // Pins the member NAMES. A scene authored today has to load tomorrow, and a rename is
    // SILENT -- from_value leaves the field at its default, so a renamed `spacing` would
    // quietly become 0 and every stack in every scene would close up. Three distinct
    // non-default values, so a field read from the wrong member cannot pass.
    UiStack s;
    s.axis    = 1;
    s.spacing = 0.125f;
    s.padding = 0.0625f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    UiStack    back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.axis == 1);
    REQUIRE(back.spacing == Approx(0.125f));
    REQUIRE(back.padding == Approx(0.0625f));
}
