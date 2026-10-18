# U8a — inherited alpha and stack layout — design

**Status:** spec, written 2026-10-17. The first half of U8 from
`docs/superpowers/specs/2026-10-07-ui-pillar-decomposition.md`.

> **Written without the usual per-section approval.** The user went to bed having
> said "start on U8, work as autonomously as you can", under a standing
> pre-authorisation to write a spec and implement without waiting for review.
> Every decision below that was theirs to make is marked **MY CALL** with the
> alternative stated, so each is cheap to overrule in the morning. The first one
> is the one the decomposition explicitly reserved for them.

## What U8 asked for, and what this spec is

U8 is *"inherited visibility/alpha, flow/grid/stack auto-layout"*. This spec
builds **inherited alpha and one container, `UiStack`** — and defers grid and
flow.

**MY CALL (scope).** Stack carries all the machinery the other two need:
enumerating a parent's children, ordering them deterministically, computing a
slot, and the rule about what an authored `pos` means under a layout. Grid and
flow are then the same machinery with different slot arithmetic — genuinely
additive, and better judged against a stack that exists than guessed at
alongside it. The alternative is building all three now; I think that triples the
surface of the one decision most likely to be wrong (below) before anyone has
seen it work. U7 was split mid-flight for the same reason.

Inherited alpha rides along rather than getting its own slice because it is the
same hierarchy walk `resolve_parent_frame` already does, over the same
`UiParent` chain, and it would be strange to build that walk twice in two
consecutive slices.

## The decomposition's shape for U8 is wrong

It says: *"A layout component on the parent (`UiStack` / `UiGrid` / `UiFlow`)
that overrides its children's `pos` during a pre-pass."*

**There is no pass to pre-empt, and overriding `pos` would be destructive.**

`engine/ui/ui_hierarchy.hpp` resolves geometry **lazily and purely**. Nothing
stores a rect. `resolve_ui_rect` is called independently by the draw path
(`render_system.hpp:475` for panels, `:512` for images, again for text) and by
the hit-test path (`ui_button_rects.hpp:29`), and each call recurses up the
parent chain on demand. So:

- **A pre-pass that wrote computed values back into `pos` would mutate authored
  data.** `pos` is reflected on all four UI components, so the overwrite would
  **serialise**: the first save after the first frame would destroy the authored
  value permanently, and the scene file would thereafter contain whatever the
  layout computed at whatever window size that frame happened to be.
- **It would reintroduce the failure class this project has already paid for
  twice**: two consumers computing geometry from different inputs. That is the
  `to_anchor` triplication (three copies with three independent bounds, fixed
  2026-10-12) and the `UiButton`/`UiPanel` `order` disagreement that
  `ui_order_check.hpp` exists to lint. A pre-pass makes every consumer's
  correctness depend on having run *after* it; lazy resolution makes draw and
  hit-test agree **by construction**.

**So layout resolves inside the existing resolution path.** This is the sixth
time in this arc a prediction has been found wrong by the code that contradicts
it, and the usual reason: the decomposition was written in 2026-10-07, before U6
and U7 put `resolve_ui_rect` in its current shape.

### And the hook goes in `resolve_parent_frame`, not `resolve_ui_rect`

There are **two** entry points into the hierarchy, not one, and the second is
easy to miss:

| Caller | Entry point |
|---|---|
| panels (`render_system.hpp:475`), images (`:512`), button hit-tests (`ui_button_rects.hpp:29`) | `resolve_ui_rect(world, e, anchor, pos, size)` |
| **text (`render_system.hpp:558`)** | **`resolve_parent_frame(world, e)`**, passed straight to `layout_text`, which does its own anchoring |

Hooking `resolve_ui_rect` would have laid out panels and images and **silently
left text where it was** -- a child that moves under a stack and a label that does
not, which is precisely the kind of partial correctness this UI has been bitten
by before.

`resolve_ui_rect` is a one-line wrapper over `resolve_parent_frame`, so hooking
the *frame* covers both callers at once. And it is the better model anyway: a
child's slot **is** the frame it resolves within, narrowed. Layout does not
compute positions -- it narrows the frame and lets the existing arithmetic run
unchanged.

## The decision the decomposition reserved for the human

> *"The honest tension: children's authored `pos` becomes ignored under a layout
> parent, which is surprising unless the Inspector shows it. Worth deciding how
> that is communicated."*

**MY CALL, and the question turns out to have a better answer than the one it
anticipates: `pos` is not ignored, and this needs no new rule at all.**

Because the hook narrows the *frame* (above), a child under a stack resolves with
`resolve_in(slot, anchor, pos, size)` -- the same call it already makes, with a
smaller frame. And `resolve_in` already treats `pos` as an offset from the anchor
within the frame. So "`pos` offsets the child from its slot" is not a decision I
am imposing; it is **what the existing arithmetic already does** once the frame is
the slot. `pos = {0, 0}` -- the default on every UI component -- lands the child
exactly on its slot.

That is a stronger position than I expected to be able to take: the alternative
("`pos` is ignored under a layout parent") would require *adding* a special case
to suppress arithmetic that already works, and then explaining the suppression in
the Inspector.

It also **dissolves** the tension rather than communicating it:

- Nothing is ignored, so there is nothing to explain in the Inspector, and no
  special case in reflection, serialisation or the editor.
- An author who drags `pos` sees the element move, which is what dragging a
  position field should do. Under the "ignored" reading they would see nothing
  happen and conclude the field, or the layout, is broken.
- It composes: adding a `UiStack` to an existing parent shifts its children into
  slots without silently discarding the offsets someone already tuned.
- The failure mode is mild and self-evident — an element that looks misplaced
  within its slot, with a non-zero `pos` visible in the Inspector explaining
  exactly why.

**The alternative** is the decomposition's own: `pos` is ignored under a layout
parent, and the Inspector greys the field out or annotates it. That is more
honest about "the layout owns this axis" and avoids a reading where `pos` means
two different things depending on an ancestor. I rejected it because the editor
work it requires is the real cost, and because a greyed-out field still has to
explain *why*, which is a worse version of the thing it is trying to avoid.

**If you disagree, this is the cheapest thing in the slice to change**: it is one
addition in `layout_slot`'s caller and the tests that pin it.

## Sibling order comes from an authored key, never from iteration order

**This is the sharpest correctness point in the slice.** A stack has to put its
children in *some* order, and the obvious source — the order `for_each` visits
them — is unusable.

`engine/ecs/component_registry.hpp:130` documents `for_each` as visiting "in
dense order", and the storage is a sparse set. A sparse set's dense array is
**insertion order mutated by swap-and-pop on removal**: destroying any entity
with that component moves the last one into the hole. So a stack ordered by
iteration would silently reshuffle when an **unrelated** entity was destroyed —
including an entity in a different part of the UI, or a non-UI entity that
happens to carry a `UiPanel`.

That is the same hazard the backlog already records for draw `order` ("an
unrelated destroy can reorder an equal-order run"), and it is worse here: draw
order reshuffling is a z-fighting flicker, but layout reshuffling moves elements
to different places on screen.

**Children therefore sort by `(order, entity index)`** — `UiPanel::order` (or the
corresponding field on the child's own component) as the authored key, with the
entity's index as a *deterministic* tiebreak only. The tiebreak exists so that
two children with equal `order` land in a stable, reproducible order rather than
a storage-dependent one; it is explicitly **not** a feature to rely on.

**MY CALL (ties are linted, not silent).** Equal `order` among a stack's children
gets the same treatment `ui_order_check.hpp` gives the button/panel case: a
heuristic lint naming the entities and the shared value, reported once, not a
hard error. The alternative — defining ties as "whatever the tiebreak says" and
staying quiet — is what makes a layout look stable in testing and reshuffle in
the field. The alternative in the other direction, an assertion, is wrong because
a tie is legal and often harmless.

## Inherited alpha

A child's effective alpha is its own authored alpha times every ancestor panel's,
up the `UiParent` chain. Fading a container fades everything in it.

**It is a render-time override, and the authored value survives.** This is
`resolve_appearance`'s existing discipline, stated in `render_system.hpp`: *"Hover
feedback is a render-time override: nothing writes to p.color, so the authored
value survives and a scene saved while hovering carries no hover tint."* Inherited
alpha works the same way — nothing writes back, so a scene saved while a
container is mid-fade carries the authored alphas.

**The early-out stays on the AUTHORED alpha.** All three HUD passes currently
skip an element whose own `alpha <= 0`, and `render_system.hpp` says why for the
hover case: *"0 means hidden, and a style must not reveal it."* The same rule
holds here in both directions — an element authored at 0 stays hidden whatever its
ancestors say, and the inherited product reaching 0 hides a child whose own alpha
is 1. Both are early-outs; neither writes anything.

**MY CALL (no `visible` bool).** U8 says "inherited visibility/alpha", and alpha
covers visibility functionally: 0 is hidden. I am not adding a `visible` field,
for a specific reason beyond economy — **adding a field to `UiPanel` re-binds the
positional brace initialisers in `app/run/demo_scene.hpp`**, and because
`UiPanel`'s fields would make the new one adjacent to same-typed neighbours, the
compiler would not necessarily catch it. That is a known trap in this repo with a
memory of its own. A real `visible` bool is deferred, and it is *not* the same
feature: `alpha = 0` still hit-tests, so an invisible panel still eats clicks.
That question belongs to U6's hit-testing, and it is in the backlog.

## The shape

Two new pure headers in `engine/ui/`, matching the file-per-concern style of
`ui_fit.hpp`, `ui_hit.hpp`, `ui_button_rects.hpp` and `ui_order_check.hpp` — each
testable with no world and no device where possible, each with its own test file.

| File | Responsibility |
|---|---|
| `engine/ui/ui_stack.hpp` (new) | The `UiStack` component, and `layout_slot(...)` — **pure arithmetic**, no world: given a parent rect, a child count, an index, and the stack's parameters, return the child's slot rect. |
| `engine/ui/ui_layout.hpp` (new) | The world-aware half: `stack_children(world, parent)` (deterministically ordered), `layout_slot_for(world, e)` (this child's slot, or nothing if its parent has no `UiStack`), and `inherited_alpha(world, e)`. |
| `engine/ui/ui_hierarchy.hpp` (modify) | **`resolve_parent_frame`** returns the child's slot instead of the parent's rect when the parent has a `UiStack`. One change, in the one function **all four** consumers funnel through -- panels, images and button hit-tests via `resolve_ui_rect`, and text directly. |
| `engine/render/render_system.hpp` (modify) | Three passes multiply by `inherited_alpha`. |
| `engine/reflection/builtin_components.hpp` (modify) | One registration line. |

```cpp
struct UiStack
{
    int   axis    = 0;     // 0 = vertical (children stack downward), 1 = horizontal
    float spacing = 0.0f;  // normalized gap between slots, in the parent's frame
    float padding = 0.0f;  // normalized inset on all four sides of the parent's rect
};
```

**`axis` is an int, not an enum**, matching `anchor` on all four UI components:
the reflection layer and the scene format already carry `anchor` that way, and a
scene file can hold any int, so the clamp-on-read discipline `to_anchor`
established applies unchanged.

**Slots divide the parent's padded rect evenly along the axis.** A vertical stack
of three children in a padded rect of height `h` gives each a slot of height
`(h - 2*spacing) / 3`. Equal division rather than content-sized, because nothing
in this UI measures content: `UiText` has no measured height until the font
atlas is resolved, and making layout depend on that would make it depend on asset
loading. Content-sized slots are deferred.

**The child's own `size` still applies**, resolved *within* its slot by the
existing `resolve_in`. So a child with `size = {1, 1}` fills its slot and one with
`size = {0.5, 1}` fills half of it, anchored as it asks. Layout decides *where*;
the child still decides how big and against which corner — which keeps `anchor`
meaningful under a layout and needs no new rule.

## Testing

The arithmetic half (`layout_slot`) needs no world: index/count/axis/spacing/
padding in, rect out. That is where the edge cases live and where they are
cheapest to pin.

- One child fills the padded rect; two split it; `spacing` eats from the middle,
  not the ends.
- `padding` insets all four sides, and a padding large enough to invert the rect
  clamps to zero extent rather than producing a negative one.
- A horizontal stack is the vertical result with the axes swapped, pinned as a
  property so the two cannot drift.
- `count = 0` and an out-of-range `index` return an empty rect rather than
  dividing by zero.

The world half needs a world but no device:

- `stack_children` is ordered by `order`, and **a destroy elsewhere in the world
  does not change it** — the test that would have caught the iteration-order
  hazard, written by destroying an unrelated entity between two enumerations.
- A child whose parent has no `UiStack` gets no slot, and resolves exactly as it
  does today — the regression guard for every existing scene.
- `resolve_ui_rect` agrees between the draw and hit-test call paths under a
  layout, asserted by calling it the way each caller does.
- **Text is laid out too.** `resolve_parent_frame` returns the slot for a text
  child of a stack, called exactly as `render_system.hpp:558` calls it. This is
  the case an implementer who hooked `resolve_ui_rect` would have shipped broken,
  and no existing test would have failed.
- `inherited_alpha` multiplies down a three-deep chain, returns the element's own
  alpha with no parent, and is unaffected by a parent that has no `UiPanel`.
- The authored `pos` offsets a child from its slot, and `pos = {0,0}` lands it
  exactly on the slot.

**Mutation probes**, derived from the code: `layout_slot` ignoring `spacing`;
ignoring `padding`; the sort dropped from `stack_children`; `inherited_alpha`
returning the parent's alpha instead of the product; `resolve_ui_rect` resolving
in the parent frame when a slot exists.

## What this cannot verify

Whether a stack *looks* right needs a human at the window — but less than usual,
because the arithmetic is pinned numerically and the demo gives an authored
constant to check against. One verification item: **three panels in a vertical
stack with `spacing = 0` must exactly tile their parent**, with no visible seam
and no overlap, at two different window sizes. A seam means the slot division
leaves a gap; an overlap means it double-counts spacing.

The editor's Inspector will show `UiStack`'s three fields through reflection with
no work, so authoring one is reachable immediately — but nothing in the editor
*visualises* a slot, so an author tuning `padding` is reading numbers. A slot
gizmo is deferred and in the backlog.

## Non-goals

- **`UiGrid` and `UiFlow`.** Same machinery, different slot arithmetic; deferred
  until a stack exists to judge them against.
- **Content-sized slots.** Needs content measurement, which needs the font atlas,
  which would make layout depend on asset loading.
- **A real `visible` bool.** Not the same feature as `alpha = 0`, because of
  hit-testing; belongs with U6.
- **Clipping and scroll.** U9, deliberately, and for the reason the
  decomposition gives: clipping is a render-device feature, not a layout one.
- **A slot gizmo in the editor.** Authoring works through reflection today;
  visualising a computed rect is editor work with its own design.
- **Nested stack interaction with `anchor` beyond "the child anchors within its
  slot".** That rule is stated above and tested; anything cleverer needs a
  reason.
