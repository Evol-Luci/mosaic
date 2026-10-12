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
inline std::vector<HitCandidate> button_hit_candidates(mosaic::engine::World& world)
{
    std::vector<HitCandidate> out;
    world.for_each<UiButton>(
        [&](mosaic::foundation::EntityID e, UiButton& b)
        {
            out.push_back(HitCandidate{
                e, b.order,
                resolve_ui_rect(world, e, to_anchor(b.anchor), b.pos, b.size)});
        });
    return out;
}
} // namespace mosaic::engine::ui
