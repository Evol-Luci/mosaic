# UI — Hover Feedback and the Ownership Rule (design)

**Date:** 2026-10-10
**Status:** approved design, not yet implemented
**Backlog items:** **Engine-rendered hover visuals (a `UiHoverStyle` component)** and, indirectly,
**A button's `order` can disagree with its visual's `order`** — both recorded 2026-10-10 as
blocked on the same missing thing: a definition of "this visual belongs to this button".

## Intent

U6 shipped hover *events*. The engine renders no hover feedback of its own, so a button
acknowledges the pointer only if someone writes a script for it — which is not what a person
expects from a button. "The UI feels dead" is therefore half-fixed.

Fixing the other half needed an ownership relation that did not exist. This slice defines
that relation and uses it to render hover feedback.

## What scoping measured

Six findings. The first dissolves most of the problem, and three changed the design.

1. **The ownership relation already exists: it is entity identity.** Every place a button is
   authored in this repository puts the button and its visuals on **one entity**.
   `templates/default/scenes/main.json`'s entity 8 carries `UiButton`, `UiImage`, `UiPanel`
   **and** `UiText` together; `app/run/demo_scene.hpp` adds `UiButton` to the `camera` entity
   that already holds the HUD `UiPanel`. There is **no counter-example** — nothing anywhere
   puts a button on its own entity with its visual on another.

   So the backlog's framing — that a new relation must be designed — was wrong. The
   convention was already in the data; it had simply never been written down or relied upon.

2. **`UiParent` points UP, and is a *layout* relation rather than a hierarchy.**
   `resolve_parent_frame` (`engine/ui/ui_hierarchy.hpp`) reads a child's `UiParent` and
   resolves within the parent's **`UiPanel`** rect, falling back to the whole screen if the
   parent has no panel. Nothing stores children. So "the button plus its descendants" would
   require scanning every entity, while "this element's hovered ancestor" is one upward walk
   in the direction the data already runs.

3. **`types.save` serializes every *registered* component type**
   (`engine/reflection/component_reflection.hpp:105-111`, via `engine/scene/scene.hpp`). A
   runtime hover marker must therefore stay **unregistered**, or saving a scene while the
   cursor rests on a button would persist a phantom hover into the file — and loading it
   would start up already hovered.

4. **All three UI render passes share one shape:** read an authored `Vec3` colour/tint plus a
   `float` alpha, then emit (`render_system.hpp`, the panel/image/text passes). So a single
   pure function serves all three, and there is no need for three overrides.

5. **The demo's existing hover script demonstrates the unsafe pattern.** It assigns
   `panel->alpha = 0.9f` on enter and restores it on exit. Fine for a demo, wrong for the
   engine: it destroys the authored value, depends on the exit event to restore it, and **a
   scene saved while hovered would persist the hover tint.** The engine's version must be a
   render-time override, never a mutation.

6. **An unregistered marker is invisible to the Inspector.** This corrects a claim made while
   choosing the approach: the ECS-tag approach was argued partly on being inspectable, and it
   is not, because the Inspector works off reflection. The approach still wins on the other
   grounds (one source of truth, no new seam, ECS grain), but that argument is withdrawn.

## The decisions, and what each costs

**Ownership is "the element's own entity, or its nearest `UiParent` ancestor".** Resolved by
walking up. `UiSystem` tags exactly one entity — the hovered button — and each render pass
asks whether this element is itself tagged or descends from something tagged.

*Cost:* a button and its visual **cannot** live on separate entities. Any layout that wanted
that must move the visual onto the button's entity. Acceptable because nothing in the
repository does it, and the alternative — an explicit `visual` `EntityID` on `UiButton` — is a
second source of truth about a relation the authoring convention already expresses, which is
the exact defect shape that produced every bug in the U6/U7 sessions.

**Two components, with deliberately opposite registration.** `UiHoverStyle` is authored data
and is registered. `UiHovered` is runtime state and is **not**.

*Cost:* `UiHovered` does not appear in the Inspector, so the engine's idea of what is hovered
is not directly visible. Accepted in exchange for immunity to the persistence trap in
finding 3.

**Style is a multiplier, not an absolute colour.** `tint = {1,1,1}` and `alpha_scale = 1.0`
make a default-constructed `UiHoverStyle` an exact no-op, and one style composes across
differently-coloured elements.

*Cost:* a hover state cannot be a *specific* colour, only a scaling of the authored one. An
absolute override would need an "is set" flag to be safely default-constructed, and is
recorded as a non-goal.

**Feedback is a render-time override, never a mutation.** Nothing writes to `UiPanel::color`,
`UiImage::tint` or `UiText::color`.

*Cost:* one asymmetry, stated in full below.

## Where the work happens

```cpp
// engine/ui/ui_hover_style.hpp

// AUTHORED. Reflected and saved. Multiplies the authored appearance while hovered.
struct UiHoverStyle
{
    Vec3  tint        = Vec3{1.0f, 1.0f, 1.0f};
    float alpha_scale = 1.0f;
};

// RUNTIME ONLY, deliberately NOT registered -- see finding 3.
struct UiHovered {};

// The style to apply to `e`, or nullptr for "render the authored values".
//
// ONE upward walk answers both questions: is this element hovered (itself or via an
// ancestor), and which UiHoverStyle governs it (the nearest one found on the way up).
// Capped at the same depth 16 resolve_parent_frame uses, so a UiParent cycle terminates.
const UiHoverStyle* active_hover_style(World& world, EntityID e);

// The arithmetic, pure: no world, no component lookup.
struct UiAppearance { Vec3 color; float alpha; };
UiAppearance resolve_appearance(const UiHoverStyle* style, Vec3 color, float alpha);
```

The split is deliberate: `active_hover_style` does the lookup and the walk, `resolve_appearance`
does the multiply and the clamp. The second is testable with no world at all — the fourth time
this slice pattern has been used (`resolve_image_quad`, `topmost_hit`, `should_start_playing`),
and in all three previous cases the extracted function is where the real bugs were found.

**Two consequences fall out of walking up, both wanted.** The demo's child `UiText` label
brightens with its parent panel with no descendant bookkeeping. And a style authored on the
panel is **inherited** by that label, so hover is configured once per button rather than once
per visual.

## The change

**`engine/ui/ui_hover_style.hpp`** (new) — the two components plus both functions. Header
only, `engine/ui` tier, depending on `ui_parent.hpp`, `foundation/math/vec3.hpp` and
`engine/world/world.hpp`.

**`engine/reflection/builtin_components.hpp`** — register `UiHoverStyle` only. `UiHovered` is
deliberately absent, and that absence is asserted by a test rather than left to be "fixed".

**`engine/ui/ui_system.hpp`** — `hover_enter` adds `UiHovered` to the hovered entity;
`hover_exit` removes it. `UiSystem` becomes a component **writer** for the first time.

**`engine/render/render_system.hpp`** — the three UI passes resolve the appearance before
emitting. Two lines each.

**`app/run/demo_scene.hpp`** — the hover *script* is replaced by a `UiHoverStyle` on the HUD
panel. The demo stops demonstrating the unsafe mutation pattern.

## The rules that are load-bearing

**A hover style must never reveal a hidden element.** Each pass's `if (alpha <= 0.0f) return;`
early-out stays on the **authored** alpha, not the resolved one. Alpha 0 means hidden, and a
hover style must not be able to override that.

The flip side, stated rather than fixed: `alpha_scale = 0` still emits a fully transparent
quad instead of skipping the draw. That is a wasted draw call with no visual difference, and
adding a second early-out on the resolved alpha is how the two conditions drift apart.

**Hover with no style changes nothing.** `active_hover_style` returns `nullptr` when the chain
is hovered but carries no `UiHoverStyle` anywhere. Hovering is not itself a visual state; the
style is what makes it one.

**The nearest style wins.** An element with its own `UiHoverStyle` uses it rather than an
ancestor's, so a child can opt out of inheritance by authoring its own.

**Every existing scene renders byte-identically.** Nothing has a `UiHoverStyle`, so
`active_hover_style` returns `nullptr` everywhere and every pass emits exactly what it emits
today. This is the migration guarantee and it is pinned through the real `RenderSystem`.

## Testing

**`resolve_appearance`, pure:**

1. **`nullptr` leaves both untouched** — the migration guarantee, and the case every existing
   scene takes.
2. **Both values multiply**, with asymmetric numbers so a transposed channel fails.
3. **Values above 1 clamp to 1**, for colour and alpha both. UI colour is LDR.
4. **`alpha_scale = 0` yields 0**, not the authored alpha — the override must be able to hide.

**`active_hover_style`, where the subtlety is:**

5. **A style present but NOT hovered** → `nullptr`. The natural wrong implementation returns
   the style whenever it finds one, which would make every styled element permanently
   highlighted.
6. **Hovered with a style on the same entity** → that style.
7. **Hovered ancestor, child has no style** → the ancestor's style. This is inheritance, and
   it is what makes the demo's label brighten.
8. **Child has its own style** → the child's, not the ancestor's.
9. **Hovered with no style anywhere** → `nullptr`.
10. **A chain deeper than 16** → `nullptr` rather than unbounded recursion.
11. **A `UiParent` cycle terminates** — asserted, because the depth cap is the only thing
    preventing it and a cycle is authorable in a scene file.

**Through the real `RenderSystem`:**

12. **A panel with no hover style emits its authored colour and alpha** — migration, pinned at
    the seam rather than only in the pure function.
13. **A hovered panel with a style emits the multiplied values.**
14. **A child `UiText` under a hovered panel inherits**, which no pure test can see because it
    is the pass's lookup that walks the chain.

**On `UiSystem`:**

15. **`hover_enter` adds `UiHovered`; `hover_exit` removes it.**
16. **An entity that loses its `UiButton` while hovered leaves no stale `UiHovered`.** This
    extends a U6 edge case: the exit event is deliberately silent there because the channel
    name lived on the component, but the tag must still come off or the element stays
    highlighted forever.

**On registration:**

17. **`UiHoverStyle` round-trips through reflection** at non-default values, and is a
    registered built-in — matching every other UI component.
18. **A scene saved while hovered contains no `UiHovered`.** The persistence trap, asserted
    directly rather than trusted to the absence of a registration line.

**Mutation probes derived from the CODE:** drop the `nullptr` check in `resolve_appearance`;
drop the clamp; multiply colour but not alpha; return the style without checking for
`UiHovered`; walk down instead of up; remove the depth cap; forget the `UiHovered` removal on
exit; register `UiHovered`.

## Non-goals

- **Validating that a button's `order` agrees with its visual's.** This slice *unblocks* it —
  "same entity" is now the stated rule, so the check is "an entity with both a `UiButton` and
  a `UiPanel`/`UiImage`/`UiText` whose `order` values differ" — but it is a **diagnostic**,
  not a rendering feature, and bundling a warning system into a rendering slice is how a
  reviewer loses track of which half to scrutinise. Its backlog entry is updated with the rule
  it can now use.
- **An absolute hover colour.** Multipliers cannot express "highlight to exactly this blue".
  Adding it later means an extra field plus a precedence rule, which is cheap on this
  machinery and is not needed by anything today.
- **Hover on non-button components.** `UiPanel`/`UiImage`/`UiText` are still not hit-testable;
  only `UiButton` is. Already recorded.
- **Pressed and disabled visual states.** The same mechanism would serve them (a tag plus a
  style), but each needs its own decision about precedence when several apply at once.
- **Transitions or easing.** Hover is instantaneous. Animating it needs a per-element clock
  the UI subsystem does not have.
- **Pointer capture and drag.** The remaining third of U6, already recorded.
