#pragma once

#include "foundation/core/entity_id.hpp"

namespace mosaic::studio
{

// The editor's current entity selection, or none. Pure studio state; the Hierarchy
// panel writes it and the Inspector reads it. A default (nil) EntityID means nothing
// is selected.
class SelectionModel
{
public:
    mosaic::foundation::EntityID selected() const { return selected_; }
    bool has_selection() const { return static_cast<bool>(selected_); }
    bool is_selected(mosaic::foundation::EntityID id) const { return selected_ == id; }
    void select(mosaic::foundation::EntityID id) { selected_ = id; }
    void clear() { selected_ = mosaic::foundation::EntityID{}; }

private:
    mosaic::foundation::EntityID selected_{};
};

} // namespace mosaic::studio
