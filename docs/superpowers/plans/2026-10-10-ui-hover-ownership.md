# UI Hover Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the engine render hover feedback for `UiButton`s, using the ownership rule that a button owns the UI components on its own entity or its nearest `UiParent` ancestor.

**Architecture:** Two components with deliberately opposite registration — `UiHoverStyle` (authored, reflected) and `UiHovered` (runtime, **unregistered** so it cannot be serialized into a scene). `UiSystem` tags exactly one entity on `hover_enter`. Each of the three UI render passes resolves the effective appearance before emitting, via one upward `UiParent` walk plus one pure multiply-and-clamp. Nothing mutates the authored components.

**Tech Stack:** C++20 header-only `engine/ui` tier; Catch2 + ctest; `foundation::math::Vec3`; `engine/reflection` for component registration; `engine/scene` for the save path.

**Spec:** `docs/superpowers/specs/2026-10-10-ui-hover-ownership-design.md`

## Global Constraints

- Tier rules are enforced by `tools/ci/check_dependency_rules.py`. `engine/ui` may depend on `foundation/*` and other `engine/*`, never on `studio/`, `app/` or `modules/`.
- Test names are checked by `tools/ci/check_test_case_names.py`: no name may begin with `--`, and none may contain an unmatched `[`.
- **`Vec3` has NO component-wise multiply.** `foundation/math/vec3.hpp` defines only `operator*(Vec3, float)` and `operator*(float, Vec3)`. Writing `color * style->tint` does not compile. Multiply the three channels explicitly.
- **`UiHovered` must NOT be passed to `types.register_component`.** `ComponentReflection::save` iterates every registered type (`engine/reflection/component_reflection.hpp:105-111`), so registering it would serialize a phantom hover into any scene saved while the cursor rests on a button.
- Components are registered as `types.register_component<T>(registry, "T")` in `engine/reflection/builtin_components.hpp`.
- `UiParent` is `{mosaic::foundation::EntityID parent;}` and points **upward**. Nothing stores children.
- The depth cap for `UiParent` traversal is **16**, matching `resolve_parent_frame` in `engine/ui/ui_hierarchy.hpp`.
- Run the suite with `ctest --test-dir build/debug`. The pre-commit hook regenerates `docs/reference/components.md` and **aborts**; `git add` what it rewrote and re-run the **full** commit — never `git commit --amend`, and never with `--no-verify`.

## Review Focus

Five input classes the spec implies but whose tests do not fall out of it. Each has a test in the task that owns the code.

1. **A negative `tint` or `alpha_scale`.** `UiHoverStyle` is authored data, so a scene file can hold `-1`. The spec says "clamps to `[0,1]`" but a clamp written as `min(v, 1.0f)` passes a negative straight through, yielding a negative colour channel the shader then interprets. Task 1.
2. **A nil `UiParent::parent`.** `UiParent` default-constructs with a nil `EntityID`, and an authored `UiParent` with no parent set is the common accident. The walk must stop rather than treat nil as a valid entity to look components up on. Task 1.
3. **A `UiParent` pointing at a destroyed entity.** Stale ids survive in scene data and across `destroy_entity`. Distinct from nil: the id is well-formed but no longer valid, and `get_component` on it must not be reached. Task 1.
4. **Two styled sibling buttons, one hovered.** The natural wrong implementation — treat "a style exists" or "anything is hovered" as the condition — highlights both. Nothing in the spec's own test list has two independent styled elements. Task 4.
5. **A `UiHovered` on an entity with no `UiButton`.** The tag is the signal, so such an entity *is* hovered for styling purposes. That is defensible but unstated; pinning it prevents a later "fix" that couples styling back to `UiButton` and breaks ancestor inheritance (ancestors are panels, not buttons). Task 1.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/ui/ui_hover_style.hpp` (new) | `UiHoverStyle`, `UiHovered`, `active_hover_style` (the walk), `resolve_appearance` (the arithmetic) |
| `tests/unit/ui/ui_hover_style_test.cpp` (new) | Both functions, headless |
| `engine/reflection/builtin_components.hpp` | registers `UiHoverStyle` only |
| `tests/unit/ui/ui_hover_style_reflection_test.cpp` (new) | round-trip + registered-builtin + the **unregistered** assertion |
| `engine/ui/ui_system.hpp` | adds/removes `UiHovered` on the hover transition |
| `engine/render/render_system.hpp` | the three UI passes resolve appearance before emitting |
| `app/run/demo_scene.hpp` | hover script replaced by a `UiHoverStyle` |
| `tests/CMakeLists.txt` | registers the two new test files |

---

### Task 1: the components, the walk, and the arithmetic

**Files:**
- Create: `engine/ui/ui_hover_style.hpp`
- Create: `tests/unit/ui/ui_hover_style_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::ui::UiParent` from `engine/ui/ui_parent.hpp`; `mosaic::foundation::math::Vec3`; `mosaic::engine::World`.
- Produces: `mosaic::engine::ui::UiHoverStyle{Vec3 tint; float alpha_scale;}`, `mosaic::engine::ui::UiHovered{}`, `mosaic::engine::ui::UiAppearance{Vec3 color; float alpha;}`,
  `active_hover_style(World&, EntityID) -> const UiHoverStyle*`, and
  `resolve_appearance(const UiHoverStyle*, Vec3 color, float alpha) -> UiAppearance`.
  Tasks 2–5 use these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/ui/ui_hover_style_test.cpp`.

```cpp
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
```

Add `#include <vector>` at the top of that file for the last case, and register it in `tests/CMakeLists.txt` beside the other `unit/ui/` entries (find them with `grep -n "unit/ui/" tests/CMakeLists.txt`).

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug 2>&1 | grep -m5 "error:"
```
Expected: FAIL at compile time — `engine/ui/ui_hover_style.hpp: No such file or directory`.

- [ ] **Step 3: Implement `engine/ui/ui_hover_style.hpp`**

```cpp
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
        at = up->parent; // may be nil or stale; the is_valid check above catches both
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
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][hover]"
```
Expected: PASS, 16 cases.

- [ ] **Step 5: Confirm the gates**

```bash
python3 tools/ci/check_dependency_rules.py && python3 tools/ci/check_test_case_names.py
```
Expected: both exit 0 with no output.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_hover_style.hpp tests/unit/ui/ui_hover_style_test.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
feat(ui): UiHoverStyle, UiHovered, and the ownership walk

Two components with deliberately opposite registration, plus the two functions
that make hover feedback resolvable without mutating anything.

active_hover_style walks UP the UiParent chain because that is the direction the
data runs: nothing stores children, so resolving a hovered button's descendants
would mean scanning every entity. One walk answers both questions -- is this
element hovered, and which style governs it -- and it is what makes a style
authored on a panel apply to the label inside it.

Returns nullptr when the chain is hovered but carries no style, because hovering
is not itself a visual state: a button with no style must look normal.

resolve_appearance multiplies the three channels EXPLICITLY, since
foundation/math has no component-wise Vec3 multiply, and clamps to [0,1] on both
ends. The lower bound is not defensive padding: the style is authored data, so a
scene file can hold a negative, and a clamp written as min(v, 1) passes that
straight through to the shader.
EOF
```

---

### Task 2: register `UiHoverStyle`, and assert `UiHovered` stays unregistered

**Files:**
- Modify: `engine/reflection/builtin_components.hpp`
- Create: `tests/unit/ui/ui_hover_style_reflection_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `UiHoverStyle`, `UiHovered` (Task 1).
- Produces: `"UiHoverStyle"` as a registered component name. Nothing later depends on this task, but the demo in Task 5 authors the component, which requires it to round-trip.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/ui/ui_hover_style.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"

using Catch::Approx;
using mosaic::engine::ui::UiHoverStyle;
using mosaic::foundation::math::Vec3;

TEST_CASE("UiHoverStyle round-trips through reflection", "[ui][reflection]")
{
    UiHoverStyle s;
    // Non-default on purpose: a field left at its default round-trips whether or not it is
    // serialised at all. Asymmetric per channel so a transposed axis fails.
    s.tint = Vec3{1.5f, 0.5f, 2.0f};
    s.alpha_scale = 0.25f;

    const auto value = mosaic::foundation::serialization::to_value(s);
    UiHoverStyle back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.tint.x == Approx(1.5f));
    REQUIRE(back.tint.y == Approx(0.5f));
    REQUIRE(back.tint.z == Approx(2.0f));
    REQUIRE(back.alpha_scale == Approx(0.25f));
}

TEST_CASE("UiHoverStyle is a registered built-in component", "[ui][reflection]")
{
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE(types.has("UiHoverStyle"));
}

TEST_CASE("UiHovered is deliberately NOT a registered component", "[ui][reflection]")
{
    // The absence is load-bearing, so it is asserted rather than left to be "fixed" by
    // someone adding the missing-looking registration line.
    //
    // ComponentReflection::save iterates every REGISTERED type, so registering UiHovered
    // would write a phantom hover into any scene saved while the cursor rested on a button.
    // Loading that scene would then start up already hovered, with no pointer near it -- and
    // the highlight would never clear, because no hover_exit is coming.
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    REQUIRE_FALSE(types.has("UiHovered"));
}
```

Register the file in `tests/CMakeLists.txt` beside the other `unit/ui/` entries.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "UiHoverStyle is a registered built-in component"
```
Expected: FAIL — `types.has("UiHoverStyle")` is false. The round-trip case and the unregistered case both pass already; only the registration case is red. (If the build fails instead, Task 1's header is missing an include this file needs.)

- [ ] **Step 3: Implement**

In `engine/reflection/builtin_components.hpp`, add the include beside the other `engine/ui/` ones and the registration beside the other UI components:

```cpp
#include "engine/ui/ui_hover_style.hpp"
```
```cpp
    types.register_component<mosaic::engine::ui::UiHoverStyle>(registry, "UiHoverStyle");
```

Do **not** add a line for `UiHovered`.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][reflection]"
```
Expected: PASS.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: all passing. Registering a component regenerates `docs/reference/components.md`, which the pre-commit hook rewrites — `git add` it when the hook asks.

- [ ] **Step 6: Commit**

```bash
git add engine/reflection/builtin_components.hpp tests/unit/ui/ui_hover_style_reflection_test.cpp tests/CMakeLists.txt
git commit -F - <<'EOF'
feat(ui): register UiHoverStyle, and pin that UiHovered is not registered

UiHoverStyle is authored data and registers like every other UI component.
UiHovered is runtime state and must not, because ComponentReflection::save
iterates every registered type -- so registering it would write a phantom hover
into any scene saved while the cursor rested on a button, and loading that scene
would start up already hovered with no pointer near it and no exit event coming.

That absence is asserted by a test, so it reads as a decision rather than as a
missing line someone should helpfully add.
EOF
```

---

### Task 3: `UiSystem` tags the hovered entity

**Files:**
- Modify: `engine/ui/ui_system.hpp`
- Test: `tests/unit/ui/ui_system_test.cpp` (exists)

**Interfaces:**
- Consumes: `UiHovered` (Task 1).
- Produces: the invariant that exactly the hovered button carries `UiHovered`. Task 4's render passes rely on it, and Task 5's demo is visible only because of it.

- [ ] **Step 1: Write the failing tests**

`tests/unit/ui/ui_system_test.cpp` has a `Harness` (read it: `sed -n '20,60p' tests/unit/ui/ui_system_test.cpp`) with a `World`, a mutable `ptr`, a synchronous `ScriptEvent` capture, a `button` member holding the entity, and an `add_overlapping(order, event)` helper. Append:

```cpp
TEST_CASE("the hovered button carries a UiHovered tag", "[ui][input]")
{
    Harness h;
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.ptr = {0.05f, 0.05f, false}; h.tick(); // hover it
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.ptr = {0.9f, 0.9f, false};   h.tick(); // leave
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("only the topmost of two overlapping buttons is tagged", "[ui][input]")
{
    // The tag must follow the SAME resolution the click does, or an element would highlight
    // while a different one answered the click -- the UI lying rather than a bug.
    Harness h;
    const auto top = h.add_overlapping(5, "on_top");

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(top));
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("moving between buttons moves the tag", "[ui][input]")
{
    // Two non-overlapping buttons, so one pointer move crosses from one into the other. The
    // tag must come OFF the first in the same update it goes onto the second; a hover_exit
    // that forgets the removal leaves two elements highlighted at once.
    Harness h;
    const auto e = h.world.create_entity();
    UiButton b;
    b.anchor = 0; b.pos = Vec2{0.5f, 0.5f}; b.size = Vec2{0.2f, 0.2f};
    b.event = "second";
    h.world.add_component<UiButton>(e, b);

    h.ptr = {0.05f, 0.05f, false}; h.tick();
    h.ptr = {0.55f, 0.55f, false}; h.tick();

    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(e));
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("a button that loses its UiButton while hovered leaves no stale tag",
          "[ui][input]")
{
    // Extends the U6 edge case. The hover_exit EVENT is deliberately silent there, because
    // the channel name lived on the component that is gone -- but the TAG must still come
    // off, or the element stays highlighted forever with nothing able to clear it.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    h.world.remove_component<UiButton>(h.button);
    h.tick();
    REQUIRE_FALSE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));
}

TEST_CASE("a saved scene contains no UiHovered even while hovering", "[ui][input]")
{
    // The persistence trap, asserted end to end rather than trusted to the missing
    // registration line: serialize a world WHILE an element is hovered and confirm the
    // marker does not appear in the output.
    Harness h;
    h.ptr = {0.05f, 0.05f, false}; h.tick();
    REQUIRE(h.world.has_component<mosaic::engine::ui::UiHovered>(h.button));

    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);
    const auto value = mosaic::engine::scene::to_value(h.world, types);

    // Cheapest total check: the serialized form must not mention the type name anywhere.
    const std::string text = mosaic::foundation::serialization::to_json(value);
    REQUIRE(text.find("UiHovered") == std::string::npos);
}
```

That last case needs these includes at the top of the file:
```cpp
#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/ui/ui_hover_style.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/json_backend.hpp"
```
Both entry points are verified: `mosaic::engine::scene::to_value(World&, const ComponentReflection&)`
(`engine/scene/scene.hpp:23`) and `mosaic::foundation::serialization::to_json(const Value&)`
(`foundation/serialization/json_backend.hpp:17`).

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "the hovered button carries a UiHovered tag"
```
Expected: FAIL — the second `REQUIRE` reports false, because nothing adds the tag yet.

- [ ] **Step 3: Implement**

In `engine/ui/ui_system.hpp`, add the include beside the other `engine/ui/` ones:

```cpp
#include "engine/ui/ui_hover_style.hpp"
```

Then, in `update`'s hover-transition block, add the tag alongside each event. The block currently emits `hover_exit` for the old entity and `hover_enter` for the new one; it becomes:

```cpp
        if (top != hovered_)
        {
            if (hovered_ != mosaic::foundation::EntityID{})
            {
                // Return value deliberately ignored: a false means the hovered entity lost
                // its UiButton, and clearing hovered_ below is the whole response.
                emit_button_action(world, hovered_, "hover_exit");
                // The TAG comes off even when the event could not be emitted. The event
                // needs the component's channel name; the tag does not, and an element left
                // tagged stays highlighted forever with nothing able to clear it.
                world.remove_component<UiHovered>(hovered_);
            }
            if (top != mosaic::foundation::EntityID{})
            {
                emit_button_action(world, top, "hover_enter");
                world.add_component<UiHovered>(top, UiHovered{});
            }
            hovered_ = top;
        }
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[ui][input]"
```
Expected: PASS, every case including the pre-existing click and hover ones.

- [ ] **Step 5: Run the whole suite**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
```
Expected: all passing. `UiSystem` now writes components during `update`, so watch the studio and app pointer tests specifically — they drive `UiSystem` through real providers.

- [ ] **Step 6: Commit**

```bash
git add engine/ui/ui_system.hpp tests/unit/ui/ui_system_test.cpp
git commit -F - <<'EOF'
feat(ui): UiSystem tags the hovered entity

hover_enter adds UiHovered, hover_exit removes it. UiSystem becomes a component
writer for the first time, which is why the tag is added and removed in the same
block that emits the events -- the two cannot drift apart if they are one
decision.

The tag comes off even when the exit EVENT could not be emitted. That case is
real: a hovered entity that loses its UiButton has no channel name left to emit
on, which U6 deliberately made silent -- but an element left tagged stays
highlighted forever, with no exit coming to clear it.

Also asserts the persistence trap end to end: a world serialized WHILE hovering
contains no UiHovered anywhere.
EOF
```

---

### Task 4: the three render passes resolve the appearance

**Files:**
- Modify: `engine/render/render_system.hpp` (the panel, image and text UI passes)
- Test: `tests/unit/render/render_system_test.cpp` (exists)

**Interfaces:**
- Consumes: `active_hover_style`, `resolve_appearance`, `UiHoverStyle`, `UiHovered` (Task 1); the tagging invariant (Task 3).
- Produces: nothing. Terminal task for the render path.

- [ ] **Step 1: Write the failing tests**

`tests/unit/render/render_system_test.cpp` has a `FakeRenderDevice` capturing `ui_draws`, `ui_image_draws` and `ui_text_draws`, plus `register_white_texture(registry, w = 1, h = 1)` and `register_font`. Read the UI-pass cases first (`grep -n "UiPanel\|ui_draws" tests/unit/render/render_system_test.cpp | head`). Append:

```cpp
TEST_CASE("a panel with no hover style emits its authored colour", "[render]")
{
    // The migration guarantee at the seam. Nothing in the repository has a UiHoverStyle, so
    // every existing HUD takes this path and must be byte-identical.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.color = Vec3{0.4f, 0.5f, 0.6f};
    p.alpha = 0.5f;
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    // ui_draws is a vector OF vectors of UiQuad -- not draw-call structs with a `.quads`
    // member, unlike ui_image_draws and ui_text_draws. Existing cases use .front().
    const auto& q = device.ui_draws.front().at(0);
    REQUIRE(q.color.x == Approx(0.4f));
    REQUIRE(q.alpha == Approx(0.5f));
}

TEST_CASE("a hovered panel emits the multiplied colour and alpha", "[render]")
{
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.color = Vec3{0.4f, 0.5f, 0.6f};
    p.alpha = 0.5f;
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.tint = Vec3{2.0f, 1.0f, 0.5f};
    s.alpha_scale = 1.2f;
    world.add_component<mosaic::engine::ui::UiHoverStyle>(e, s);
    world.add_component<mosaic::engine::ui::UiHovered>(e, mosaic::engine::ui::UiHovered{});

    rs.render(world, device, registry);

    const auto& q = device.ui_draws.front().at(0);
    REQUIRE(q.color.x == Approx(0.8f)); // 0.4 * 2.0
    REQUIRE(q.color.y == Approx(0.5f)); // 0.5 * 1.0
    REQUIRE(q.color.z == Approx(0.3f)); // 0.6 * 0.5
    REQUIRE(q.alpha == Approx(0.6f));   // 0.5 * 1.2
}

TEST_CASE("only the hovered one of two styled panels is highlighted", "[render]")
{
    // Review focus 4. The natural wrong implementations -- "a style exists" or "anything is
    // hovered" -- highlight both, and with one element on screen that looks correct.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 0.5f;

    const auto hovered = world.create_entity();
    mosaic::engine::ui::UiPanel a;
    a.alpha = 1.0f; a.order = 0;
    world.add_component<mosaic::engine::ui::UiPanel>(hovered, a);
    world.add_component<mosaic::engine::ui::UiHoverStyle>(hovered, s);
    world.add_component<mosaic::engine::ui::UiHovered>(hovered,
                                                       mosaic::engine::ui::UiHovered{});

    const auto plain = world.create_entity();
    mosaic::engine::ui::UiPanel b;
    b.alpha = 1.0f; b.order = 1; // drawn after, so it is the LAST quad
    world.add_component<mosaic::engine::ui::UiPanel>(plain, b);
    world.add_component<mosaic::engine::ui::UiHoverStyle>(plain, s);

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.size() == 1);
    const auto& quads = device.ui_draws.front();
    REQUIRE(quads.size() == 2);
    REQUIRE(quads.front().alpha == Approx(0.5f)); // hovered -> styled
    REQUIRE(quads.back().alpha == Approx(1.0f));  // not hovered -> authored
}

TEST_CASE("a hover style never reveals a hidden panel", "[render]")
{
    // alpha 0 means hidden, and the early-out stays on the AUTHORED alpha so a style cannot
    // override that. A resolved-alpha early-out would make an invisible element appear on
    // hover, which reads as a rendering glitch rather than as a style.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const auto e = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.alpha = 0.0f; // hidden
    world.add_component<mosaic::engine::ui::UiPanel>(e, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 100.0f; // would clamp to 1 if it were applied
    world.add_component<mosaic::engine::ui::UiHoverStyle>(e, s);
    world.add_component<mosaic::engine::ui::UiHovered>(e, mosaic::engine::ui::UiHovered{});

    rs.render(world, device, registry);

    REQUIRE(device.ui_draws.empty());
}

TEST_CASE("a child UiText inherits its hovered ancestor's style", "[render]")
{
    // The inheritance no pure test can see: it is the PASS's lookup that walks the chain.
    // This is the demo's exact shape -- a hovered panel with a label parented to it.
    World world;
    FakeRenderDevice device;
    AssetRegistry registry;
    RenderSystem rs;
    rs.set_aspect(2.0f);
    add_camera(world, Vec3{0, 0, 5});

    const AssetID font = register_font(registry);

    const auto panel = world.create_entity();
    mosaic::engine::ui::UiPanel p;
    p.alpha = 1.0f;
    world.add_component<mosaic::engine::ui::UiPanel>(panel, p);
    mosaic::engine::ui::UiHoverStyle s;
    s.alpha_scale = 0.5f;
    world.add_component<mosaic::engine::ui::UiHoverStyle>(panel, s);
    world.add_component<mosaic::engine::ui::UiHovered>(panel,
                                                       mosaic::engine::ui::UiHovered{});

    const auto label = world.create_entity();
    mosaic::engine::ui::UiText t;
    t.font = font;
    t.text = "HP";
    t.alpha = 1.0f;
    world.add_component<mosaic::engine::ui::UiText>(label, t);
    world.add_component<mosaic::engine::ui::UiParent>(
        label, mosaic::engine::ui::UiParent{panel});

    rs.render(world, device, registry);

    REQUIRE(!device.ui_text_draws.empty());
    REQUIRE(device.ui_text_draws[0].quads.at(0).alpha == Approx(0.5f));
}
```

The capture members, verified while writing this plan: `ui_text_draws` and `ui_image_draws`
are vectors of draw-call structs with a `.quads` member, while **`ui_draws` is a vector of
vectors of `UiQuad` directly** — it has no `.quads`. `register_font(registry)` is defined at
`render_system_test.cpp:945`, in the file's second anonymous namespace, so it is visible to
cases appended at the end.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "a hovered panel emits the multiplied colour and alpha"
```
Expected: FAIL — `q.color.x` is 0.4 rather than 0.8, because the pass still emits the authored colour. (The "no hover style" case passes already; it is the migration guarantee and must be green before and after.)

- [ ] **Step 3: Implement**

In `engine/render/render_system.hpp`, add the include beside the other `engine/ui/` ones:

```cpp
#include "engine/ui/ui_hover_style.hpp"
```

**The panel pass** — the `alpha <= 0.0f` early-out stays on `p.alpha`:

```cpp
                    if (p.alpha <= 0.0f) { return; }
                    const auto rect = mosaic::engine::ui::resolve_ui_rect(
                        world, e, to_anchor(p.anchor), p.pos, p.size);
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), p.color, p.alpha);
                    pending.push_back({p.order, UiQuad{rect, app.color, app.alpha}});
```

**The image pass** — same shape, on `im.tint` and `im.alpha`, applied to the `UiImageQuad` the fit maths produced:

```cpp
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), im.tint, im.alpha);
                    device.draw_ui_image(
                        std::vector<UiImageQuad>{
                            UiImageQuad{q.rect, q.u0, q.v0, q.u1, q.v1, app.color, app.alpha}},
                        info.handle);
```

**The text pass** — the resolved values go into the existing `layout_text` call, replacing `t.color, t.alpha`:

```cpp
                    const auto app = mosaic::engine::ui::resolve_appearance(
                        mosaic::engine::ui::active_hover_style(world, e), t.color, t.alpha);
                    const std::vector<mosaic::engine::ui::TextQuad> tq =
                        mosaic::engine::ui::layout_text(*font, t.text, to_anchor(t.anchor),
                                                        t.pos, t.height, aspect_,
                                                        app.color, app.alpha, frame);
```

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build/debug && ./build/debug/tests/mosaic_unit_tests "[render]" 2>&1 | tail -4
```
Expected: PASS, including every pre-existing UI pass case — they have no `UiHoverStyle`, so `active_hover_style` returns nullptr and the emitted values are unchanged.

- [ ] **Step 5: Run the whole suite and the gates**

```bash
ctest --test-dir build/debug 2>&1 | tail -4
python3 tools/ci/check_dependency_rules.py && python3 tools/ci/check_test_case_names.py && echo "gates clean"
```
Expected: all passing, both gates silent.

- [ ] **Step 6: Commit**

```bash
git add engine/render/render_system.hpp tests/unit/render/render_system_test.cpp
git commit -F - <<'EOF'
feat(render): the UI passes resolve a hover style before emitting

All three passes -- panel, image and text -- resolve the effective appearance
through active_hover_style plus resolve_appearance instead of emitting the
authored values directly. Nothing mutates a component, so the authored colour is
never destroyed and a scene saved while hovering carries no hover tint.

Each pass's `alpha <= 0` early-out deliberately stays on the AUTHORED alpha. Alpha
0 means hidden, and a hover style must not be able to reveal a deliberately hidden
element -- which is also why a style scaling alpha from 0 emits nothing rather
than appearing on hover.

Every pre-existing case is untouched and still passes: nothing in the repository
has a UiHoverStyle, so active_hover_style returns nullptr everywhere and each pass
emits exactly what it emitted before.
EOF
```

---

### Task 5: make it visible, probe it, and hand it off

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `docs/project/next-steps.md`, `docs/project/deferred-work-backlog.md`, `docs/design/ui.md`

**Interfaces:**
- Consumes: everything above. Terminal task.

- [ ] **Step 1: Run the mutation probes**

Derived from the **code**, statement by statement. Script them into one run (each needs a rebuild), apply, rebuild, run `"[ui]"` and `"[render]"`, confirm the expected red, revert, and **rebuild before judging the baseline** (a restored source with a stale binary reports the mutant's results).

| # | Mutation | Must turn red |
|---|---|---|
| 1 | `ui_hover_style.hpp`: `resolve_appearance` returns `{color, alpha}` unconditionally (drop the style) | "a style multiplies every channel and the alpha", the RenderSystem multiply case |
| 2 | `resolve_appearance`: clamp with `std::min(v, 1.0f)` instead of `std::clamp` | "a negative multiplier clamps to zero rather than passing through" |
| 3 | `resolve_appearance`: drop the clamp entirely | the clamp-to-one case and the negative case |
| 4 | `resolve_appearance`: multiply colour but leave `alpha` unscaled | the multiply case, "an alpha scale of zero hides the element" |
| 5 | `resolve_appearance`: use `style->tint.x` for all three channels | the multiply case (asymmetric per channel) |
| 6 | `active_hover_style`: return `nearest` at the end instead of `nullptr` (style without a tag) | "a style present but not hovered yields no style" |
| 7 | `active_hover_style`: record the style on EVERY level, overwriting (farthest wins) | "the nearest style wins over an ancestor's" |
| 8 | `active_hover_style`: drop the `is_valid` check | the nil-parent and destroyed-parent cases |
| 9 | `active_hover_style`: raise `kMaxDepth` to 1000 | "a chain deeper than the cap yields no style" (the cycle case would hang, so run it with a timeout) |
| 10 | `active_hover_style`: require a `UiButton` alongside the tag | "a hovered entity needs no UiButton for its style to apply", the inheritance cases |
| 11 | `ui_system.hpp`: drop `world.remove_component<UiHovered>` on exit | "the hovered button carries a UiHovered tag", "moving between buttons moves the tag" |
| 12 | `ui_system.hpp`: move the `remove_component` inside the `emit_button_action` success path | "a button that loses its UiButton while hovered leaves no stale tag" |
| 13 | `builtin_components.hpp`: also register `UiHovered` | "UiHovered is deliberately NOT a registered component", "a saved scene contains no UiHovered even while hovering" |
| 14 | `render_system.hpp`: panel pass early-out uses the RESOLVED alpha | "a hover style never reveals a hidden panel" |

**One of the spec's eight named probes cannot be built, and that is worth stating rather than
letting it read as drift.** The spec lists "walk down instead of up". There is no way to walk
down: `UiParent` points upward and nothing stores children, which is the entire reason the
design walks up. Probe 7 is the nearest real equivalent — recording the style at every level
so the *farthest* ancestor wins instead of the nearest — and it breaks exactly the
precedence the spec's probe was aiming at. Note this in the handoff.

Record each result. A probe that produces no red is a finding to write down, not a failure to hide.

- [ ] **Step 2: Replace the demo's hover script with a style**

`app/run/demo_scene.hpp` currently has a **deferred** `ScriptEvent` subscriber that assigns `panel->alpha` on `hover_enter`/`hover_exit` (search for `subscribe_deferred`). Delete that subscriber entirely and author a `UiHoverStyle` on the HUD bar's entity instead — the same `camera` entity that carries the `UiPanel` and the `UiButton`:

```cpp
    // Hover feedback, now rendered by the engine rather than scripted. The HUD bar's panel,
    // button and style all live on this one entity, which IS the ownership rule: a button
    // owns the UI components on its own entity.
    //
    // This replaces a subscriber that assigned panel->alpha directly. That worked, and it
    // was the wrong pattern to ship as the example: it destroyed the authored alpha, relied
    // on the exit event arriving to restore it, and a scene saved while hovering would have
    // persisted the hover tint into the file.
    //
    // 1.4x brightens the blue bar noticeably without clipping it; alpha 0.6 -> 0.84 makes
    // the whole bar firmer rather than just recoloured. The nested "HP" label is parented to
    // this entity, so it inherits this style and brightens with the bar.
    world.add_component<mosaic::engine::ui::UiHoverStyle>(camera,
        mosaic::engine::ui::UiHoverStyle{
            .tint        = mosaic::foundation::math::Vec3{1.4f, 1.4f, 1.4f},
            .alpha_scale = 1.4f});
```

Keep the `hud_click` logger — it filters on `action == "click"` and is still the proof of the click path.

**Check the entity-count tests before moving on.** Adding a component (not an entity) should not affect them, but this project has four app tests asserting the default scene's shape:

```bash
./build/debug/tests/mosaic_unit_tests "[app]" 2>&1 | tail -4
```
Expected: PASS. If one fails on a component count rather than an entity count, update all four rather than one.

- [ ] **Step 3: Verify the suite, the gates and both entry points**

```bash
cmake --build build/debug && ctest --test-dir build/debug 2>&1 | tail -4
python3 tools/ci/check_dependency_rules.py && python3 tools/ci/check_test_case_names.py && echo "gates clean"
timeout 180 ./build/debug/app/mosaic run --window --ticks 90 2>&1 | tail -2; echo "exit=${PIPESTATUS[0]}"
timeout 180 ./build/debug/app/mosaic editor --ticks 90 2>&1 | tail -2; echo "exit=${PIPESTATUS[0]}"
timeout 180 ./build/debug/app/mosaic run --ticks 60 2>&1 | tail -1; echo "headless exit=${PIPESTATUS[0]}"
```
Expected: all tests passing, both gates silent, all three commands exit 0 with no `VUID` / `validation` / `error` lines. The headless run matters here specifically: it installs **no** pointer provider, so nothing should ever be hovered and no style should ever apply.

- [ ] **Step 4: Report what a human must check**

Screenshots are unavailable in this sandbox, so do not claim visual confirmation. Report:

> With `mosaic run --window`: moving the pointer over the HUD bar at the top-left should brighten **both the bar and its "HP" label** — the label inherits the style from the bar it is parented to, which is the ownership rule working. Moving away restores both. In `mosaic editor` it does nothing until **Play**, because the loop is stopped while in authoring mode.

- [ ] **Step 5: Update the three docs**

In `docs/project/deferred-work-backlog.md`, strike through **Engine-rendered hover visuals (a `UiHoverStyle` component)** with `**SHIPPED 2026-10-10**` and what actually happened, keeping the original reasoning. State that the `order`-disagreement item remains open and now has a concrete check available.

In `docs/design/ui.md`, extend section 5 — which already documents the three hover actions — with the rendered-feedback half: the two components, the ownership rule in one sentence, the fact that a style is inherited down a `UiParent` chain, and that a style cannot reveal an element whose authored alpha is 0.

Add a `**Last updated:** 2026-10-10 (...)` entry at the top of `docs/project/next-steps.md` in the existing dense-parenthetical style, covering: that the blocker was a non-problem because ownership already existed as entity identity, with the template-entity-8 evidence; why the walk goes up; that `UiHovered` is unregistered and why, with the save-path assertion; the multiplier-versus-absolute decision and its cost; the clamp's lower bound and why a negative is a real authored input; the probe results including any that produced no red; the gate count; and that the demo now uses a style instead of a script, with visual confirmation outstanding.

- [ ] **Step 6: Commit, push, open the PR**

```bash
git add app/run/demo_scene.hpp docs/project/next-steps.md docs/project/deferred-work-backlog.md docs/design/ui.md
git commit -F - <<'EOF'
docs: hover feedback shipped, and the demo stops scripting it

The demo authors a UiHoverStyle on the HUD bar instead of subscribing to hover
events and assigning panel->alpha. The engine renders the feedback now, so the
demo no longer demonstrates a pattern that destroyed the authored value and would
have persisted the hover tint into any scene saved while hovering.

The bar's nested "HP" label is parented to the same entity, so it inherits the
style and brightens with the bar -- which is the ownership rule visible by eye.
EOF
git push -u origin ui-hover-ownership
```
Then open a PR against `main` and request review before merging.

---
