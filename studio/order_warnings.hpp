#pragma once

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "engine/ui/ui_order_check.hpp"

namespace mosaic::studio
{
// Remembers which order disagreements have already been reported, so an every-frame check
// warns on the transition rather than on every frame. Pure data -- no ImGui, no World, no
// logger. Mirrors PlayState and SceneFileModel.
//
// WHY EVERY FRAME AT ALL, given `order` is not editable in the Inspector (ImGuiFieldVisitor
// has no field_int, so every order renders read-only): SetComponentCommand goes through
// reflection, so a Python script, a Studio plugin, the Script Console, an MCP
// `set_component` call or the Blender live-link can all write an order mid-session. A
// load-time-only check would miss every one of those.
class OrderWarningModel
{
public:
    // The subset of `current` worth telling a user about now: the findings not already
    // reported with these same two numbers.
    //
    // THE NUMBERS ARE PART OF THE IDENTITY, not just the entity. A finding whose orders
    // changed (5-vs-2 becoming 5-vs-9) is a DIFFERENT disagreement with a different fix, and
    // a model keyed on the entity alone would stay silent about it forever after the first
    // warning.
    //
    // THE VISIBLE CONSEQUENCE, REVIEWED AND KEPT: dragging an `order` in the Inspector passes
    // through every intermediate value, so one drag from 0 to 7 emits roughly seven warnings.
    // That looks like a de-duplication bug and is not one -- each line reports a genuinely
    // different disagreement. It became reachable only once field_int made `order` draggable,
    // was then exercised at the window on 2026-10-12, and the warning level was accepted as
    // it stands. Debouncing during an active drag was considered and deliberately not added.
    // Do not "fix" the burst without revisiting that decision.
    std::vector<mosaic::engine::ui::OrderDisagreement> emit(
        const std::vector<mosaic::engine::ui::OrderDisagreement>& current)
    {
        std::vector<mosaic::engine::ui::OrderDisagreement> fresh;
        Warned                                            still;
        for (const auto& f : current)
        {
            const Value value{f.button_order, f.panel_order};
            const auto  seen = warned_.find(f.e.value());
            if (seen == warned_.end() || seen->second != value)
            {
                fresh.push_back(f);
            }
            still.emplace(f.e.value(), value);
        }
        // REBUILT from `current` rather than inserted into, so a finding that disappears is
        // forgotten and warns again if it returns. A monotonically growing set would go
        // permanently silent about a disagreement the user fixed and then reintroduced.
        warned_ = std::move(still);
        return fresh;
    }

private:
    // Keyed on the raw EntityID value because EntityID has no operator< -- it offers ==, !=
    // and a std::hash specialization only. The value includes the generation, so a recycled
    // index cannot inherit a destroyed entity's warning history.
    using Value  = std::pair<int, int>;
    using Warned = std::map<std::uint64_t, Value>;

    Warned warned_;
};
} // namespace mosaic::studio
