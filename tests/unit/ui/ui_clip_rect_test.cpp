#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_clip_rect.hpp"
#include "engine/ui/ui_layout_stats.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_stack.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::Anchor;
using mosaic::engine::ui::intersect_rects;
using mosaic::engine::ui::is_empty;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::resolve_clip;
using mosaic::engine::ui::same_clip;
using mosaic::engine::ui::take_layout_stats;
using mosaic::engine::ui::UiClipRect;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::engine::ui::UiStack;

namespace
{
// A panel occupying an explicit top-left rect of its frame, so every clip below is a number
// the reader can check by hand rather than an anchor to re-derive.
UiPanel panel_at(float x, float y, float w, float h)
{
    UiPanel p;
    p.anchor = static_cast<int>(Anchor::TopLeft);
    p.pos    = {x, y};
    p.size   = {w, h};
    return p;
}
} // namespace

// ---------------------------------------------------------------------------
// The pure arithmetic. No world, no hierarchy, no device.
// ---------------------------------------------------------------------------

TEST_CASE("intersect_rects returns the overlap of two rects", "[ui][clip]")
{
    const Rect r = intersect_rects(Rect{0.0f, 0.0f, 0.6f, 0.6f},
                                   Rect{0.4f, 0.2f, 0.6f, 0.6f});
    REQUIRE(r.x == Approx(0.4f));
    REQUIRE(r.y == Approx(0.2f));
    REQUIRE(r.w == Approx(0.2f)); // 0.6 - 0.4
    REQUIRE(r.h == Approx(0.4f)); // 0.6 - 0.2
    REQUIRE_FALSE(is_empty(r));
}

TEST_CASE("intersect_rects with a containing rect returns the contained one", "[ui][clip]")
{
    // The case that CANNOT tell intersection from "use the innermost": stated here so the
    // nested-clip cases below are not read as covering it.
    const Rect inner{0.25f, 0.25f, 0.1f, 0.1f};
    const Rect r = intersect_rects(Rect{0.0f, 0.0f, 1.0f, 1.0f}, inner);
    REQUIRE(r.x == Approx(inner.x));
    REQUIRE(r.y == Approx(inner.y));
    REQUIRE(r.w == Approx(inner.w));
    REQUIRE(r.h == Approx(inner.h));
}

TEST_CASE("intersect_rects is empty for disjoint rects", "[ui][clip]")
{
    const Rect r = intersect_rects(Rect{0.0f, 0.0f, 0.2f, 0.2f},
                                   Rect{0.5f, 0.5f, 0.2f, 0.2f});
    REQUIRE(is_empty(r));
    // CLAMPED, not left negative. A negative extent fed back in as `a` would hand the
    // min/max an inverted interval; see intersect_rects' own comment.
    REQUIRE(r.w == Approx(0.0f));
    REQUIRE(r.h == Approx(0.0f));
}

TEST_CASE("intersecting with an empty rect stays empty whichever side it is on", "[ui][clip]")
{
    // What makes folding a chain of ancestors left-to-right safe: an empty link cannot be
    // resurrected by a later ancestor that happens to contain it.
    const Rect empty = intersect_rects(Rect{0.0f, 0.0f, 0.2f, 0.2f},
                                       Rect{0.5f, 0.5f, 0.2f, 0.2f});
    const Rect full{0.0f, 0.0f, 1.0f, 1.0f};
    REQUIRE(is_empty(intersect_rects(empty, full)));
    REQUIRE(is_empty(intersect_rects(full, empty)));
}

TEST_CASE("intersect_rects touching edges yields a zero extent, not an overlap", "[ui][clip]")
{
    const Rect r = intersect_rects(Rect{0.0f, 0.0f, 0.5f, 1.0f},
                                   Rect{0.5f, 0.0f, 0.5f, 1.0f});
    REQUIRE(r.w == Approx(0.0f));
    REQUIRE(is_empty(r));
}

TEST_CASE("is_empty reports a negative extent on either axis", "[ui][clip]")
{
    // UiPanel::size is an unclamped reflected float, so a negative extent is one inspector
    // drag away and must not read as "a rect with some pixels in it".
    REQUIRE(is_empty(Rect{0.0f, 0.0f, -1.0f, 1.0f}));
    REQUIRE(is_empty(Rect{0.0f, 0.0f, 1.0f, -1.0f}));
    REQUIRE(is_empty(Rect{0.0f, 0.0f, 0.0f, 1.0f}));
    REQUIRE_FALSE(is_empty(Rect{0.0f, 0.0f, 0.001f, 0.001f}));
}

TEST_CASE("same_clip distinguishes unclipped from clipped and one rect from another",
          "[ui][clip]")
{
    // The HUD panel pass's batching key. A false positive here would merge two genuinely
    // different clips and draw one of them in the other's rect.
    const std::optional<Rect> none;
    const std::optional<Rect> a = Rect{0.1f, 0.2f, 0.3f, 0.4f};
    const std::optional<Rect> same = Rect{0.1f, 0.2f, 0.3f, 0.4f};
    const std::optional<Rect> differs_in_w = Rect{0.1f, 0.2f, 0.35f, 0.4f};

    REQUIRE(same_clip(none, none));
    REQUIRE(same_clip(a, same));
    REQUIRE_FALSE(same_clip(none, a));
    REQUIRE_FALSE(same_clip(a, none));
    REQUIRE_FALSE(same_clip(a, differs_in_w));
}

// ---------------------------------------------------------------------------
// resolution down the parent chain
// ---------------------------------------------------------------------------

TEST_CASE("resolve_clip is nothing when no ancestor clips", "[ui][clip]")
{
    // nullopt, NOT the whole screen: it is the value RenderDevice::UiClip reserves for
    // "record no scissor at all", and collapsing the two would make every UI draw in every
    // scene set one.
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    REQUIRE_FALSE(resolve_clip(world, child).has_value());
    // And a root element with no UiParent at all.
    REQUIRE_FALSE(resolve_clip(world, parent).has_value());
}

TEST_CASE("resolve_clip returns a single clipping ancestor's resolved rect", "[ui][clip]")
{
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, panel_at(0.1f, 0.2f, 0.4f, 0.3f));
    world.add_component<UiClipRect>(parent, UiClipRect{});

    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    const auto clip = resolve_clip(world, child);
    REQUIRE(clip.has_value());
    REQUIRE(clip->x == Approx(0.1f));
    REQUIRE(clip->y == Approx(0.2f));
    REQUIRE(clip->w == Approx(0.4f));
    REQUIRE(clip->h == Approx(0.3f));
}

TEST_CASE("resolve_clip does not clip the entity carrying UiClipRect itself", "[ui][clip]")
{
    // The component clips the SUBTREE. A container must draw its own chrome in full, or a
    // scroll region's frame would be masked by the region it defines.
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, panel_at(0.1f, 0.1f, 0.2f, 0.2f));
    world.add_component<UiClipRect>(parent, UiClipRect{});

    REQUIRE_FALSE(resolve_clip(world, parent).has_value());
}

TEST_CASE("resolve_clip intersects a NESTED clip with its ancestor's rather than taking the innermost",
          "[ui][clip]")
{
    // THE DECISIVE CASE OF U9b. The inner clip deliberately reaches OUTSIDE its ancestor's
    // rect, which is the only arrangement in which "intersect every ancestor" and "use the
    // innermost" give different answers -- and it is exactly the authored mistake a clip is
    // meant to contain.
    //
    // outer: a container occupying x in [0.0, 0.5), y in [0.0, 0.5) of the screen.
    // inner: a child of outer, authored pos {0.5, 0.5} size {1.0, 1.0} of its frame, so it
    //        resolves to x in [0.25, 0.75), y in [0.25, 0.75) -- half of it hangs out past
    //        outer's right and bottom edges.
    //
    // The intersection is x in [0.25, 0.5), y in [0.25, 0.5): w = h = 0.25.
    // "The innermost clip" would be inner's own 0.5 x 0.5, letting a nested region paint
    // over the panel that bounds it.
    World      world;
    const auto outer = world.create_entity();
    world.add_component<UiPanel>(outer, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    world.add_component<UiClipRect>(outer, UiClipRect{});

    const auto inner = world.create_entity();
    world.add_component<UiPanel>(inner, panel_at(0.5f, 0.5f, 1.0f, 1.0f));
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<UiClipRect>(inner, UiClipRect{});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{inner});

    // Stated first so the expectation below is not a restatement of the implementation:
    // inner's own rect is the thing that must NOT be the answer.
    const auto inner_only = resolve_clip(world, inner);
    REQUIRE(inner_only.has_value());
    REQUIRE(inner_only->w == Approx(0.5f)); // outer's clip, seen from inner

    const auto clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    REQUIRE(clip->x == Approx(0.25f));
    REQUIRE(clip->y == Approx(0.25f));
    REQUIRE(clip->w == Approx(0.25f)); // NOT 0.5 -- that is the innermost-only answer
    REQUIRE(clip->h == Approx(0.25f));
}

TEST_CASE("resolve_clip yields an empty clip when two nested clips are disjoint", "[ui][clip]")
{
    // The whole subtree is clipped away. The draw passes drop such an element entirely and
    // button_hit_candidates emits no candidate for it; both are asserted in their own files.
    World      world;
    const auto outer = world.create_entity();
    world.add_component<UiPanel>(outer, panel_at(0.0f, 0.0f, 0.5f, 1.0f));
    world.add_component<UiClipRect>(outer, UiClipRect{});

    const auto inner = world.create_entity();
    // pos {1.0, 0} size {1,1} of a frame that is the left half -> x in [0.5, 1.0), which
    // touches outer's right edge and overlaps it in nothing.
    world.add_component<UiPanel>(inner, panel_at(1.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<UiClipRect>(inner, UiClipRect{});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{inner});

    const auto clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    REQUIRE(is_empty(*clip));
}

TEST_CASE("a clip is unaffected by the clipped child's own anchor, pos and size", "[ui][clip]")
{
    // A clip must change NEITHER position nor size -- it is not resolve_parent_frame. The
    // converse is asserted here: moving and resizing the child cannot move its clip, so a
    // scroll offset applied to a child in U9d cannot drag the region with it.
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, panel_at(0.2f, 0.3f, 0.4f, 0.4f));
    world.add_component<UiClipRect>(parent, UiClipRect{});

    const auto a = world.create_entity();
    world.add_component<UiPanel>(a, panel_at(0.0f, 0.0f, 0.1f, 0.1f));
    world.add_component<UiParent>(a, UiParent{parent});

    const auto b = world.create_entity();
    UiPanel    moved = panel_at(0.7f, 0.9f, 1.0f, 1.0f);
    moved.anchor = static_cast<int>(Anchor::BottomRight);
    world.add_component<UiPanel>(b, moved);
    world.add_component<UiParent>(b, UiParent{parent});

    const auto ca = resolve_clip(world, a);
    const auto cb = resolve_clip(world, b);
    REQUIRE(ca.has_value());
    REQUIRE(same_clip(ca, cb));
    REQUIRE(ca->x == Approx(0.2f));
    REQUIRE(ca->w == Approx(0.4f));
}

TEST_CASE("an invisible container still clips its children", "[ui][clip]")
{
    // A clip is GEOMETRY; `visible` is chrome. U8b's layout-only container -- a UiPanel with
    // visible = false whose job is to lay children out showing nothing of itself -- must be
    // able to clip exactly as well as a visible one, or the authored way to write a
    // container would be the one way you cannot clip.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p = panel_at(0.0f, 0.0f, 0.5f, 0.5f);
    p.visible = false;
    world.add_component<UiPanel>(parent, p);
    world.add_component<UiClipRect>(parent, UiClipRect{});

    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    const auto clip = resolve_clip(world, child);
    REQUIRE(clip.has_value());
    REQUIRE(clip->w == Approx(0.5f));
}

TEST_CASE("a fully transparent container still clips its children", "[ui][clip]")
{
    // Same statement for alpha. An alpha-0 container draws nothing but still bounds its
    // subtree, and treating it as absent would let the children escape.
    World      world;
    const auto parent = world.create_entity();
    UiPanel    p = panel_at(0.0f, 0.0f, 0.5f, 0.5f);
    p.alpha = 0.0f;
    world.add_component<UiPanel>(parent, p);
    world.add_component<UiClipRect>(parent, UiClipRect{});

    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    REQUIRE(resolve_clip(world, child).has_value());
}

TEST_CASE("UiClipRect on an entity with no UiPanel contributes nothing", "[ui][clip]")
{
    // There is no rect to clip to. Treating "no rect" as the EMPTY rect would silently hide
    // a whole subtree for a reason nobody authored -- the same posture inherited_alpha takes
    // for an ancestor with no UiPanel, which contributes nothing rather than zero.
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiClipRect>(parent, UiClipRect{}); // no UiPanel

    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    REQUIRE_FALSE(resolve_clip(world, child).has_value());
}

TEST_CASE("a clip resolves through a non-clipping intermediate ancestor", "[ui][clip]")
{
    // The walk does not stop at the first ancestor. A grandchild of a clipping container is
    // clipped even when its own parent carries no UiClipRect, which is what "the subtree" in
    // the component's contract has to mean.
    World      world;
    const auto grand = world.create_entity();
    world.add_component<UiPanel>(grand, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    world.add_component<UiClipRect>(grand, UiClipRect{});

    const auto mid = world.create_entity();
    world.add_component<UiPanel>(mid, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(mid, UiParent{grand});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{mid});

    const auto clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    REQUIRE(clip->w == Approx(0.5f));
}

TEST_CASE("a clipping container's rect is its LAYOUT SLOT when its own parent lays it out",
          "[ui][clip]")
{
    // The clip comes from resolve_ui_rect, so it inherits the frame narrowing a container
    // above it performs. A clipping panel inside a stack clips to the slot it actually
    // occupies, not to the rect its own numbers describe in isolation -- which is the
    // behaviour a scroll region inside a layout needs, and the one that would break if the
    // clip walk resolved rects its own way.
    World      world;
    const auto stack = world.create_entity();
    world.add_component<UiPanel>(stack, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiStack>(stack, UiStack{.axis = 0, .spacing = 0.0f, .padding = 0.0f});

    const auto first = world.create_entity();
    world.add_component<UiPanel>(first, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(first, UiParent{stack});
    world.add_component<UiClipRect>(first, UiClipRect{});

    const auto second = world.create_entity();
    world.add_component<UiPanel>(second, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(second, UiParent{stack});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{first});

    // Two children, axis 0 (vertical), no spacing or padding -> the first slot is the top
    // half of the screen.
    const auto clip = resolve_clip(world, leaf);
    REQUIRE(clip.has_value());
    REQUIRE(clip->y == Approx(0.0f));
    REQUIRE(clip->h == Approx(0.5f));
    REQUIRE(clip->w == Approx(1.0f));
}

TEST_CASE("resolve_clip terminates on a UiParent cycle instead of recursing forever", "[ui][clip]")
{
    // Same kMaxDepth guard as resolve_parent_frame and inherited_alpha, and for the same
    // reason: UiParent is authored data and can name an ancestor.
    World      world;
    const auto a = world.create_entity();
    const auto b = world.create_entity();
    world.add_component<UiPanel>(a, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    world.add_component<UiPanel>(b, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    world.add_component<UiClipRect>(a, UiClipRect{});
    world.add_component<UiClipRect>(b, UiClipRect{});
    world.add_component<UiParent>(a, UiParent{b});
    world.add_component<UiParent>(b, UiParent{a});

    // Reaching this line at all is the assertion.
    const auto clip = resolve_clip(world, a);
    REQUIRE(clip.has_value());
}

TEST_CASE("a clip-free scene pays no layout resolves for the clip walk", "[ui][clip]")
{
    // PREMISE CHECK, and the reason ui_layout_cost_test.cpp's exact counts are still green
    // after U9b. resolve_clip probes each ancestor for UiClipRect and calls resolve_ui_rect
    // ONLY for one that carries it -- so in every scene shipped today, which authors no
    // clip anywhere, the layout counters read exactly what they read before this function
    // existed. A `clip_resolves` counter is an explicit U9 non-goal; this assertion is what
    // makes the existing counter's meaning survive without one.
    World      world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, panel_at(0.0f, 0.0f, 0.5f, 0.5f));
    const auto child = world.create_entity();
    world.add_component<UiPanel>(child, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(child, UiParent{parent});

    (void)take_layout_stats();
    const auto clip = resolve_clip(world, child);
    const auto s = take_layout_stats();
    REQUIRE_FALSE(clip.has_value());
    REQUIRE(s.resolves == 0);
    REQUIRE(s.scans == 0);
    REQUIRE(s.visits == 0);
    REQUIRE(s.container_probes == 0);
    REQUIRE(s.allocations == 0);

    // And the converse, so the assertion above is a measurement rather than a function that
    // never does anything: a scene that DOES clip spends resolves.
    world.add_component<UiClipRect>(parent, UiClipRect{});
    (void)take_layout_stats();
    REQUIRE(resolve_clip(world, child).has_value());
    const auto clipped = take_layout_stats();
    REQUIRE(clipped.resolves > 0);
}

TEST_CASE("UiClipRect is a registered built-in component", "[ui][clip]")
{
    // Registration is what makes a clip region authorable from a scene file and the
    // Inspector. Unregistered, it would be silently dropped on save -- a scene would load
    // with its clipping gone and nothing would say so.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiClipRect"));
}

TEST_CASE("UiClipRect round-trips through reflection as a bare marker", "[ui][clip]")
{
    // A fieldless component, like AudioListener: the round trip has no values to preserve,
    // and the thing being pinned is that it round-trips at all rather than failing to
    // serialize.
    UiClipRect c;
    const auto value = mosaic::foundation::serialization::to_value(c);
    UiClipRect back;
    mosaic::foundation::serialization::from_value(value, back);
    SUCCEED("a marker component serializes and deserializes without members");
}

// ---------------------------------------------------------------------------
// Exactness. A clip must change NEITHER the position nor the size of what survives it, and
// that sentence has to be true to the bit or it needs a footnote. These two cases exist
// because the obvious implementation makes it false by one ULP: intersect_rects recomputes
// an extent as (x + w) - x, which is not always w. The first version of
// "a button fully inside its clip keeps exactly its unclipped rect" in
// tests/unit/ui/ui_button_rects_test.cpp failed on exactly that, printing `0.2f == 0.2f`.
// ---------------------------------------------------------------------------

TEST_CASE("clip_rect returns a contained rect bit-identically", "[ui][clip]")
{
    // Values chosen so that (x + w) - x is NOT x's own w in float: 0.1 and 0.2 are both
    // inexact in binary.
    const Rect inner{0.1f, 0.30000001f, 0.2f, 0.45f};
    const Rect clip{0.05f, 0.1f, 0.9f, 0.8f};
    const Rect out = mosaic::engine::ui::clip_rect(inner, clip);
    REQUIRE(out.x == inner.x);
    REQUIRE(out.y == inner.y);
    REQUIRE(out.w == inner.w);
    REQUIRE(out.h == inner.h);

    // ...and the plain intersection is what would NOT be exact, so the case above is
    // measuring the fast path rather than restating float arithmetic that happens to work.
    const Rect naive = intersect_rects(inner, clip);
    REQUIRE(naive.x == inner.x); // the origins survive max() unchanged
    const bool extent_drifted = (naive.w != inner.w) || (naive.h != inner.h);
    INFO("naive w=" << naive.w << " vs " << inner.w << ", h=" << naive.h << " vs "
                    << inner.h);
    REQUIRE(extent_drifted);
}

TEST_CASE("clip_rect still trims a rect that hangs out of its clip", "[ui][clip]")
{
    // The fast path must not swallow the case the function exists for.
    const Rect out = mosaic::engine::ui::clip_rect(Rect{0.4f, 0.0f, 0.4f, 1.0f},
                                                   Rect{0.0f, 0.0f, 0.5f, 1.0f});
    REQUIRE(out.x == Approx(0.4f));
    REQUIRE(out.w == Approx(0.1f));
}

TEST_CASE("contains_rect is the containment the fast path keys on", "[ui][clip]")
{
    const Rect outer{0.0f, 0.0f, 1.0f, 1.0f};
    REQUIRE(mosaic::engine::ui::contains_rect(outer, Rect{0.1f, 0.1f, 0.2f, 0.2f}));
    REQUIRE(mosaic::engine::ui::contains_rect(outer, outer)); // edges are inclusive
    REQUIRE_FALSE(mosaic::engine::ui::contains_rect(outer, Rect{0.9f, 0.0f, 0.2f, 0.2f}));
    REQUIRE_FALSE(mosaic::engine::ui::contains_rect(outer, Rect{-0.1f, 0.0f, 0.2f, 0.2f}));
}

TEST_CASE("a nested clip contained in its ancestor resolves bit-identically", "[ui][clip]")
{
    // The hierarchy-level statement of the same property: adding an outer clip that already
    // contains the inner one must not move the inner clip at all, or every clipped region
    // would shift by an ULP for each ancestor above it.
    World      world;
    const auto outer = world.create_entity();
    world.add_component<UiPanel>(outer, panel_at(0.0f, 0.0f, 1.0f, 1.0f));

    const auto inner = world.create_entity();
    world.add_component<UiPanel>(inner, panel_at(0.1f, 0.3f, 0.2f, 0.45f));
    world.add_component<UiParent>(inner, UiParent{outer});
    world.add_component<UiClipRect>(inner, UiClipRect{});

    const auto leaf = world.create_entity();
    world.add_component<UiPanel>(leaf, panel_at(0.0f, 0.0f, 1.0f, 1.0f));
    world.add_component<UiParent>(leaf, UiParent{inner});

    const auto without_outer_clip = resolve_clip(world, leaf);
    REQUIRE(without_outer_clip.has_value());

    // Now make the (containing) ancestor clip as well. The answer must be the same rect,
    // to the bit.
    world.add_component<UiClipRect>(outer, UiClipRect{});
    const auto with_outer_clip = resolve_clip(world, leaf);
    REQUIRE(with_outer_clip.has_value());
    REQUIRE(same_clip(without_outer_clip, with_outer_clip));
}
