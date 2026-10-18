#pragma once

#include "engine/ui/ui_layout.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// The frame a child positions within: its parent UiPanel's resolved rect (recursive),
// or the whole screen {0,0,1,1} if no UiParent / invalid parent / parent has no UiPanel.
inline Rect resolve_parent_frame(mosaic::engine::World& world,
                                 mosaic::foundation::EntityID e, int depth = 0)
{
    constexpr int kMaxDepth = 16;
    const Rect kScreen{0.0f, 0.0f, 1.0f, 1.0f};
    if (depth >= kMaxDepth) { return kScreen; }
    UiParent* up = world.get_component<UiParent>(e);
    if (up == nullptr || !world.is_valid(up->parent)) { return kScreen; }
    UiPanel* pp = world.get_component<UiPanel>(up->parent);
    if (pp == nullptr) { return kScreen; }
    const Rect parent_frame = resolve_parent_frame(world, up->parent, depth + 1);
    const Rect parent_rect =
        resolve_in(parent_frame, to_anchor(pp->anchor), pp->pos, pp->size);
    // LAYOUT NARROWS THE FRAME. If the parent carries a UiStack, this child's frame is its
    // slot rather than the parent's whole rect -- and everything downstream runs unchanged,
    // which is the point. resolve_in already treats `pos` as an offset within the frame and
    // `size` as a fraction of it, so a child under a stack keeps its anchor, offsets from
    // its slot, and sizes relative to it with no new rule anywhere.
    //
    // THE HOOK IS HERE AND NOT IN resolve_ui_rect, deliberately. Two call paths reach the
    // hierarchy: panels, images and button hit-tests go through resolve_ui_rect, but TEXT
    // calls this function directly (the HUD text pass in render_system.hpp) and hands the
    // frame to layout_text. Hooking the wrapper would have laid out panels and images and
    // silently left every label where it was, with no existing test failing.
    if (const std::optional<Rect> slot = slot_for_child(world, e, parent_rect))
    {
        return *slot;
    }
    return parent_rect;
}

// This element's screen rect, resolved within its parent frame.
inline Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                            Anchor anchor, Vec2 pos, Vec2 size)
{
    return resolve_in(resolve_parent_frame(world, e), anchor, pos, size);
}
} // namespace mosaic::engine::ui
