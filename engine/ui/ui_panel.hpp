#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD panel: an anchored, alpha-blended colored rectangle.
// anchor is an int cast to ui::Anchor (0 = TopLeft); the render path clamps
// out-of-range values to TopLeft. pos is a normalized offset from the anchor
// toward the interior; size is a fraction of the screen. order stacks panels
// (higher draws on top). A normal component: scripts can animate alpha/pos.
struct UiPanel
{
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.2f, 0.1f};
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    int   order  = 0;
    // DRAWN OR NOT. `false` means this component is not drawn; it is the authored way to
    // write a UiStack container that lays its children out while showing no chrome of its
    // own (see ui_stack.hpp).
    //
    // Four properties of it are load-bearing, and each one is a thing a future reader will
    // be tempted to "improve":
    //
    // 1. DRAWING ONLY. It must never make resolve_parent_frame treat this panel as ABSENT.
    //    That function returns the whole SCREEN for a parent with no UiPanel, so an
    //    invisible container treated as absent would resolve every child against the screen
    //    instead of against its slot -- the exact opposite of what the flag is for, and
    //    silently wrong rather than visibly wrong.
    // 2. NOT IN THE ALPHA PRODUCT. inherited_alpha multiplies ancestor alphas down so that
    //    fading a container fades its contents; `visible` deliberately does NOT join that
    //    product, because the whole point is a container that is itself undrawn while its
    //    children render normally. A layout-only container is therefore authored
    //    `alpha = 1.0, visible = false`: alpha 1 contributes nothing to the product, and
    //    `visible` hides only this one component.
    // 3. NO INHERITANCE. Unlike alpha, it does not walk down the parent chain. Hiding a
    //    container does not hide its children.
    // 4. PER-COMPONENT, NO CROSS-COMPONENT INFERENCE. Hiding this does NOT disable a
    //    UiButton on the same entity, and vice versa. Co-location is not always ownership:
    //    app/run/demo_scene.hpp piles the unrelated "MOSAIC" title and the checker UiImage
    //    onto the `camera` entity, so "one entity is one widget" is a convention this
    //    repository happens to follow, not an invariant the data enforces. Hiding a button
    //    and its visual means setting two fields, and that is the honest cost.
    //
    // LAST FIELD, like `order` before it, so no positional brace init in any scene-building
    // code can re-bind. Every such site in app/run/demo_scene.hpp was converted to
    // designated initialisers first anyway.
    bool  visible = true;
};

template <class V>
void reflect(V& visitor, UiPanel& p)
{
    visitor.member("anchor", p.anchor);
    visitor.member("pos", p.pos);
    visitor.member("size", p.size);
    visitor.member("color", p.color);
    visitor.member("alpha", p.alpha);
    visitor.member("order", p.order);
    visitor.member("visible", p.visible);
}
} // namespace mosaic::engine::ui
