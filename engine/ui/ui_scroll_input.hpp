#pragma once

#include <algorithm>

#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/ui/ui_scroll.hpp"
#include "engine/world/world.hpp"

namespace mosaic::engine::ui
{

// One wheel notch, in container-heights.
//
// DERIVED, NOT TUNED. The demo region authors its labels at `height = 0.1`, and layout_text
// maps a label's height to `height * frame.h` of normalized Y per line -- so with the frame
// being the container, 0.1 container-heights is EXACTLY one line of that region's text. One
// notch, one line. That is checkable arithmetic rather than a feel, which matters because
// nobody has scrolled this yet and a number chosen by feel would be a number chosen by
// nobody.
//
// A per-region tunable is declined for now on UiFlow's rule -- one meaning everywhere, a
// differing default at most -- and because a second region wanting a different step is the
// trigger that would justify the field. Backlogged with that trigger.
inline constexpr float kScrollNotch = 0.1f;

// Apply this tick's wheel delta to every scroll region under the pointer.
//
// CALLED FROM UiSystem::update AND NOWHERE ELSE, which is both a tick-rate decision and a
// correctness one:
//   * UiSystem already holds the tick's PointerState. A second system reading the provider
//     would make it three reads per tick and, since RuntimeLoop::advance runs up to eight
//     fixed ticks per frame, up to 24 observations of one delta.
//   * It is a TICKED system, and that is what keeps a stopped editor from scrolling. The
//     editor runs zero ticks while stopped, so nothing calls the provider at all -- the
//     wheel is not suppressed by the provider, it is never asked for. Driving scroll from a
//     per-FRAME hook instead would scroll a stopped editor, and the `hovered` gate on the
//     editor's wheel would not stop it.
//
// `p.inside` is required, not just the rect test: x/y are clamped even when no pointer is
// present, so without it a wheel event delivered while the cursor sat on the desktop would
// scroll whichever region happens to contain the clamped edge position.
//
// EVERY region containing the pointer consumes the delta. With one authored region there is
// no ambiguity; nested regions and scroll chaining -- which region wins -- are explicit
// non-goals with their own backlog entry.
//
// Writes the CLAMPED value back, so the inspector shows the offset the renderer will
// actually use rather than a number it silently ignores. scroll_offset_of clamps on read as
// well, because a scene file can set `offset` without the wheel ever running.
inline void apply_wheel_scroll(mosaic::engine::World& world, const PointerState& p)
{
    if (!p.inside || p.wheel == 0.0f) { return; }
    world.for_each<UiScroll>([&](mosaic::foundation::EntityID e, UiScroll& s) {
        const UiPanel* panel = world.get_component<UiPanel>(e);
        if (panel == nullptr) { return; } // no rect to scroll within; same rule as UiStack
        const Rect rect = resolve_ui_rect(world, e, to_anchor(panel->anchor), panel->pos,
                                          panel->size);
        // Inclusive edges, matching topmost_hit's containment: a pointer exactly on the
        // region's edge is on the region.
        if (p.x < rect.x || p.x > rect.x + rect.w || p.y < rect.y || p.y > rect.y + rect.h)
        {
            return;
        }
        // NEGATED: PointerState::wheel is positive AWAY from the user, which means "look
        // further UP the document", which is a SMALLER offset.
        const float next = scroll_offset_of(s) - p.wheel * kScrollNotch;
        s.offset = std::clamp(next, 0.0f, scroll_max_offset(s));
    });
}

} // namespace mosaic::engine::ui
