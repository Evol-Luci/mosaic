#pragma once

#include <vector>

#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_hit.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Every UiButton flattened into hit candidates.
//
// SHARED ON PURPOSE, and this is the whole reason the function moved out of UiSystem. The
// press path, the hover path AND the order-conflict diagnostic must all reason about the
// SAME rects: a diagnostic that resolved buttons its own way would police rectangles nobody
// ever clicks, and would drift the first time either side changed. One definition means a
// conflict it reports is a conflict the hit test will actually honour.
//
// `UiButton::visible == false` IS FILTERED HERE, AND ONLY HERE. That sharing is what makes
// one line enough: the press path, the hover path and the order-conflict diagnostic all read
// this list, so an invisible button is absent from every one of them -- it emits no click, no
// hover, no drag, and raises no order finding, because it can misroute nothing. Filtering in
// UiSystem instead would have left the diagnostic policing rectangles nobody can click.
//
// ABSENT, not merely silent. The button is never a candidate, so it cannot win the topmost
// resolution and then swallow the press -- a click passes through to whatever is beneath it,
// and press_consumed() stays false.
//
// It reads UiButton::visible and NOTHING ELSE. A UiPanel{visible = false} on the same entity
// does not disable the button: co-location is a convention this repository follows, not an
// invariant the data enforces (see UiPanel::visible, property 4).
inline std::vector<HitCandidate> button_hit_candidates(mosaic::engine::World& world)
{
    std::vector<HitCandidate> out;
    world.for_each<UiButton>(
        [&](mosaic::foundation::EntityID e, UiButton& b)
        {
            if (!b.visible) { return; }
            out.push_back(HitCandidate{
                e, b.order,
                resolve_ui_rect(world, e, to_anchor(b.anchor), b.pos, b.size)});
        });
    return out;
}
} // namespace mosaic::engine::ui
