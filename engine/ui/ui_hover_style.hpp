#pragma once

#include <algorithm>

#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec3;

// AUTHORED hover appearance: multiplies the element's authored colour and alpha while it is
// hovered. Reflected and saved.
//
// Multipliers rather than absolute values, so that a default-constructed style is an exact
// no-op -- adding the component without configuring it must change nothing -- and so one
// style composes across differently-coloured elements. An absolute override would need an
// extra "is set" flag to be safely default-constructable; recorded as a non-goal.
struct UiHoverStyle
{
    Vec3  tint        = Vec3{1.0f, 1.0f, 1.0f};
    float alpha_scale = 1.0f;
};

template <class V>
void reflect(V& visitor, UiHoverStyle& s)
{
    visitor.member("tint", s.tint);
    visitor.member("alpha_scale", s.alpha_scale);
}

// RUNTIME marker: UiSystem adds this to the hovered button and removes it on exit.
//
// DELIBERATELY NOT REGISTERED in engine/reflection/builtin_components.hpp, and that absence
// is load-bearing rather than an omission. ComponentReflection::save iterates every
// REGISTERED type, so registering this would serialize a phantom hover into any scene saved
// while the cursor rested on a button -- and loading that scene would start up already
// hovered, with no pointer anywhere near it. The cost is that it does not appear in the
// Inspector, which works off reflection. There is a test asserting it stays unregistered.
struct UiHovered
{
};

// The effective appearance an element should be drawn with.
struct UiAppearance
{
    Vec3  color;
    float alpha = 1.0f;
};

// The style governing `e`, or nullptr meaning "draw the authored values".
//
// ONE upward walk answers both questions -- is this element hovered, and which style governs
// it -- because they are found along the same chain. Hovered-ness comes from a UiHovered
// anywhere at or above the element; the style is the NEAREST UiHoverStyle on the way up, so
// a child can override its ancestor's by authoring its own.
//
// Walks UP because UiParent points up: nothing stores children, so resolving "the hovered
// button's descendants" would mean scanning every entity, while "this element's hovered
// ancestor" is a short walk in the direction the data already runs. It is also what makes a
// style authored on a panel apply to the label inside it.
//
// Returns nullptr when the chain is hovered but carries no style at all: hovering is not
// itself a visual state, so a button with no style looks normal.
//
// Depth-capped at 16, matching resolve_parent_frame in ui_hierarchy.hpp. The cap is the only
// thing preventing unbounded recursion on a UiParent cycle, which a scene file can author.
inline const UiHoverStyle* active_hover_style(mosaic::engine::World& world,
                                              mosaic::foundation::EntityID e)
{
    // 16 ANCESTORS, which is what resolve_parent_frame in ui_hierarchy.hpp contributes to a
    // rect: it reads e's UiParent, recurses, and its `depth >= 16` guard first fires on the
    // sixteenth ancestor. So this inspects e plus 16 ancestors -- 17 entities. An earlier
    // version looped 16 times total and therefore reached one level LESS far than the rect
    // resolution, which would have left an element exactly 16 links down positioned inside
    // its ancestor's frame while rendering unstyled.
    constexpr int kMaxAncestors = 16;

    // BOTH searches run the whole chain, and only their answers are combined at the end.
    //
    // An earlier version returned the moment it found the tag, which silently truncated the
    // STYLE search there -- so a style on an entity ABOVE the hovered one was never looked
    // for. Since only UiButton is hit-testable the tag always lands on the button, and an
    // author's first instinct is to style the containing panel, so the most natural thing to
    // author did nothing at all. The spec's rule is "the nearest UiHoverStyle found on the
    // way up", with hovered-ness from a tag anywhere at or above; truncating one search at
    // the other's answer is not that rule.
    const UiHoverStyle*          nearest = nullptr;
    bool                         hovered = false;
    mosaic::foundation::EntityID at      = e;

    for (int step = 0; step <= kMaxAncestors; ++step)
    {
        // Defence in depth, not the thing that makes this safe: World::get_component defers
        // to ComponentRegistry::get, which begins with its OWN is_valid check and returns
        // nullptr for a nil or stale id. (Below that, SparseSet::position_of also compares
        // the full 64-bit EntityID, and generations start at 1, so the nil handle can never
        // match a live entity even in slot 0.) Verified by a mutation probe: removing this
        // changes no observable behaviour. It stays because it states the precondition
        // locally and costs one branch -- a reader should not believe it is load-bearing.
        if (!world.is_valid(at))
        {
            break;
        }
        // The nearest style wins, so only the first one found going up is recorded.
        if (nearest == nullptr)
        {
            if (const UiHoverStyle* s = world.get_component<UiHoverStyle>(at))
            {
                nearest = s;
            }
        }
        // A tag anywhere at or above the element means hovered. Noted, not returned on:
        // the style search must keep climbing past it.
        if (world.get_component<UiHovered>(at) != nullptr)
        {
            hovered = true;
        }
        const UiParent* up = world.get_component<UiParent>(at);
        if (up == nullptr)
        {
            break; // top of the chain
        }
        at = up->parent; // may be nil or stale; the check at the top of the loop handles it
    }

    // Hovering is not itself a visual state: a hovered chain with no style anywhere returns
    // nullptr, so a button without a style looks normal.
    return hovered ? nearest : nullptr;
}

// Apply `style` to an authored colour and alpha. nullptr -> unchanged.
//
// Channels are multiplied EXPLICITLY because foundation/math/vec3.hpp defines only
// operator*(Vec3, float) -- there is no component-wise Vec3 multiply, so `color * tint`
// does not compile.
//
// Clamped to [0, 1] on BOTH ends. The upper bound matters because brightening is the point,
// so exceeding 1 is the expected input rather than an edge case; the lower bound matters
// because the style is authored data and a scene file can hold a negative, which would
// otherwise reach the shader as a negative colour channel.
inline UiAppearance resolve_appearance(const UiHoverStyle* style, Vec3 color, float alpha)
{
    if (style == nullptr)
    {
        return UiAppearance{color, alpha};
    }
    const auto clamp01 = [](float v) { return std::clamp(v, 0.0f, 1.0f); };
    return UiAppearance{Vec3{clamp01(color.x * style->tint.x),
                             clamp01(color.y * style->tint.y),
                             clamp01(color.z * style->tint.z)},
                        clamp01(alpha * style->alpha_scale)};
}
} // namespace mosaic::engine::ui
