# U9 — clipping and scroll regions: decomposition

**Date:** 2026-10-26
**Status:** decomposition, drafted from what is written down plus a verification pass over
its stated premises. Four ordered slices: **U9a → U9b → U9c → U9d**.
**Source items:** the 🔴 `U9 clipping and scroll regions` row added by the
[2026-10-26 tier re-derivation](../../record/2026-10.md#the-2026-10-26-tier-re-derivation-the-ui-pillars-next-group-is-the-question),
the `U9` group in the
[UI pillar decomposition](2026-10-07-ui-pillar-decomposition.md), and the live UI-section
bullets named per slice below.

---

## Why this one, and why now

U8 is complete across five slices, and it was the 🔴 table's only UI row. The human chose U9
over U11 after both were put with their counter-arguments. U9 is the **enabling** candidate:
it is a renderer change before it is a UI change, and the UI items behind it cannot be
decided until the hook exists.

---

## Three stated premises, checked first. Two were wrong.

The tier row and the pillar spec were written on 2026-10-26 and 2026-10-07 respectively.
Verifying them before designing was worth it, and the corrections belong here rather than in
a slice's review.

**1. "The neutral `RenderDevice` exposes no scissor." — HOLDS.** `engine/render/` contains no
occurrence of `scissor`. Nothing in `render_device.hpp` can ask for a sub-rect, and the three
UI draw entry points (`draw_ui`, `draw_ui_image`, `draw_ui_text`, declared at
`render_device.hpp:312-320`) take only their quad vectors.

**2. "The Vulkan backend sets a full-extent `VkRect2D` internally
(`vulkan_render_device.cpp:1833`)." — WRONG IN ITS DETAIL, AND THE DETAIL MATTERS.** The file
is `modules/render_vulkan/vulkan_render_device.cpp`, not `engine/render/`, and there are
**four** dynamic `vkCmdSetScissor` sites, not one: lines **1999, 2445, 2533, 2634** — the
swapchain pass, the viewport pass, the HDR pass and the resolve/blit — each full-extent
against its own `extent_` / `vp_extent_` / `hdr_extent_` / `dst_extent`. A fifth scissor is
**static pipeline state** for the shadow map (`:4271`, `kShadowMapSize`) and is not part of
this work.

*Why the correction changes the estimate:* the pillar spec called clipping "the single most
under-estimated item on the list", on the reading that dynamic scissor state would have to be
introduced. It is already there and already set every frame in four places. What is missing
is **interface surface and a per-draw set**, not dynamic-state plumbing. U9a is therefore
smaller than the source documents imply — and this is the one direction a stale estimate is
dangerous in, because an item believed large gets deferred without being re-checked.

*How the error happened, since it is my own and recent:* the tier row cites
`grep -ri scissor engine/render/`, whose denominator is narrower than the claim drawn from
it. `modules/` is a separate tree. This is the same class of mistake as the four denominator
errors recorded across the U8 arc, made again within hours of writing that record entry.
**Corrected in the backlog row as part of this slice.**

**3. "Scroll depends on the deferred `PointerState` widening." — HALF OF IT ALREADY SHIPPED.**
`PointerState` carries `inside` (`engine/ui/pointer.hpp:25`), added 2026-10-12, and both
providers set it: `mosaic::app::window_pointer_state` in `app/window_pointer.hpp` and the
editor's `studio/viewport.hpp`. The backlog bullet that U9's row leaned on is explicitly
marked *"(superseded entry, kept for the reasoning)"* and its sentence "that widening is the
same one U9 needs for wheel and multi-button" is what remains true.

So what U9 actually needs is a **wheel delta**, which nothing carries, on a struct whose
widening precedent is already set and already documented. Multi-button is **not** needed by
U9 and is not in scope. **Also corrected in the backlog row:** it claimed "three live UI
items are queued behind it"; the honest count is **one** live item (a grid's overflow
children) whose three candidate policies all need the hook.

---

## Settled decisions, so they are not re-litigated per slice

**The clip is a parameter on the draw call, never device state.** A `set_clip` /
`push_clip` pair would be per-frame device state that must be reset, and this project has
paid twice for exactly that shape: the per-frame staging-buffer aliasing of #275 and #287,
whose remaining non-latent gap is still an open 🟡 row (`begin_frame`'s counter resets). A
clip left set from the previous frame draws the next frame's HUD into a stale rect and no
test would see it. Pass it; do not store it.

**Grouping is by clip, and `RenderSystem` does the grouping.** The HUD pass already groups
`draw_ui_image` and `draw_ui_text` calls by texture (`render_system.hpp:588` and `:634`); a
clip is one more grouping key. Per-quad clipping is rejected: it would mean one
`vkCmdSetScissor` per quad and destroy the batching the pass was built around.

**Clipping must not reuse the frame-narrowing hook.** `resolve_parent_frame`
(`ui_hierarchy.hpp:25-48`) narrows a child's *frame*, which relocates and resizes it —
`resolve_in` treats `pos` as an offset within the frame and `size` as a fraction of it. A
clip must change **neither** position nor size, only which pixels survive. It is therefore a
separate walk returning an intersected rect, not a second `slot_for_child`.

**Clip applies to the hit test in the same slice that applies it to the draw.** Shipping a
clip that hides a button but leaves it clickable would be the U8b bug class exactly — a faded
visual over a fully live button — and that one was found by a human looking at a screenshot,
not by a test. `button_hit_candidates` (`ui_button_rects.hpp:35`) is the single flatten both
the press and hover paths use, and `find_order_conflicts` draws its rects from it, so one
filter there reaches every consumer. This is not a separate slice.

**`UiFlow`'s lesson applies to any new enum or axis field:** one meaning everywhere, a
differing default at most.

---

## The slices

### U9a — a clip sub-rect on `RenderDevice`, asserted in pixels

**Shape.** Add an optional clip rect to the three UI draw entry points, defaulting to the
full surface so every existing call site is unchanged. Implement it in the Vulkan backend as
a `vkCmdSetScissor` before the draw and a restore to full extent after, in whichever of the
four passes the UI draws in. No UI-side behaviour yet.

**Gate.** A GPU pixel test under the offscreen harness: a UI quad drawn with a clip covering
half of it leaves the other half's pixels **exactly** at the clear colour, and the clipped
half is unchanged from an unclipped draw. Then the decisive mutation: **delete the
`vkCmdSetScissor` call and watch that test go red** — run it, read which assertion fails, and
confirm it is the outside-the-clip pixel and not an incidental one.

**Why first.** Nothing above it can be built or decided without the hook, and this is the only
slice whose risk is in the backend.

**Watch for.** The renderer gate (`timeout 12 env MOSAIC_VK_SYNC_VALIDATION=1 ... editor`,
exit **124**, three `[INFO]`, zero VUIDs) must be captured on unmodified `main` and diffed
after. A scissor outside the render area is a validation error, and that is precisely the
class `ctest` cannot see.

### U9b — `UiClip`: intersect down the hierarchy, for drawing AND hit-testing

**Shape.** A component on a parent declaring that its subtree is clipped to the parent's
resolved rect. Resolution walks the parent chain intersecting each ancestor's clip, exactly
as rect arithmetic — pure, headless-testable, no device involved. `RenderSystem` groups HUD
quads by the resolved clip and passes it to U9a's parameter. `button_hit_candidates` drops a
candidate whose rect is entirely outside its clip, and clips the rect it reports where it is
partly outside.

**Gate.** Property tests on the intersection (nested clips intersect, an empty intersection
yields no draw and no hit candidate, a clip is unaffected by the child's own anchor) plus one
GPU pixel test for a nested clip. Mutation: replace the intersection with "use the innermost
clip" and watch a nested case go red — the two differ only when an inner clip reaches outside
its ancestor, which is the authored mistake this guards.

**Depends on** U9a's parameter existing.

### U9c — a wheel delta on `PointerState`, and both providers

**Shape.** One field, following `inside`'s precedent and its comment style: a default that
keeps every existing aggregate initialisation meaning what it means today. GLFW's scroll
callback accumulates per frame in `app/main.cpp`'s provider; the editor's
`studio/viewport.hpp` provider reports ImGui's wheel, gated on the same viewport-hover
condition it already gates `inside` and `down` on.

**Gate.** The policy is a pure function and is tested as one (`window_pointer_state`'s
existing shape). A wheel delta reaching `UiSystem` with no scroll region present must change
nothing — that is the regression this slice can introduce. Mutation: drop the per-frame reset
of the accumulator and watch a test catch the delta persisting into the next tick.

**Independent of U9a/U9b**; required by U9d. Takeable in parallel.

### U9d — `UiScroll`: a clipped subtree with an offset

**Shape.** Offset applied to a clipped subtree's children, bounded by the content extent, and
driven by the wheel when the pointer is inside the region. The first authored scroll region
goes in the demo scene, where its correctness is visible.

**Gate.** Headless: an offset moves children and the clip does not move; scrolling past the
end clamps; a child scrolled out of the clip produces no hit candidate (U9b's filter,
exercised through a real scroll rather than an authored clip). Eye: a human scrolls it, which
no gate here can do.

**Depends on** U9b and U9c.

---

## Non-goals, each recorded in the backlog in the same turn as this spec

- **The grid's overflow policy enum (clip / scroll / grow).** Becomes *decidable* after U9d
  but is its own authored-policy decision, and "grow" was already declined by the human.
- **Scrollbar visuals**, and any affordance showing that content continues.
- **Nested scroll regions and scroll chaining** (which region consumes a wheel delta).
- **Smooth or inertial scrolling.** The wheel delta is discrete; easing is a separate
  feature with its own frame-timing questions.
- **Multi-button pointer support.** Named beside the wheel in the source bullet; not needed
  by U9.
- **Clipping any pass other than the three UI draws.** World labels draw before the UI passes
  and are not clipped by a HUD rect; the unified z-order across that boundary is the separate
  Rendering-section item.
- **A clip-aware order lint.** `find_order_conflicts` inherits U9b's filter for free because
  it reads `button_hit_candidates`; `find_order_disagreements` reads `for_each<UiButton>`
  directly and will not, which is the same open 🟢 decision U8b left.
- **A `clip_resolves` counter.** U9b adds a second parent-chain walk per element per pass,
  next to the one `ui_layout_stats.hpp` already counts. Whether it earns its own counter
  depends on the same "is this measurable at all?" question U8d answered with a no.

---

## What belongs to the human

Merging each slice; the eye check at U9d (a person scrolling a real region); and any ruling
where a slice argues my decided calls above are wrong **with evidence** — that is the process
working, not a delegate overstepping.
