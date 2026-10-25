#pragma once

#include <cstddef>

#include "engine/ui/ui_rect.hpp"

namespace mosaic::engine::ui
{

// EVERY LAYOUT-CONTAINER COMPONENT AND ALL THE SLOT ARITHMETIC LIVES IN THIS ONE HEADER --
// UiStack, UiGrid and UiFlow. The file name is historical (UiStack shipped alone in U8a) and
// the single file is deliberate: the three divisions share a floored-input discipline and a
// never-negative-extent rule that must not diverge, and this project has already paid for
// the same arithmetic existing in three places once (the to_anchor triplication, see
// ui_rect.hpp). The world-aware half -- who a parent's children are and in what order -- is
// ui_layout.hpp for all three.

// A one-axis auto-layout container, authored on the PARENT entity.
//
// Its children's frames become evenly divided slots of the parent's rect, which is what
// makes each child's own anchor/pos/size keep working unchanged: layout narrows the frame
// and the existing arithmetic runs inside it. A child's authored `pos` therefore offsets
// it FROM its slot rather than being ignored -- the UI decomposition reserved that
// question for the human, and narrowing the frame turns out to answer it, because
// resolve_in already treats `pos` as an offset within the frame it is given.
//
// `spacing` and `padding` are fractions of the parent's frame, like `pos` and `size`, not
// absolute screen units -- so a stack nests correctly and the arithmetic never refers to
// the screen.
//
// A STACK NEEDS A UiPanel ON THE SAME ENTITY. resolve_parent_frame returns the whole SCREEN
// when a parent has no UiPanel, so a UiStack alone lays nothing out -- there is no rect to
// divide. A layout container must therefore carry a panel even when it should show no chrome
// of its own.
//
// THE AUTHORED WAY TO WRITE A LAYOUT-ONLY CONTAINER:
//
//     UiPanel{ ..., .alpha = 1.0f, .visible = false }
//
// Both halves are load-bearing, and each one is the answer to a thing that does not work:
//
//   .visible = false   hides THIS panel's own quad and nothing else. It does not inherit
//                      down the chain and it does not reach any other component, so the
//                      children render normally. It also does NOT make resolve_parent_frame
//                      treat the panel as absent, which is what makes the layout survive --
//                      see UiPanel::visible, property 1.
//   .alpha = 1.0f      because inherited_alpha multiplies ANCESTOR alphas down, and 1 is
//                      that product's identity. Author anything less and the children fade
//                      with the container they cannot see.
//
// WHAT DOES NOT WORK, AND WHY IT IS WRITTEN DOWN: UiPanel{alpha = 0} is the obvious reach,
// and it hides the whole subtree, because a container's alpha multiplies down to its
// children. This comment previously offered two workarounds for that -- a fully transparent
// `color`, or an alpha low enough to look invisible but above zero -- and named two possible
// real fixes, the `visible` bool "or an opt-out on alpha propagation". The opt-out was
// CONSIDERED AND DECLINED by the human verification pass of 2026-10-20 ("fading a container
// should fade what is inside it"; recorded in app/run/demo_scene.hpp beside the "HP" label
// and in docs/record/2026-10.md), so do not reach for it. `visible` shipped in U8b and is the
// whole answer; the two workarounds are gone with the problem.
struct UiStack
{
    // 0 = vertical (children stack downward), 1 = horizontal. An int rather than an enum,
    // matching `anchor` on all four UI components: the reflection layer and the scene
    // format already carry it that way, and a scene file can hold any int.
    int   axis    = 0;
    float spacing = 0.0f; // gap BETWEEN slots, as a fraction of the frame's main axis
    float padding = 0.0f; // inset on all four sides, as a fraction of the frame
};

template <class V>
void reflect(V& visitor, UiStack& s)
{
    visitor.member("axis", s.axis);
    visitor.member("spacing", s.spacing);
    visitor.member("padding", s.padding);
}

// The slot for child `index` of `count`, within `frame`.
//
// PURE ARITHMETIC, no world: this is where every edge case lives, and keeping it free of
// the ECS is what makes those cases cheap to pin. The world-aware half -- who the children
// are, and in what order -- is ui_layout.hpp.
//
// Returns a zero-extent rect at the frame's origin for `count == 0` or an out-of-range
// index. NEVER returns a negative extent: resolve_in MULTIPLIES by w/h, so a negative one
// would turn every descendant rect into garbage -- inverted, off-screen, hit-testing
// against nothing -- rather than into nothing at all.
inline Rect layout_slot(const Rect& frame, int axis, float spacing, float padding,
                        std::size_t index, std::size_t count)
{
    if (count == 0 || index >= count)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }

    // Floored rather than trusted. Both are reflected floats on unbounded inspector
    // DragFloats, and a negative padding would OUTSET the slot past its own frame -- a
    // child escaping its container, which reads as an engine bug rather than as a typo.
    const float pad      = padding > 0.0f ? padding : 0.0f;
    const float gap_frac = spacing > 0.0f ? spacing : 0.0f;

    const float ix = pad * frame.w;
    const float iy = pad * frame.h;
    float       inner_w = frame.w - 2.0f * ix;
    float       inner_h = frame.h - 2.0f * iy;
    if (inner_w < 0.0f) { inner_w = 0.0f; }
    if (inner_h < 0.0f) { inner_h = 0.0f; }
    const Rect inner{frame.x + ix, frame.y + iy, inner_w, inner_h};

    // Anything that is not 1 is vertical, so a scene file holding a stray int gets a real
    // column rather than an invisible UI -- the same posture to_anchor takes.
    const bool  horizontal = (axis == 1);
    const float extent     = horizontal ? inner.w : inner.h;

    // count - 1 gaps: spacing sits BETWEEN slots, never at the ends. Counted at the ends
    // too, the stack would float inside its frame and look like a padding bug.
    const float gap  = gap_frac * extent;
    const float gaps = gap * static_cast<float>(count - 1);
    float       slot = (extent - gaps) / static_cast<float>(count);
    if (slot < 0.0f) { slot = 0.0f; }

    const float off = (slot + gap) * static_cast<float>(index);
    if (horizontal)
    {
        return Rect{inner.x + off, inner.y, slot, inner.h};
    }
    return Rect{inner.x, inner.y + off, inner.w, slot};
}

// ---------------------------------------------------------------------------
// UiGrid and UiFlow (U8c). Two more containers, sharing every piece of machinery UiStack
// proved -- stack_children, child_layout_order, slot_for_child and the resolve_parent_frame
// hook -- and differing ONLY in the slot arithmetic below.
//
// THE LINE BETWEEN THEM, and it is the reason both exist rather than one:
//
//   UiGrid  divides BOTH axes by an AUTHORED number (`cols`, `rows`). grid_slot therefore
//           takes no child count at all, so a grid of 12 slots holding 5 children has
//           exactly the shape it has holding 12. That was the human's call on 2026-10-25;
//           the declined alternative (`cols` only, rows derived from the child count) would
//           make cell HEIGHT change whenever a child is created or destroyed -- an
//           inventory grid that reflows as it empties.
//   UiFlow  divides the MAIN axis by an authored number (`per_line`) and the CROSS axis by
//           a DERIVED one, ceil(count / per_line). Its line count therefore varies with the
//           child count, which is the feature.
//
// THEY SHARE ONE CORE ON PURPOSE. detail::cell_slot below is the whole arithmetic, and
// flow_slot reaches it by computing (cols, rows) from `per_line` and `count`. So yes: at a
// child count that exactly fills an authored grid, a flow produces the identical rects, and
// flow_slot IS grid_slot with one divisor derived. That is stated rather than hidden,
// because the distinction worth keeping is in what the SCENE AUTHORS, not in the pixels --
// and sharing the core is what stops the two from drifting apart the way the three
// to_anchor copies once did.

namespace detail
{
// One cell of a cols x rows division of `frame`, at column `col` and row `row`.
//
// PURE ARITHMETIC, no world, and every edge case of both containers lives here. It is the
// two-axis sibling of layout_slot and it takes the same positions, for the same reasons:
//
//   * An empty cell is a ZERO-EXTENT rect at the FRAME's origin -- for a non-positive
//     divisor and for an out-of-range column or row alike.
//   * `padding` and `spacing` are FLOORED, not trusted. Both are reflected floats on
//     unbounded inspector DragFloats, so a negative is one drag away: a negative padding
//     would OUTSET the cell past its own frame and a negative spacing would overlap the
//     cells, each reading as an engine bug rather than as a typo.
//   * NEVER a negative extent. resolve_in MULTIPLIES by w/h, so a negative one turns every
//     descendant rect into garbage -- inverted, off-screen, hit-testing against nothing --
//     rather than into nothing at all.
//   * Spacing sits BETWEEN cells and never at the ends: cols - 1 gaps across and rows - 1
//     down. Counted at the ends too, the grid would float inside its frame and look like a
//     padding bug.
//
// `spacing` is ONE fraction read against EACH axis's own inner extent, so a square grid in
// a wide frame has a wider gap between columns than between rows. That matches layout_slot,
// where spacing is a fraction of the main axis, and it keeps the arithmetic free of the
// screen's aspect ratio -- which is what lets a grid nest inside a stack unchanged.
inline Rect cell_slot(const Rect& frame, int cols, int rows, float spacing, float padding,
                      int col, int row)
{
    if (cols <= 0 || rows <= 0 || col < 0 || row < 0 || col >= cols || row >= rows)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }

    const float pad      = padding > 0.0f ? padding : 0.0f;
    const float gap_frac = spacing > 0.0f ? spacing : 0.0f;

    const float ix      = pad * frame.w;
    const float iy      = pad * frame.h;
    float       inner_w = frame.w - 2.0f * ix;
    float       inner_h = frame.h - 2.0f * iy;
    if (inner_w < 0.0f) { inner_w = 0.0f; }
    if (inner_h < 0.0f) { inner_h = 0.0f; }

    const float gap_x = gap_frac * inner_w;
    const float gap_y = gap_frac * inner_h;
    float       cw = (inner_w - gap_x * static_cast<float>(cols - 1)) /
                     static_cast<float>(cols);
    float       ch = (inner_h - gap_y * static_cast<float>(rows - 1)) /
                     static_cast<float>(rows);
    if (cw < 0.0f) { cw = 0.0f; }
    if (ch < 0.0f) { ch = 0.0f; }

    return Rect{frame.x + ix + (cw + gap_x) * static_cast<float>(col),
                frame.y + iy + (ch + gap_y) * static_cast<float>(row), cw, ch};
}
} // namespace detail

// A two-axis auto-layout container with a FIXED cell count, authored on the PARENT entity.
//
// `cols` and `rows` are BOTH authored, which is the whole character of this component: the
// grid's shape is a property of the scene and not of how many children happen to exist this
// frame. A child at index >= cols * rows gets no cell and is not placed.
//
// Like UiStack, a grid NEEDS A UiPanel ON THE SAME ENTITY -- resolve_parent_frame returns
// the whole SCREEN for a parent with no panel, so there would be no rect to divide. Author
// `UiPanel{.alpha = 1.0f, .visible = false}` for a container with no chrome of its own; see
// UiStack's comment above for why both halves are load-bearing.
struct UiGrid
{
    // A zero or negative divisor means NO CELLS, not one cell -- see cell_slot. The default
    // of 1 is the only honest no-op for a component whose divisors are authored.
    int   cols    = 1;
    int   rows    = 1;
    float spacing = 0.0f; // gap BETWEEN cells on both axes, as a fraction of that axis
    float padding = 0.0f; // inset on all four sides, as a fraction of the frame
};

template <class V>
void reflect(V& visitor, UiGrid& g)
{
    visitor.member("cols", g.cols);
    visitor.member("rows", g.rows);
    visitor.member("spacing", g.spacing);
    visitor.member("padding", g.padding);
}

// A one-axis auto-layout container that WRAPS, authored on the PARENT entity.
//
// THE WRAP RULE, settled by the U8c implementation: a flow fills `per_line` slots along its
// main axis and then starts a new line on the cross axis, so the number of lines is
// ceil(count / per_line) and it moves as children are created and destroyed. Nothing here
// measures a child -- wrapping on a child's own size would need content measurement, and
// content-sized slots are deliberately deferred because UiText has no height until its font
// atlas resolves.
//
// THE PARTIAL FINAL LINE, also settled here: its slots size AS IF THE LINE WERE FULL. Four
// children at 3 per line means the lone child on the second line is a third of the main axis
// and flush with the cell above it, NOT the whole line.
//
// Why that way round, and the alternative was defensible. Dividing the final line among the
// children actually present would make a cell's size change whenever a child is added or
// destroyed -- which is precisely the defect that got the "cols only" alternative declined
// for UiGrid, and refusing it there while accepting it here would make the pair incoherent.
// Keeping the within-line divisor authored leaves `per_line` as the ONE number that sizes a
// cell, and leaves only the line count varying with the collection. It also means the last
// line aligns with the lines above it, which is what a toolbar or an inventory wants.
struct UiFlow
{
    // 0 = pack DOWN a column and wrap into new columns to the right; 1 = pack ACROSS a row
    // and wrap into new rows downward. The same meaning `axis` has on UiStack, and the same
    // posture: anything that is not 1 is vertical, so a scene file holding a stray int gets
    // a real layout rather than an invisible UI.
    int   axis     = 0;
    // Slots per line along the main axis. A zero or negative value means NO SLOTS, the same
    // rule UiGrid's cols takes -- it is also the modulus, so it is the division flow_slot
    // would otherwise do by zero.
    int   per_line = 1;
    float spacing  = 0.0f; // gap BETWEEN slots on both axes, as a fraction of that axis
    float padding  = 0.0f; // inset on all four sides, as a fraction of the frame
};

template <class V>
void reflect(V& visitor, UiFlow& f)
{
    visitor.member("axis", f.axis);
    visitor.member("per_line", f.per_line);
    visitor.member("spacing", f.spacing);
    visitor.member("padding", f.padding);
}

// The cell for child `index` of a `cols` x `rows` grid, within `frame`. Row-major:
// slot(i) -> (i % cols, i / cols), fixed by the U8 decomposition.
//
// Takes NO child count, deliberately -- that absence IS the settled decision. A grid's cells
// cannot move as children come and go because there is no count to move them.
inline Rect grid_slot(const Rect& frame, int cols, int rows, float spacing, float padding,
                      std::size_t index)
{
    if (cols <= 0 || rows <= 0)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }
    const std::size_t slots = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
    if (index >= slots)
    {
        // NOT `index % slots`, which is the tempting simplification: it would draw the 13th
        // child of a 12-slot grid on top of the 1st, which is a wrong picture with no other
        // symptom. An authored grid that is too small simply does not place the overflow.
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }
    const int col = static_cast<int>(index % static_cast<std::size_t>(cols));
    const int row = static_cast<int>(index / static_cast<std::size_t>(cols));
    return detail::cell_slot(frame, cols, rows, spacing, padding, col, row);
}

// The slot for child `index` of `count`, within `frame`, packed `per_line` to a line and
// wrapped. See UiFlow above for the wrap rule and the partial-final-line decision.
//
// Takes the child count BECAUSE the cross-axis divisor is derived from it. That is the one
// difference from grid_slot and the whole difference between the two containers.
inline Rect flow_slot(const Rect& frame, int axis, int per_line, float spacing,
                      float padding, std::size_t index, std::size_t count)
{
    if (count == 0 || index >= count || per_line <= 0)
    {
        return Rect{frame.x, frame.y, 0.0f, 0.0f};
    }
    const std::size_t line_len = static_cast<std::size_t>(per_line);
    const std::size_t lines    = (count + line_len - 1) / line_len; // ceil, so >= 1
    const int         along    = static_cast<int>(index % line_len);
    const int         line     = static_cast<int>(index / line_len);

    // Anything that is not 1 is vertical, the posture UiStack and to_anchor both take.
    if (axis == 1)
    {
        // Across a row, wrapping downward: the authored divisor is the column count.
        return detail::cell_slot(frame, per_line, static_cast<int>(lines), spacing, padding,
                                 along, line);
    }
    // Down a column, wrapping rightward: the authored divisor is the ROW count, and the
    // derived line count becomes the columns. Expressed as the transpose of the case above
    // rather than as its own arithmetic, so the two axes cannot drift apart -- the bug
    // layout_slot's axis-swap property test exists to catch.
    return detail::cell_slot(frame, static_cast<int>(lines), per_line, spacing, padding,
                             line, along);
}

} // namespace mosaic::engine::ui
