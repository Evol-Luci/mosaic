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
    constexpr int kMaxDepth = 16;
    const UiHoverStyle* nearest = nullptr;
    mosaic::foundation::EntityID at = e;
    for (int depth = 0; depth < kMaxDepth; ++depth)
    {
        if (!world.is_valid(at))
        {
            return nullptr;
        }
        // The nearest style wins, so only record the first one found going up.
        if (nearest == nullptr)
        {
            if (const UiHoverStyle* s = world.get_component<UiHoverStyle>(at))
            {
                nearest = s;
            }
        }
        // A tag anywhere at or above the element means hovered. Found one -> the style
        // recorded so far (possibly none) is the answer.
        if (world.get_component<UiHovered>(at) != nullptr)
        {
            return nearest;
        }
        const UiParent* up = world.get_component<UiParent>(at);
        if (up == nullptr)
        {
            return nullptr; // top of the chain, and nothing was hovered
        }
        at = up->parent; // may be nil or stale
        // The is_valid check at the top of the loop is defence in depth rather than the
        // thing that makes this safe: get_component already returns nullptr for a nil or
        // stale id, because the sparse set compares the FULL EntityID including its
        // generation. Verified by a mutation probe -- removing is_valid changes no
        // observable behaviour. It stays because it states the precondition locally and
        // costs one branch, but a reader should not believe it is load-bearing.
    }
    return nullptr; // cap exhausted: fail closed rather than guess
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
