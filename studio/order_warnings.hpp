#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/ui/ui_order_check.hpp"

namespace mosaic::studio
{
// Transition-only reporting, shared by every editor-time diagnostic: which findings are new
// since the last round. Pure data -- no ImGui, no World, no logger. Mirrors PlayState and
// SceneFileModel.
//
// Keyed on strings so ONE implementation serves findings of different shapes -- one entity
// and two numbers for the per-entity order lint, two entities and four for the pairwise
// conflict check. The subtle part is the rebuild below, and that is exactly the part worth
// not duplicating once per diagnostic.
class WarningLedger
{
public:
    // `key` is what the finding is ABOUT (an entity, a pair); `signature` is the numbers that
    // make this instance of it distinct.
    struct Entry
    {
        std::string key;
        std::string signature;
    };

    // The INDICES into `current` worth reporting now: a key absent last round, or present
    // with a different signature.
    //
    // Indices rather than keys, so a caller maps a result straight back to its own finding
    // (`for (auto i : ledger.emit(entries)) warn(findings[i]);`) instead of keeping a second
    // key-to-finding map of its own.
    std::vector<std::size_t> emit(const std::vector<Entry>& current)
    {
        std::vector<std::size_t>           fresh;
        std::map<std::string, std::string> still;
        for (std::size_t i = 0; i < current.size(); ++i)
        {
            const Entry& e    = current[i];
            const auto   seen = seen_.find(e.key);
            if (seen == seen_.end() || seen->second != e.signature)
            {
                fresh.push_back(i);
            }
            still.emplace(e.key, e.signature);
        }
        // REBUILT from `current` rather than inserted into, so a key that stops being
        // reported is forgotten and warns again if it returns. A monotonically growing map
        // would go permanently silent about a problem the user fixed and then reintroduced.
        seen_ = std::move(still);
        return fresh;
    }

private:
    std::map<std::string, std::string> seen_;
};

// The per-entity order lint's de-duplication: a thin typed wrapper over the ledger.
//
// WHY EVERY FRAME AT ALL: an `order` can change mid-session from the Inspector (a DragInt
// since #242 -- this comment previously argued from `order` being read-only there, which
// that slice made false), and also from a Python script, a Studio plugin, the Script
// Console, an MCP `set_component` call or the Blender live-link. Every one of those goes
// through SetComponentCommand and reflection, so a load-time-only check would miss them all.
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
        std::vector<WarningLedger::Entry> entries;
        entries.reserve(current.size());
        for (const auto& f : current)
        {
            // The entity identifies the finding. Its generation is in EntityID::value(), so a
            // recycled index cannot inherit a destroyed entity's warning history.
            entries.push_back(WarningLedger::Entry{
                std::to_string(f.e.value()),
                std::to_string(f.button_order) + ":" + std::to_string(f.panel_order)});
        }
        std::vector<mosaic::engine::ui::OrderDisagreement> fresh;
        for (const std::size_t i : ledger_.emit(entries))
        {
            fresh.push_back(current[i]);
        }
        return fresh;
    }

private:
    WarningLedger ledger_;
};
} // namespace mosaic::studio
