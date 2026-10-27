#pragma once

#include <optional>

#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{

// THERE ARE TWO THINGS CALLED "CLIP" AND THIS IS THE AUTHORED ONE.
//
//   * `UiClipRect` (here) is a COMPONENT. It is authored on a container entity and says
//     "everything under me is clipped to my rect". It knows about the world and the
//     hierarchy, and nothing in it mentions a device.
//   * `RenderDevice::UiClip` (engine/render/render_device.hpp) is a device-side ALIAS for
//     `std::optional<Rect>`: the already-resolved sub-rect one draw call carries. It knows
//     nothing about components or parents.
//
// resolve_clip below is the only bridge between them, and the two names are deliberately
// different because render_system.hpp's HUD passes mention both within a few lines of each
// other. `UiClipRect` is the component's name because it states the semantics exactly --
// clip to *this entity's rect* -- rather than restating the word the device already took.
//
// A MARKER WITH NO FIELDS, like AudioListener. The rect is not authored here: it IS the
// container's resolved UiPanel rect, which is the point. Authoring a second rect beside the
// panel's would create two sources of truth for one rectangle and let a container's chrome
// and its clip drift apart -- and nothing would be able to say which one was meant.
//
// IT CLIPS THE SUBTREE, NOT THE ENTITY ITSELF. A panel carrying UiClipRect draws in full;
// its descendants are what get masked. Three consequences worth stating rather than
// discovering:
//   1. A container clips its children even when its own panel is `visible = false` -- the
//      layout-only container of U8b clips exactly as well as a visible one, because a clip
//      is geometry and `visible` is chrome.
//   2. A clip is unaffected by the clipped child's own anchor, pos or size. It is resolved
//      entirely from ancestors, so moving a child cannot move its clip -- only how much of
//      the child survives it.
//   3. UiClipRect on an entity with no UiPanel contributes NOTHING, rather than clipping
//      everything away. There is no rect to clip to, and treating "no rect" as the empty
//      rect would silently hide a whole subtree for a reason nobody authored. Same posture
//      as inherited_alpha's missing-UiPanel ancestor (ui_layout.hpp).
struct UiClipRect
{
};

template <class V>
void reflect(V& /*visitor*/, UiClipRect& /*c*/)
{
}

// This element's clip: the INTERSECTION of every clipping ancestor's rect, or nothing when
// no ancestor clips.
//
// INTERSECTION, NOT THE INNERMOST ONE, and this is the single correctness claim of the whole
// feature. The two answers differ only when an inner clip reaches OUTSIDE its ancestor's --
// which is exactly the authored mistake a clip is supposed to contain. Taking the innermost
// would let a nested region paint over the panel that is meant to bound it, and every
// single-level test would stay green.
//
// `std::nullopt` means "unclipped", NOT "the whole screen", even though {0,0,1,1} would
// behave the same today. It is the value RenderDevice::UiClip reserves for "record no
// scissor at all", so a clip-free HUD emits byte-for-byte the command stream it emitted
// before this feature existed. Collapsing the two would make every UI draw in every scene
// set a scissor.
//
// AN ITERATIVE WALK UP UiParent, not recursion through resolve_parent_frame, and the same
// kMaxDepth as resolve_parent_frame and inherited_alpha, for the same reason: a cycle in
// UiParent must terminate. It is a SEPARATE walk because resolve_parent_frame narrows a
// child's FRAME -- it relocates and resizes -- and a clip must change neither position nor
// size. There is no second slot_for_child here, by construction.
//
// WHAT IT COSTS A SCENE THAT CLIPS NOTHING: one get_component<UiClipRect> probe per
// ancestor, and NOTHING else. resolve_ui_rect is called only for an ancestor that actually
// carries the component, so the layout counters in ui_layout_stats.hpp read exactly what
// they read before this function existed in every scene with no clip -- which is every
// scene shipped today, including the demo scene. That is deliberate: a clip-free scene must
// not pay for the feature, and the existing exact-count assertions in
// ui_layout_cost_test.cpp are what hold it to that.
//
// WHAT IT COSTS A SCENE THAT DOES CLIP: resolving a clipping ancestor's rect walks ITS
// ancestors, so d nested clips cost O(d^2) resolves, bounded by kMaxDepth. Those resolves
// are counted by `resolves` like any other, which is correct -- they are genuine
// resolve_parent_frame entries -- and a dedicated `clip_resolves` counter is an explicit
// non-goal of U9 (see the decomposition's Non-goals).
inline std::optional<Rect> resolve_clip(mosaic::engine::World&       world,
                                        mosaic::foundation::EntityID e)
{
    constexpr int                kMaxDepth = 16;
    std::optional<Rect>          clip;
    mosaic::foundation::EntityID cur = e;
    for (int depth = 0; depth < kMaxDepth; ++depth)
    {
        const UiParent* up = world.get_component<UiParent>(cur);
        if (up == nullptr || !world.is_valid(up->parent)) { break; }
        if (world.get_component<UiClipRect>(up->parent) != nullptr)
        {
            // A clipping ancestor with no UiPanel has no rect, so it contributes nothing.
            // See consequence 3 on the component above.
            if (const UiPanel* pp = world.get_component<UiPanel>(up->parent))
            {
                const Rect ancestor = resolve_ui_rect(world, up->parent,
                                                      to_anchor(pp->anchor), pp->pos,
                                                      pp->size);
                // clip_rect(accumulated, ancestor), in that argument order on purpose:
                // the walk goes UPWARD, so `clip` already holds the inner clips and
                // `ancestor` is the outer one. The usual nested arrangement has the inner
                // clip inside the outer, and this order is the one whose containment fast
                // path then returns the inner clip EXACTLY rather than one ULP off it.
                // See clip_rect in ui_rect.hpp for why that exactness is a property and
                // not a micro-optimisation.
                clip = clip.has_value() ? clip_rect(*clip, ancestor) : ancestor;
            }
        }
        cur = up->parent;
    }
    return clip;
}

// Do two resolved clips name the same sub-rect? The grouping key the HUD passes batch by.
//
// EXACT FLOAT EQUALITY, deliberately, and it can only ever err in the safe direction. Two
// elements under the same clipping ancestor resolve through identical arithmetic and so
// compare equal bit for bit; two different clips cannot compare equal. The one float
// subtlety is NaN, which never equals itself -- so an element whose clip arithmetic produced
// a NaN edge never batches with anything, costing a draw call. An approximate comparison
// would buy nothing and could merge two genuinely different clips, which is the only way
// this function could change a PIXEL rather than a call count.
inline bool same_clip(const std::optional<Rect>& a, const std::optional<Rect>& b)
{
    if (a.has_value() != b.has_value()) { return false; }
    if (!a.has_value()) { return true; }
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

} // namespace mosaic::engine::ui
