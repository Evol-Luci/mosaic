#pragma once

#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ui
{
// Names this element's container: an entity carrying a UiPanel. The element's pos/size
// then resolve within the parent panel's resolved rect (recursively up the panel chain).
// A nil parent (or a parent without a UiPanel) resolves against the whole screen.
struct UiParent
{
    mosaic::foundation::EntityID parent;
};

template <class V>
void reflect(V& visitor, UiParent& p)
{
    visitor.member("parent", p.parent);
}
} // namespace mosaic::engine::ui
