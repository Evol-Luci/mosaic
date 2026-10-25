#pragma once

#include "foundation/math/vec2.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec2;
using mosaic::foundation::math::Vec3;

// A screen-space HUD sprite: `texture` stretched to an anchored rect, tinted and
// alpha-blended. anchor is an int cast to ui::Anchor (clamped to TopLeft out of range).
// pos is a normalized offset from the anchor; size is a fraction of the screen. tint
// multiplies the texture rgb; alpha multiplies the texture alpha. Script-mutable.
struct UiImage
{
    mosaic::foundation::serialization::AssetID texture;
    int   anchor = 0;
    Vec2  pos    = Vec2{0.0f, 0.0f};
    Vec2  size   = Vec2{0.1f, 0.1f};
    // Sub-rect in TEXTURE PIXELS; a zero width or height means the whole texture, which is
    // what a default-constructed UiImage carries -- so every existing scene is unchanged.
    //
    // Two Vec2s because that is exactly how this component already spells pos/size, and
    // because foundation/math has no Vec4. Pixels rather than normalized UVs because that
    // is how atlas art is authored and how any tool reports it; hand-divided UVs also
    // break silently when an atlas is repacked.
    Vec2  sub_rect_pos  = Vec2{0.0f, 0.0f};
    Vec2  sub_rect_size = Vec2{0.0f, 0.0f};
    // ui::ImageFitMode as an int, matching how `anchor` is an int cast to ui::Anchor.
    // 0 = Stretch (the pre-existing behaviour), 1 = Fit, 2 = Fill; out of range -> Stretch.
    int   fit_mode = 0;
    Vec3  tint   = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    // Z-order WITHIN THIS PASS: higher draws on top. Among equal orders the entity later in
    // iteration draws on top, via a stable sort -- the same rule UiPanel::order follows, and
    // the one ui_hit.hpp's rule 2 assumes.
    //
    // THE PASS BOUNDARY IS ABSOLUTE AND THIS ORDER CANNOT CROSS IT. The three HUD passes run
    // in a fixed sequence -- panels, then images over panels, then text over both -- so a
    // UiImage is above every UiPanel no matter what either `order` says, and a negative value
    // here will NOT push it behind one. It only ranks images against each other. A unified
    // z-order across all UI is recorded in the backlog as its own (larger) piece of work.
    int   order  = 0;
    // Drawn or not. See UiPanel::visible for the four properties this flag has to keep;
    // the short version is: drawing only, not in the alpha product, no inheritance, and no
    // inference from or to any other component on the same entity.
    bool  visible = true;
};

template <class V>
void reflect(V& visitor, UiImage& i)
{
    visitor.member("texture", i.texture);
    visitor.member("anchor", i.anchor);
    visitor.member("pos", i.pos);
    visitor.member("size", i.size);
    visitor.member("sub_rect_pos", i.sub_rect_pos);
    visitor.member("sub_rect_size", i.sub_rect_size);
    visitor.member("fit_mode", i.fit_mode);
    visitor.member("tint", i.tint);
    visitor.member("alpha", i.alpha);
    visitor.member("order", i.order);
    visitor.member("visible", i.visible);
}
} // namespace mosaic::engine::ui
