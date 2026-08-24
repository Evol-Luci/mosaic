#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine/assets/asset_registry.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::studio
{

struct AssetEntry
{
    mosaic::foundation::serialization::AssetID id;
    std::string                                type;
    std::string                                source; // may be empty
};

// The asset browser's list + selection. Rebuilt each frame from the registry via
// refresh(); the selection persists across refreshes while its id still exists.
class AssetBrowserModel
{
public:
    void refresh(const mosaic::engine::assets::AssetRegistry& registry)
    {
        entries_.clear();
        registry.for_each([&](const mosaic::engine::assets::AssetMetadata& m)
                          { entries_.push_back(AssetEntry{m.uuid, m.type, m.source}); });
        std::sort(entries_.begin(), entries_.end(), [](const AssetEntry& a, const AssetEntry& b)
                  { return a.id.to_string() < b.id.to_string(); });

        if (has_selection_)
        {
            bool still_present = false;
            for (const AssetEntry& e : entries_)
            {
                if (e.id == selected_)
                {
                    still_present = true;
                    break;
                }
            }
            if (!still_present)
            {
                has_selection_ = false;
                selected_ = {};
            }
        }
    }

    const std::vector<AssetEntry>& entries() const { return entries_; }

    void select(mosaic::foundation::serialization::AssetID id)
    {
        selected_ = id;
        has_selection_ = true;
    }

    bool has_selection() const { return has_selection_; }
    mosaic::foundation::serialization::AssetID selected() const { return selected_; }

private:
    std::vector<AssetEntry>                    entries_;
    mosaic::foundation::serialization::AssetID selected_{}; // nil when none
    bool                                       has_selection_ = false;
};

} // namespace mosaic::studio
