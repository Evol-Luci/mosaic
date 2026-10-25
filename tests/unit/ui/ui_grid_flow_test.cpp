#include <cstddef>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::flow_slot;
using mosaic::engine::ui::grid_slot;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::UiFlow;
using mosaic::engine::ui::UiGrid;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiStack;

namespace
{
// The whole screen, so the arithmetic reads in the same units the design doc uses.
const Rect kFull{0.0f, 0.0f, 1.0f, 1.0f};

// A container entity carrying a panel that fills its frame exactly, so a child's resolved
// frame IS its slot with no anchor arithmetic in the way.
World::EntityID make_container(World& world)
{
    const auto e = world.create_entity();
    UiPanel    p;
    p.anchor = 0;
    p.pos    = {0.0f, 0.0f};
    p.size   = {1.0f, 1.0f};
    world.add_component<UiPanel>(e, p);
    return e;
}

World::EntityID make_child(World& world, World::EntityID parent, int order)
{
    const auto e = world.create_entity();
    UiPanel    p;
    p.order = order;
    world.add_component<UiPanel>(e, p);
    world.add_component<UiParent>(e, UiParent{parent});
    return e;
}
} // namespace

// ---------------------------------------------------------------------------
// UiGrid -- explicit cols AND rows, so a slot exists whether or not a child fills it.
// ---------------------------------------------------------------------------

TEST_CASE("UiGrid and UiFlow defaults are a single slot filling the frame", "[ui][layout]")
{
    // A one-slot default is the only honest no-op for a container whose divisors are
    // authored: zero would mean "no slots" (see the empty-rect cases below) and anything
    // larger would invent a layout nobody asked for. Adding the component is still what
    // asks for a layout -- a parent WITHOUT one is the real no-op, and that is
    // ui_layout's business.
    const UiGrid g;
    REQUIRE(g.cols == 1);
    REQUIRE(g.rows == 1);
    REQUIRE(g.spacing == Approx(0.0f));
    REQUIRE(g.padding == Approx(0.0f));

    const UiFlow f;
    REQUIRE(f.axis == 0);
    REQUIRE(f.per_line == 1);
    REQUIRE(f.spacing == Approx(0.0f));
    REQUIRE(f.padding == Approx(0.0f));

    const Rect only = grid_slot(kFull, 1, 1, 0.0f, 0.0f, 0);
    REQUIRE(only.x == Approx(0.0f));
    REQUIRE(only.y == Approx(0.0f));
    REQUIRE(only.w == Approx(1.0f));
    REQUIRE(only.h == Approx(1.0f));
}

TEST_CASE("a grid's cells exactly tile its frame", "[ui][layout]")
{
    // THE GATE'S OWN PROPERTY, asserted on the pure function so the demo scene's version of
    // it has something to agree with. 3 cols x 2 rows with no spacing or padding: every
    // cell is a third wide and a half high, the column edges meet with no seam and no
    // overlap, and the right/bottom edges land flush on the frame.
    //
    // A seam shows up as `x + w` falling short of the next cell's `x`; an overlap as it
    // overshooting. Both are the same assertion, which is why it is written as an edge
    // match rather than as six hand-computed rects.
    for (std::size_t row = 0; row < 2; ++row)
    {
        for (std::size_t col = 0; col < 3; ++col)
        {
            const std::size_t i = row * 3 + col;
            const Rect        c = grid_slot(kFull, 3, 2, 0.0f, 0.0f, i);
            INFO("cell " << i << " at col " << col << " row " << row);
            REQUIRE(c.w == Approx(1.0f / 3.0f));
            REQUIRE(c.h == Approx(0.5f));
            REQUIRE(c.x == Approx(static_cast<float>(col) / 3.0f));
            REQUIRE(c.y == Approx(static_cast<float>(row) * 0.5f));
            // Flush against the next cell on both axes, or against the frame edge.
            const float right  = c.x + c.w;
            const float bottom = c.y + c.h;
            REQUIRE(right == Approx(col == 2 ? 1.0f : static_cast<float>(col + 1) / 3.0f));
            REQUIRE(bottom == Approx(row == 1 ? 1.0f : 0.5f));
        }
    }
}

TEST_CASE("a grid walks its slots row-major", "[ui][layout]")
{
    // slot(i) -> (i % cols, i / cols), fixed by the decomposition. Column-major would place
    // every index but 0 and the diagonal somewhere else, so this is pinned as the index
    // mapping rather than inferred from the tiling case above.
    const Rect one = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 1);
    REQUIRE(one.x == Approx(1.0f / 3.0f)); // second COLUMN
    REQUIRE(one.y == Approx(0.0f));        // still the first row
    const Rect three = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 3);
    REQUIRE(three.x == Approx(0.0f));      // back to the first column
    REQUIRE(three.y == Approx(0.5f));      // second ROW
}

TEST_CASE("grid spacing sits between the cells and never at the ends", "[ui][layout]")
{
    // cols - 1 and rows - 1 gaps, the discipline layout_slot states for one axis. Counted
    // at the ends too, the grid would float inside its frame and look like a padding bug.
    // 3 cols with spacing 0.1 of the frame width: 0.2 of gap, 0.8 shared by three cells.
    const Rect first = grid_slot(kFull, 3, 2, 0.1f, 0.0f, 0);
    const Rect last  = grid_slot(kFull, 3, 2, 0.1f, 0.0f, 5);
    REQUIRE(first.x == Approx(0.0f));
    REQUIRE(first.y == Approx(0.0f));
    REQUIRE(first.w == Approx(0.8f / 3.0f));
    REQUIRE(first.h == Approx(0.45f)); // (1 - 0.1) / 2
    REQUIRE(last.x + last.w == Approx(1.0f));
    REQUIRE(last.y + last.h == Approx(1.0f));
}

TEST_CASE("grid padding insets all four sides within the frame", "[ui][layout]")
{
    // A fraction of the FRAME on each axis, like pos and size and like layout_slot's
    // padding -- never absolute screen units, which is what lets a grid nest inside a stack
    // without the arithmetic ever referring to the screen.
    const Rect frame{0.5f, 0.25f, 0.5f, 0.5f};
    const Rect c = grid_slot(frame, 2, 2, 0.0f, 0.1f, 0);
    REQUIRE(c.x == Approx(0.5f + 0.05f));
    REQUIRE(c.y == Approx(0.25f + 0.05f));
    REQUIRE(c.w == Approx(0.2f)); // (0.5 - 0.1) / 2
    REQUIRE(c.h == Approx(0.2f));
}

TEST_CASE("a grid with no columns or no rows yields an empty rect", "[ui][layout]")
{
    // cols/rows are the grid's SLOT COUNT, so a zero or negative one is the count == 0 case
    // layout_slot already answers with an empty rect at the frame's origin -- and it is
    // also the division this function would otherwise do by zero.
    //
    // Deliberately NOT the fallback posture `axis` takes (a bad axis falls back to a real
    // vertical layout). `axis` has no meaningful zero, so clamping it keeps a scene
    // visible; `cols = 0` literally asks for no cells, and inventing one would show a
    // layout the scene did not author.
    for (const int bad : {0, -1, -7})
    {
        INFO("cols = " << bad);
        const Rect no_cols = grid_slot(kFull, bad, 2, 0.0f, 0.0f, 0);
        REQUIRE(no_cols.x == Approx(0.0f));
        REQUIRE(no_cols.y == Approx(0.0f));
        REQUIRE(no_cols.w == Approx(0.0f));
        REQUIRE(no_cols.h == Approx(0.0f));
        const Rect no_rows = grid_slot(kFull, 2, bad, 0.0f, 0.0f, 0);
        REQUIRE(no_rows.w == Approx(0.0f));
        REQUIRE(no_rows.h == Approx(0.0f));
    }
}

TEST_CASE("a grid index past the last slot does not wrap onto the first", "[ui][layout]")
{
    // A grid of 12 slots holding 13 children is authorable, and the 13th must VANISH rather
    // than be drawn on top of the 1st. `index % (cols * rows)` is the tempting
    // simplification and it would silently overlap two children -- a bug with no symptom
    // but a wrong picture.
    const Rect past = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 6);
    REQUIRE(past.w == Approx(0.0f));
    REQUIRE(past.h == Approx(0.0f));
    const Rect far_past = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 97);
    REQUIRE(far_past.w == Approx(0.0f));
    REQUIRE(far_past.h == Approx(0.0f));
}

TEST_CASE("a grid never returns a negative extent", "[ui][layout]")
{
    // resolve_in MULTIPLIES by w/h, so a negative extent turns every descendant rect into
    // garbage -- inverted, off-screen, hit-testing against nothing -- rather than into
    // nothing at all. Both knobs can ask for more than the frame has: padding 0.6 insets
    // 1.2 of a 1.0 frame, and spacing 0.9 across 3 cols asks for 1.8 of gap.
    const Rect over_pad = grid_slot(kFull, 2, 2, 0.0f, 0.6f, 0);
    REQUIRE(over_pad.w >= 0.0f);
    REQUIRE(over_pad.h >= 0.0f);
    REQUIRE(over_pad.w == Approx(0.0f));
    REQUIRE(over_pad.h == Approx(0.0f));

    const Rect over_gap = grid_slot(kFull, 3, 3, 0.9f, 0.0f, 1);
    REQUIRE(over_gap.w >= 0.0f);
    REQUIRE(over_gap.h >= 0.0f);
    REQUIRE(over_gap.w == Approx(0.0f));
    REQUIRE(over_gap.h == Approx(0.0f));
}

TEST_CASE("a negative grid spacing or padding is treated as zero", "[ui][layout]")
{
    // Floored rather than trusted: both are reflected floats on unbounded inspector
    // DragFloats, so a negative is one drag away. A negative padding would OUTSET the cell
    // past its own frame, and a negative spacing would overlap the cells -- each reads as a
    // layout engine bug rather than as a typo.
    const Rect pad = grid_slot(kFull, 1, 1, 0.0f, -0.5f, 0);
    REQUIRE(pad.x == Approx(0.0f));
    REQUIRE(pad.y == Approx(0.0f));
    REQUIRE(pad.w == Approx(1.0f));
    REQUIRE(pad.h == Approx(1.0f));

    const Rect gap = grid_slot(kFull, 2, 2, -0.5f, 0.0f, 3);
    REQUIRE(gap.x == Approx(0.5f));
    REQUIRE(gap.y == Approx(0.5f));
    REQUIRE(gap.w == Approx(0.5f));
    REQUIRE(gap.h == Approx(0.5f));
}

// ---------------------------------------------------------------------------
// UiFlow -- packs `per_line` slots along one axis and wraps, so its LINE COUNT is derived
// from the child count. That derived divisor is the whole line between flow and grid.
// ---------------------------------------------------------------------------

TEST_CASE("a flow packs per_line slots along its axis then wraps", "[ui][layout]")
{
    // THE WRAP RULE: a flow fills `per_line` slots along its main axis and then starts a new
    // line on the cross axis. axis 1 is horizontal, matching UiStack, so per_line = 3 with
    // 6 children is two rows of three.
    for (std::size_t i = 0; i < 6; ++i)
    {
        const Rect c = flow_slot(kFull, 1, 3, 0.0f, 0.0f, i, 6);
        INFO("index " << i);
        REQUIRE(c.x == Approx(static_cast<float>(i % 3) / 3.0f));
        REQUIRE(c.y == Approx(static_cast<float>(i / 3) * 0.5f));
        REQUIRE(c.w == Approx(1.0f / 3.0f));
        REQUIRE(c.h == Approx(0.5f));
    }
}

TEST_CASE("a flow's line count follows the child count but a grid's does not",
          "[ui][layout]")
{
    // THE DISTINGUISHING PROPERTY, and the reason both containers exist. A grid's cross-axis
    // divisor is AUTHORED (`rows`), so grid_slot takes no child count at all and its cell
    // height cannot move. A flow's is DERIVED -- ceil(count / per_line) -- so adding a
    // fourth child to a 3-per-line flow halves every cell's height.
    //
    // If this ever reads "equal", one container has become a special case of the other and
    // one of them should be deleted rather than shipped.
    const Rect three_of_three = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 0, 3);
    REQUIRE(three_of_three.h == Approx(1.0f)); // one line
    const Rect one_of_four = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 0, 4);
    REQUIRE(one_of_four.h == Approx(0.5f));    // two lines
    const Rect one_of_seven = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 0, 7);
    REQUIRE(one_of_seven.h == Approx(1.0f / 3.0f)); // three lines

    // The grid, over the same counts, cannot move: there is no count to pass.
    const Rect grid_cell = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 0);
    REQUIRE(grid_cell.h == Approx(0.5f));
    REQUIRE(grid_cell.h != Approx(three_of_three.h));
}

TEST_CASE("a flow's partial final line sizes as if the line were full", "[ui][layout]")
{
    // SETTLED HERE: within-line division always uses `per_line`, never the number of
    // children that actually landed on that line. Four children at 3 per line means the
    // lone child on row 1 is a THIRD wide and flush left, not the full width.
    //
    // Why: the alternative (divide the final line among the children present) makes a cell's
    // width change whenever a child is added or destroyed -- which is exactly the defect
    // that got "cols only" declined for UiGrid. Keeping the within-line divisor authored
    // keeps `per_line` the one number that sizes a cell, and leaves only the LINE COUNT
    // varying with the collection.
    const Rect lone = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 3, 4);
    REQUIRE(lone.w == Approx(1.0f / 3.0f)); // NOT 1.0
    REQUIRE(lone.x == Approx(0.0f));        // flush with the cell above it
    REQUIRE(lone.y == Approx(0.5f));
    // And it lines up with the cell directly above, which is the point of the choice.
    const Rect above = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 0, 4);
    REQUIRE(lone.x == Approx(above.x));
    REQUIRE(lone.w == Approx(above.w));
}

TEST_CASE("a vertical flow is the horizontal result with the axes swapped", "[ui][layout]")
{
    // Pinned as a PROPERTY rather than as two independent expectations, exactly as
    // layout_slot's two axes are: a bug fixed on one axis and not the other is the likeliest
    // way this function goes wrong. axis 0 packs DOWN a column and wraps into new columns to
    // the right, which is UiStack's convention for the same field.
    for (std::size_t i = 0; i < 5; ++i)
    {
        const Rect v = flow_slot(kFull, 0, 2, 0.1f, 0.05f, i, 5);
        const Rect h = flow_slot(kFull, 1, 2, 0.1f, 0.05f, i, 5);
        INFO("index " << i);
        REQUIRE(h.x == Approx(v.y));
        REQUIRE(h.y == Approx(v.x));
        REQUIRE(h.w == Approx(v.h));
        REQUIRE(h.h == Approx(v.w));
    }
}

TEST_CASE("an out-of-range flow axis falls back to vertical", "[ui][layout]")
{
    // `axis` is a reflected int and a scene file can hold anything -- the same reason
    // to_anchor clamps and layout_slot falls back. A REAL layout rather than an empty rect
    // means a bad scene value shows a column of cells and not an invisible UI.
    const Rect expected = flow_slot(kFull, 0, 2, 0.0f, 0.0f, 1, 5);
    for (const int bad : {-1, 2, 7, 1000})
    {
        const Rect r = flow_slot(kFull, bad, 2, 0.0f, 0.0f, 1, 5);
        INFO("axis = " << bad);
        REQUIRE(r.x == Approx(expected.x));
        REQUIRE(r.y == Approx(expected.y));
        REQUIRE(r.w == Approx(expected.w));
        REQUIRE(r.h == Approx(expected.h));
    }
}

TEST_CASE("a flow with no slots per line yields an empty rect", "[ui][layout]")
{
    // The same rule as a grid's zero `cols`, and stated once for both: an authored zero or
    // negative DIVISOR means "no slots", which is the count == 0 case rather than an
    // axis-style clamp. per_line is also the modulus, so it is the division this function
    // would otherwise do by zero.
    for (const int bad : {0, -1, -4})
    {
        INFO("per_line = " << bad);
        const Rect r = flow_slot(kFull, 1, bad, 0.0f, 0.0f, 0, 5);
        REQUIRE(r.x == Approx(0.0f));
        REQUIRE(r.y == Approx(0.0f));
        REQUIRE(r.w == Approx(0.0f));
        REQUIRE(r.h == Approx(0.0f));
    }
}

TEST_CASE("a flow count of zero or an index past the end yields an empty rect",
          "[ui][layout]")
{
    // count == 0 is a division by zero in the line count if unguarded, and it is reachable:
    // a flow parent whose children were all destroyed this frame.
    const Rect none = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 0, 0);
    REQUIRE(none.w == Approx(0.0f));
    REQUIRE(none.h == Approx(0.0f));
    const Rect past = flow_slot(kFull, 1, 3, 0.0f, 0.0f, 5, 4);
    REQUIRE(past.w == Approx(0.0f));
    REQUIRE(past.h == Approx(0.0f));
}

TEST_CASE("a flow never returns a negative extent", "[ui][layout]")
{
    // The same hazard as the grid's, on a function whose cross-axis divisor is derived: a
    // 7-child flow at 3 per line is three lines, and spacing 0.9 asks for 1.8 of gap
    // between them.
    const Rect over_pad = flow_slot(kFull, 1, 2, 0.0f, 0.6f, 0, 4);
    REQUIRE(over_pad.w >= 0.0f);
    REQUIRE(over_pad.h >= 0.0f);
    REQUIRE(over_pad.w == Approx(0.0f));
    REQUIRE(over_pad.h == Approx(0.0f));

    const Rect over_gap = flow_slot(kFull, 1, 3, 0.9f, 0.0f, 1, 7);
    REQUIRE(over_gap.w >= 0.0f);
    REQUIRE(over_gap.h >= 0.0f);
    REQUIRE(over_gap.w == Approx(0.0f));
    REQUIRE(over_gap.h == Approx(0.0f));
}

TEST_CASE("a negative flow spacing or padding is treated as zero", "[ui][layout]")
{
    const Rect pad = flow_slot(kFull, 1, 1, 0.0f, -0.5f, 0, 1);
    REQUIRE(pad.x == Approx(0.0f));
    REQUIRE(pad.w == Approx(1.0f));
    REQUIRE(pad.h == Approx(1.0f));

    const Rect gap = flow_slot(kFull, 1, 2, -0.5f, 0.0f, 3, 4);
    REQUIRE(gap.x == Approx(0.5f));
    REQUIRE(gap.y == Approx(0.5f));
    REQUIRE(gap.w == Approx(0.5f));
    REQUIRE(gap.h == Approx(0.5f));
}

// ---------------------------------------------------------------------------
// The ECS half: the SAME enumeration, ordering and hook U8a shipped, dispatching on two
// more container components. Nothing here is new machinery.
// ---------------------------------------------------------------------------

TEST_CASE("a UiGrid parent resolves each child into its own cell", "[ui][layout]")
{
    // The hook, through resolve_parent_frame, so the text path (which calls it directly)
    // and the panel path (which calls it through resolve_ui_rect) cannot disagree.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p, UiGrid{.cols = 3, .rows = 2, .spacing = 0.0f,
                                          .padding = 0.0f});
    std::vector<World::EntityID> kids;
    for (int i = 0; i < 6; ++i) { kids.push_back(make_child(world, p, 10 * i)); }

    for (std::size_t i = 0; i < kids.size(); ++i)
    {
        const Rect frame = mosaic::engine::ui::resolve_parent_frame(world, kids[i]);
        const Rect want  = grid_slot(kFull, 3, 2, 0.0f, 0.0f, i);
        INFO("child " << i);
        REQUIRE(frame.x == Approx(want.x));
        REQUIRE(frame.y == Approx(want.y));
        REQUIRE(frame.w == Approx(want.w));
        REQUIRE(frame.h == Approx(want.h));
        // Guards the degenerate pass where every frame collapsed to the screen rect.
        REQUIRE(frame.w < 1.0f);
    }
}

TEST_CASE("a grid's cells do not move when the child count changes", "[ui][layout]")
{
    // THE SETTLED DECISION, asserted where it is actually observable: grid_slot has no count
    // parameter to vary, so only the ECS half can see this. A grid of 6 slots must keep the
    // shape it has when full however few children remain -- the "inventory grid that reflows
    // as it empties" defect that got the cols-only alternative declined.
    //
    // THE COUNTS MATTER AND THE FIRST VERSION OF THIS TEST GOT THEM WRONG. It compared 6
    // children with 5, and the declined alternative (rows = ceil(count / cols)) gives
    // ceil(5/3) == 2 == the authored rows, so the mutation passed the test. 3 children is the
    // count that distinguishes them: ceil(3/3) == 1, which would double every cell's height.
    // 5 is kept as the weaker claim it is -- that a slot exists whether or not a child fills
    // it -- and 3 is the one that pins the decision.
    const auto build = [](World& w, int n)
    {
        const auto p = make_container(w);
        w.add_component<UiGrid>(p, UiGrid{.cols = 3, .rows = 2, .spacing = 0.0f,
                                          .padding = 0.0f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < n; ++i) { kids.push_back(make_child(w, p, 10 * i)); }
        return kids;
    };
    World      six_world;
    const auto six   = build(six_world, 6);
    World      five_world;
    const auto five  = build(five_world, 5);
    World      three_world;
    const auto three = build(three_world, 3);

    for (std::size_t i = 0; i < 3; ++i)
    {
        const Rect full      = mosaic::engine::ui::resolve_parent_frame(six_world, six[i]);
        const Rect one_short = mosaic::engine::ui::resolve_parent_frame(five_world, five[i]);
        const Rect half_full = mosaic::engine::ui::resolve_parent_frame(three_world,
                                                                       three[i]);
        INFO("child " << i);
        REQUIRE(full.h == Approx(0.5f)); // a real two-row cell
        // Dropping to ONE FULL ROW of children must not grow the cells: this is the
        // assertion the declined alternative fails, reporting 1.0 against 0.5.
        REQUIRE(half_full.h == Approx(full.h));
        REQUIRE(half_full.y == Approx(full.y));
        REQUIRE(half_full.x == Approx(full.x));
        REQUIRE(half_full.w == Approx(full.w));
        REQUIRE(one_short.h == Approx(full.h));
        REQUIRE(one_short.x == Approx(full.x));
        REQUIRE(one_short.y == Approx(full.y));
        REQUIRE(one_short.w == Approx(full.w));
    }
    // And the 5-child grid still has its 6th slot: index 5 is in range and non-empty, so an
    // emptying grid keeps its shape rather than closing up.
    const Rect sixth = grid_slot(kFull, 3, 2, 0.0f, 0.0f, 5);
    REQUIRE(sixth.w == Approx(1.0f / 3.0f));
    REQUIRE(sixth.h == Approx(0.5f));
}

TEST_CASE("a UiFlow parent wraps its children and reflows as the count changes",
          "[ui][layout]")
{
    // The mirror of the case above, and the behaviour that makes UiFlow a different feature
    // rather than a grid with one field renamed.
    const auto build = [](World& w, int n)
    {
        const auto p = make_container(w);
        w.add_component<UiFlow>(p, UiFlow{.axis = 1, .per_line = 3, .spacing = 0.0f,
                                          .padding = 0.0f});
        std::vector<World::EntityID> kids;
        for (int i = 0; i < n; ++i) { kids.push_back(make_child(w, p, 10 * i)); }
        return kids;
    };
    World      three_world;
    const auto three = build(three_world, 3);
    World      four_world;
    const auto four  = build(four_world, 4);

    const Rect one_line = mosaic::engine::ui::resolve_parent_frame(three_world, three[0]);
    REQUIRE(one_line.h == Approx(1.0f));
    const Rect two_lines = mosaic::engine::ui::resolve_parent_frame(four_world, four[0]);
    REQUIRE(two_lines.h == Approx(0.5f));
    // The fourth child wrapped onto a second row and is a third wide, not a whole row.
    const Rect wrapped = mosaic::engine::ui::resolve_parent_frame(four_world, four[3]);
    REQUIRE(wrapped.y == Approx(0.5f));
    REQUIRE(wrapped.w == Approx(1.0f / 3.0f));
}

TEST_CASE("a parent with no layout component still resolves to its whole rect",
          "[ui][layout]")
{
    // Adding two more container types must not change what a parent carrying NONE of them
    // does -- slot_for_child returns nothing and the child resolves against the parent's
    // whole rect, exactly as before U8a.
    World      world;
    const auto p = make_container(world);
    const auto a = make_child(world, p, 0);
    make_child(world, p, 10);
    const Rect frame = mosaic::engine::ui::resolve_parent_frame(world, a);
    REQUIRE(frame.x == Approx(0.0f));
    REQUIRE(frame.y == Approx(0.0f));
    REQUIRE(frame.w == Approx(1.0f));
    REQUIRE(frame.h == Approx(1.0f));
}

TEST_CASE("a parent carrying two layout components uses a fixed precedence", "[ui][layout]")
{
    // Authoring two containers on one entity is a mistake, but it must be a DETERMINISTIC
    // mistake: the same scene has to lay out the same way whatever order its components
    // were added in. UiStack wins over UiGrid wins over UiFlow -- the same discipline
    // child_layout_order states for its three components, and for the same reason.
    //
    // Asserted as equality with the stack-only result, so it cannot drift if the
    // arithmetic changes: the claim is "the stack decided this".
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p, UiGrid{.cols = 3, .rows = 2, .spacing = 0.0f,
                                          .padding = 0.0f});
    world.add_component<UiStack>(p, UiStack{.axis = 0, .spacing = 0.0f, .padding = 0.0f});
    const auto a = make_child(world, p, 0);
    make_child(world, p, 10);

    const Rect frame = mosaic::engine::ui::resolve_parent_frame(world, a);
    const Rect stacked = mosaic::engine::ui::layout_slot(kFull, 0, 0.0f, 0.0f, 0, 2);
    REQUIRE(frame.x == Approx(stacked.x));
    REQUIRE(frame.y == Approx(stacked.y));
    REQUIRE(frame.w == Approx(stacked.w));
    REQUIRE(frame.h == Approx(stacked.h));
    REQUIRE(frame.w == Approx(1.0f)); // a column, not a third-wide cell
}

TEST_CASE("a grid of two hundred and fifty six children lays out from a test alone",
          "[ui][layout]")
{
    // U8D'S INSTRUMENT. The point of this case is not the assertion -- it is that the whole
    // layout path (stack_children's for_each over the world, the sort, slot_for_child, the
    // hook) can be driven at a realistic inventory-grid size from a fixture, with no demo
    // scene, no renderer and no window. That is the seam U8d measures across.
    //
    // DELIBERATELY UNCACHED. resolve_parent_frame calls stack_children per child, so this
    // is O(N^2) by construction -- 256 children is ~65k component visits and 256 heap
    // allocations. Making that cheap is U8d's slice and its gate is a measured before/after,
    // so anything memoised here would destroy the measurement.
    //
    // What it does assert is the property a cache would endanger: every child lands in its
    // OWN cell, and the 16x16 grid is exactly full so none of them vanishes.
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p, UiGrid{.cols = 16, .rows = 16, .spacing = 0.0f,
                                          .padding = 0.0f});
    std::vector<World::EntityID> kids;
    for (int i = 0; i < 256; ++i) { kids.push_back(make_child(world, p, i)); }

    const auto enumerated = mosaic::engine::ui::stack_children(world, p);
    REQUIRE(enumerated.size() == 256);

    int distinct = 0;
    for (std::size_t i = 0; i < kids.size(); ++i)
    {
        const Rect frame = mosaic::engine::ui::resolve_parent_frame(world, kids[i]);
        const Rect want  = grid_slot(kFull, 16, 16, 0.0f, 0.0f, i);
        INFO("child " << i);
        REQUIRE(frame.x == Approx(want.x));
        REQUIRE(frame.y == Approx(want.y));
        REQUIRE(frame.w == Approx(1.0f / 16.0f));
        REQUIRE(frame.h == Approx(1.0f / 16.0f));
        ++distinct;
    }
    REQUIRE(distinct == 256);
}

TEST_CASE("a grid child carrying only a UiButton has no authorable layout order",
          "[ui][layout]")
{
    // A CHARACTERISATION TEST, not a TDD-red one: this pins today's behaviour so the gap is
    // recorded rather than discovered later. It is green before this slice's change and that
    // is on purpose -- the mutation that bites it is deleting UiPanel's branch from
    // child_layout_order, which makes the FIRST assertion fire (20 would no longer beat 10).
    //
    // WHY IT MATTERS TO U8c SPECIFICALLY. child_layout_order reads UiPanel, then UiImage,
    // then UiText, and NOT UiButton -- so a child that is a bare hit region with no visual
    // falls through to `return 0` and is ordered only by the entity-index tiebreak, which an
    // author cannot control. An inventory grid of clickable slots is the obvious shape for
    // this feature, which is why the gap surfaces here and did not surface for U8a's stack.
    //
    // It is deliberately NOT fixed here. UiButton::order is a HIT-RESOLUTION z-order, a
    // different concept from the draw order child_layout_order sorts by, and
    // ui_button_rects/ui_order_check exist because those two already disagree -- a
    // disagreement that mis-routes a click. Importing it into LAYOUT would make the same
    // authoring slip RELOCATE an element instead. In practice an inventory slot carries a
    // panel or an image for its visual (UiButton's own header says to overlay one), so the
    // co-located component's order ranks it; the gap bites only a child that draws nothing.
    // Recorded in the backlog's UI-pillar section.
    using mosaic::engine::ui::child_layout_order;
    using mosaic::engine::ui::UiButton;
    World      world;
    const auto p = make_container(world);
    world.add_component<UiGrid>(p, UiGrid{.cols = 2, .rows = 1, .spacing = 0.0f,
                                          .padding = 0.0f});

    // A child WITH a visual: its panel's order ranks it, which is the normal case.
    const auto visual = make_child(world, p, 20);
    REQUIRE(child_layout_order(world, visual) == 20);

    // A child with ONLY a button, authored with a high UiButton::order that layout ignores.
    const auto bare = world.create_entity();
    UiButton   b;
    b.order = 99;
    world.add_component<UiButton>(bare, b);
    world.add_component<UiParent>(bare, UiParent{p});
    REQUIRE(child_layout_order(world, bare) == 0); // NOT 99 -- UiButton is not consulted

    // So it sorts FIRST despite asking to be last, and the only thing deciding between two
    // such children is the entity index.
    const auto kids = mosaic::engine::ui::stack_children(world, p);
    REQUIRE(kids.size() == 2);
    REQUIRE(kids[0] == bare);
    REQUIRE(kids[1] == visual);
}

// ---------------------------------------------------------------------------
// Reflection: registration is what makes a container authorable from a scene file and from
// the Inspector, which is the whole point of putting the knobs on a component.
// ---------------------------------------------------------------------------

TEST_CASE("UiGrid and UiFlow are registered built-in components", "[ui][layout]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiGrid"));
    REQUIRE(types.has("UiFlow"));
}

TEST_CASE("UiGrid round-trips through reflection", "[ui][layout]")
{
    // Pins the member NAMES. A scene authored today has to load tomorrow and a rename is
    // SILENT -- from_value leaves the field at its default, so a renamed `cols` would
    // quietly become 1 and every grid in every scene would become a single column. Four
    // distinct non-default values, so a field read from the wrong member cannot pass.
    UiGrid g;
    g.cols    = 4;
    g.rows    = 3;
    g.spacing = 0.125f;
    g.padding = 0.0625f;

    const auto value = mosaic::foundation::serialization::to_value(g);
    UiGrid     back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.cols == 4);
    REQUIRE(back.rows == 3);
    REQUIRE(back.spacing == Approx(0.125f));
    REQUIRE(back.padding == Approx(0.0625f));
}

TEST_CASE("UiFlow round-trips through reflection", "[ui][layout]")
{
    UiFlow f;
    f.axis     = 1;
    f.per_line = 5;
    f.spacing  = 0.125f;
    f.padding  = 0.0625f;

    const auto value = mosaic::foundation::serialization::to_value(f);
    UiFlow     back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.axis == 1);
    REQUIRE(back.per_line == 5);
    REQUIRE(back.spacing == Approx(0.125f));
    REQUIRE(back.padding == Approx(0.0625f));
}
