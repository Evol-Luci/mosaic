#pragma once

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
    return resolve_in(parent_frame, to_anchor(pp->anchor), pp->pos, pp->size);
}

// This element's screen rect, resolved within its parent frame.
inline Rect resolve_ui_rect(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                            Anchor anchor, Vec2 pos, Vec2 size)
{
    return resolve_in(resolve_parent_frame(world, e), anchor, pos, size);
}
} // namespace mosaic::engine::ui
