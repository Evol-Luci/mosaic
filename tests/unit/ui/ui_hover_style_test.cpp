#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_hover_style.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_parent.hpp"
#include "engine/world/world.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::ui::active_hover_style;
using mosaic::engine::ui::resolve_appearance;
using mosaic::engine::ui::UiHovered;
using mosaic::engine::ui::UiHoverStyle;
using mosaic::engine::ui::UiPanel;
using mosaic::engine::ui::UiParent;
using mosaic::foundation::math::Vec3;

namespace
{
// A mid-grey half-transparent appearance, so a multiply is visible in every channel and a
// dropped channel shows up. Deliberately NOT 1.0 anywhere: multiplying by 1 hides bugs.
constexpr Vec3 kColor{0.4f, 0.5f, 0.6f};
constexpr float kAlpha = 0.5f;
} // namespace

TEST_CASE("a null style leaves the authored appearance untouched", "[ui][hover]")
{
    // THE migration guarantee, and the branch every existing scene takes: nothing in the
    // repository has a UiHoverStyle, so every element resolves through here and must emit
    // exactly what it emits today.
    const auto a = resolve_appearance(nullptr, kColor, kAlpha);
    REQUIRE(a.color.x == Approx(0.4f));
    REQUIRE(a.color.y == Approx(0.5f));
    REQUIRE(a.color.z == Approx(0.6f));
    REQUIRE(a.alpha == Approx(0.5f));
}

TEST_CASE("a style multiplies every channel and the alpha", "[ui][hover]")
{
    // Asymmetric multipliers per channel: a style that used one scalar for all three, or
    // transposed two, passes with equal values.
    UiHoverStyle s;
    s.tint = Vec3{2.0f, 1.5f, 0.5f};
    s.alpha_scale = 1.2f;
    const auto a = resolve_appearance(&s, kColor, kAlpha);
    REQUIRE(a.color.x == Approx(0.8f));  // 0.4 * 2.0
    REQUIRE(a.color.y == Approx(0.75f)); // 0.5 * 1.5
    REQUIRE(a.color.z == Approx(0.3f));  // 0.6 * 0.5
    REQUIRE(a.alpha == Approx(0.6f));    // 0.5 * 1.2
}

TEST_CASE("the resolved appearance clamps to one", "[ui][hover]")
{
    // UI colour is LDR. Brightening is the whole point of a hover style, so exceeding 1 is
    // the EXPECTED input here rather than an edge case.
    UiHoverStyle s;
    s.tint = Vec3{10.0f, 10.0f, 10.0f};
    s.alpha_scale = 10.0f;
    const auto a = resolve_appearance(&s, kColor, kAlpha);
    REQUIRE(a.color.x == Approx(1.0f));
    REQUIRE(a.color.y == Approx(1.0f));
    REQUIRE(a.color.z == Approx(1.0f));
    REQUIRE(a.alpha == Approx(1.0f));
}

TEST_CASE("a negative multiplier clamps to zero rather than passing through", "[ui][hover]")
{
    // Review focus 1. UiHoverStyle is authored, so a scene file can hold -1. A clamp
    // written as std::min(v, 1.0f) lets a negative through, and a negative colour channel
    // is not a visible mistake -- it is undefined-looking output the shader interprets.
    UiHoverStyle s;
    s.tint = Vec3{-1.0f, -5.0f, -0.001f};
    s.alpha_scale = -2.0f;
    const auto a = resolve_appearance(&s, kColor, kAlpha);
    REQUIRE(a.color.x == Approx(0.0f));
    REQUIRE(a.color.y == Approx(0.0f));
    REQUIRE(a.color.z == Approx(0.0f));
    REQUIRE(a.alpha == Approx(0.0f));
}

TEST_CASE("an alpha scale of zero hides the element", "[ui][hover]")
{
    // The override must be able to hide, not only brighten -- 0 is a legitimate authored
    // value and must not be confused with "no style".
    UiHoverStyle s;
    s.alpha_scale = 0.0f;
    const auto a = resolve_appearance(&s, kColor, kAlpha);
    REQUIRE(a.alpha == Approx(0.0f));
    REQUIRE(a.color.x == Approx(0.4f)); // tint defaults to 1,1,1 -- colour is untouched
}

TEST_CASE("a default-constructed style changes nothing", "[ui][hover]")
{
    // Why multipliers rather than absolute colours: adding the component without
    // configuring it must be an exact no-op, or authoring it is a trap.
    UiHoverStyle s;
    const auto a = resolve_appearance(&s, kColor, kAlpha);
    REQUIRE(a.color.x == Approx(0.4f));
    REQUIRE(a.color.y == Approx(0.5f));
    REQUIRE(a.color.z == Approx(0.6f));
    REQUIRE(a.alpha == Approx(0.5f));
}

TEST_CASE("a style present but not hovered yields no style", "[ui][hover]")
{
    // The natural wrong implementation returns the style whenever it finds one, which makes
    // every styled element PERMANENTLY highlighted -- and looks deliberate, so nobody
    // reports it as a bug.
    World world;
    const auto e = world.create_entity();
    world.add_component<UiPanel>(e, UiPanel{});
    world.add_component<UiHoverStyle>(e, UiHoverStyle{});
    REQUIRE(active_hover_style(world, e) == nullptr);
}

TEST_CASE("a hovered entity with its own style returns that style", "[ui][hover]")
{
    World world;
    const auto e = world.create_entity();
    UiHoverStyle s;
    s.alpha_scale = 0.25f; // a value no other case uses, so the RIGHT style is identified
    world.add_component<UiHoverStyle>(e, s);
    world.add_component<UiHovered>(e, UiHovered{});
    const UiHoverStyle* got = active_hover_style(world, e);
    REQUIRE(got != nullptr);
    REQUIRE(got->alpha_scale == Approx(0.25f));
}

TEST_CASE("a child inherits the style of its hovered ancestor", "[ui][hover]")
{
    // What makes the demo's label brighten with its panel, and the reason the walk goes UP:
    // the child carries no style and no tag of its own.
    World world;
    const auto parent = world.create_entity();
    UiHoverStyle s;
    s.alpha_scale = 0.75f;
    world.add_component<UiPanel>(parent, UiPanel{});
    world.add_component<UiHoverStyle>(parent, s);
    world.add_component<UiHovered>(parent, UiHovered{});

    const auto child = world.create_entity();
    world.add_component<UiParent>(child, UiParent{parent});

    const UiHoverStyle* got = active_hover_style(world, child);
    REQUIRE(got != nullptr);
    REQUIRE(got->alpha_scale == Approx(0.75f));
}

TEST_CASE("the nearest style wins over an ancestor's", "[ui][hover]")
{
    // So a child can opt out of inheritance by authoring its own style.
    World world;
    const auto parent = world.create_entity();
    UiHoverStyle ps;
    ps.alpha_scale = 0.75f;
    world.add_component<UiPanel>(parent, UiPanel{});
    world.add_component<UiHoverStyle>(parent, ps);
    world.add_component<UiHovered>(parent, UiHovered{});

    const auto child = world.create_entity();
    UiHoverStyle cs;
    cs.alpha_scale = 0.125f;
    world.add_component<UiParent>(child, UiParent{parent});
    world.add_component<UiHoverStyle>(child, cs);

    const UiHoverStyle* got = active_hover_style(world, child);
    REQUIRE(got != nullptr);
    REQUIRE(got->alpha_scale == Approx(0.125f)); // the child's, not 0.75
}

TEST_CASE("a hovered chain with no style anywhere yields no style", "[ui][hover]")
{
    // Hovering is not itself a visual state. Without this, "hovered" would have to mean
    // something on its own, and every button would need a style to look normal.
    World world;
    const auto parent = world.create_entity();
    world.add_component<UiPanel>(parent, UiPanel{});
    world.add_component<UiHovered>(parent, UiHovered{});
    const auto child = world.create_entity();
    world.add_component<UiParent>(child, UiParent{parent});
    REQUIRE(active_hover_style(world, child) == nullptr);
}

TEST_CASE("a hovered entity needs no UiButton for its style to apply", "[ui][hover]")
{
    // Review focus 5. The TAG is the signal, not the presence of a button -- and it must
    // stay that way, because every ancestor in an inheritance chain is a UiPanel rather
    // than a UiButton. A later "fix" that required a UiButton here would silently break
    // inheritance, which is the feature's whole point.
    World world;
    const auto e = world.create_entity();
    UiHoverStyle s;
    s.alpha_scale = 0.5f;
    world.add_component<UiHoverStyle>(e, s);
    world.add_component<UiHovered>(e, UiHovered{});
    REQUIRE(active_hover_style(world, e) != nullptr);
}

TEST_CASE("a nil UiParent stops the walk", "[ui][hover]")
{
    // Review focus 2. UiParent default-constructs with a nil EntityID, and an authored
    // UiParent with no parent set is the common accident. Looking components up on a nil id
    // must not happen at all.
    World world;
    const auto e = world.create_entity();
    world.add_component<UiParent>(e, UiParent{}); // nil parent
    REQUIRE(active_hover_style(world, e) == nullptr);
}

TEST_CASE("a UiParent naming a destroyed entity stops the walk", "[ui][hover]")
{
    // Review focus 3. Distinct from nil: the id is well-formed but no longer valid, which
    // is what a stale scene reference or a runtime destroy leaves behind.
    World world;
    const auto gone = world.create_entity();
    world.add_component<UiPanel>(gone, UiPanel{});
    world.add_component<UiHovered>(gone, UiHovered{});
    world.add_component<UiHoverStyle>(gone, UiHoverStyle{});

    const auto e = world.create_entity();
    world.add_component<UiParent>(e, UiParent{gone});

    world.destroy_entity(gone);
    world.tick(0.0f); // destroy_entity is deferred; tick services it

    REQUIRE(active_hover_style(world, e) == nullptr);
}

TEST_CASE("a UiParent cycle terminates instead of recursing forever", "[ui][hover]")
{
    // A cycle is authorable in a scene file, and the depth cap is the ONLY thing preventing
    // unbounded recursion here. Neither entity is hovered, so the correct answer is nullptr
    // -- reached by exhausting the cap rather than by finding an answer.
    World world;
    const auto a = world.create_entity();
    const auto b = world.create_entity();
    world.add_component<UiPanel>(a, UiPanel{});
    world.add_component<UiPanel>(b, UiPanel{});
    world.add_component<UiParent>(a, UiParent{b});
    world.add_component<UiParent>(b, UiParent{a});
    REQUIRE(active_hover_style(world, a) == nullptr);
}

TEST_CASE("a style above the hovered entity still governs it", "[ui][hover]")
{
    // The configuration every other inheritance case fails to distinguish: the tag and the
    // style on DIFFERENT entities, with the style ABOVE the tag.
    //
    // Only UiButton is hit-testable, so the tag always lands on the button. An author's
    // first instinct is to put the style on the containing panel -- and before this was
    // fixed, that silently did nothing at all: the walk returned the moment it saw the tag,
    // so a style one level further up was never looked for.
    //
    // The spec's rule is "the nearest UiHoverStyle found ON THE WAY UP", with hovered-ness
    // coming from a tag anywhere at or above. Both searches run over the whole chain; only
    // their answers are combined.
    World world;
    const auto panel = world.create_entity();
    UiHoverStyle s;
    s.alpha_scale = 0.3f;
    world.add_component<UiPanel>(panel, UiPanel{});
    world.add_component<UiHoverStyle>(panel, s); // style on the CONTAINER

    const auto button = world.create_entity();
    world.add_component<UiPanel>(button, UiPanel{});
    world.add_component<UiParent>(button, UiParent{panel});
    world.add_component<UiHovered>(button, UiHovered{}); // tag on the CHILD

    const UiHoverStyle* got = active_hover_style(world, button);
    REQUIRE(got != nullptr);
    REQUIRE(got->alpha_scale == Approx(0.3f));

    // And the container itself is NOT styled: its own chain reaches no tag, because the
    // walk only ever goes up. A hovered child does not highlight its parent.
    REQUIRE(active_hover_style(world, panel) == nullptr);
}

TEST_CASE("a style inside the cap with no tag anywhere yields no style", "[ui][hover]")
{
    // Found by a mutation probe that produced NO red: changing the cap-exhausted return
    // from nullptr to `nearest` left the whole suite green, because the only deep-chain case
    // put the style ABOVE the cap, where `nearest` is null either way.
    //
    // This is the input that distinguishes them: a 20-link chain whose style sits two links
    // up -- well inside the cap -- with no UiHovered anywhere. Correct code exhausts the cap
    // and returns nullptr. The mutant returns the style it happened to collect, which styles
    // an element while nothing is hovered at all: a permanently highlighted element, which
    // looks deliberate and so never gets reported.
    World world;
    std::vector<mosaic::foundation::EntityID> chain;
    for (int i = 0; i < 20; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<UiPanel>(e, UiPanel{});
        if (!chain.empty()) { world.add_component<UiParent>(e, UiParent{chain.back()}); }
        chain.push_back(e);
    }
    // chain.back() is the bottom; walking up, this is 2 links away and inside the cap.
    world.add_component<UiHoverStyle>(chain[17], UiHoverStyle{});
    REQUIRE(active_hover_style(world, chain.back()) == nullptr);
}

TEST_CASE("a style eleven links up is still found", "[ui][hover]")
{
    // Constrains the depth cap DOWNWARD, which nothing else did: a probe lowering
    // kMaxAncestors from 16 to 3 passed the entire suite. Every other case is either depth
    // <= 1 or deeper than the cap and expecting nullptr, so the constant's value was
    // unconstrained in the direction that matters.
    //
    // It matters because a shallow cap breaks inheritance in hierarchies people really
    // build -- panel > row > group > label is already depth 3 -- and the failure is silent:
    // the style simply stops applying past some depth, with nothing to indicate why.
    World world;
    std::vector<mosaic::foundation::EntityID> chain;
    for (int i = 0; i < 12; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<UiPanel>(e, UiPanel{});
        if (!chain.empty()) { world.add_component<UiParent>(e, UiParent{chain.back()}); }
        chain.push_back(e);
    }
    UiHoverStyle s;
    s.alpha_scale = 0.4f;
    world.add_component<UiHoverStyle>(chain.front(), s);
    world.add_component<UiHovered>(chain.front(), UiHovered{});

    // chain.back() is eleven links below the styled, hovered root.
    const UiHoverStyle* got = active_hover_style(world, chain.back());
    REQUIRE(got != nullptr);
    REQUIRE(got->alpha_scale == Approx(0.4f));
}

TEST_CASE("a chain deeper than the cap yields no style", "[ui][hover]")
{
    // 20 links with the hovered, styled entity at the TOP: beyond the cap of 16, so the
    // answer is nullptr. Pins that the cap is real rather than theoretical, and that
    // exceeding it fails closed (no style) rather than open.
    World world;
    std::vector<mosaic::foundation::EntityID> chain;
    for (int i = 0; i < 20; ++i)
    {
        const auto e = world.create_entity();
        world.add_component<UiPanel>(e, UiPanel{});
        if (!chain.empty()) { world.add_component<UiParent>(e, UiParent{chain.back()}); }
        chain.push_back(e);
    }
    world.add_component<UiHovered>(chain.front(), UiHovered{});
    world.add_component<UiHoverStyle>(chain.front(), UiHoverStyle{});
    REQUIRE(active_hover_style(world, chain.back()) == nullptr);
}
