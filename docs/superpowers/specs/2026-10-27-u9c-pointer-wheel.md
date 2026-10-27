# U9c — a wheel delta on `PointerState`, and both providers

**Date:** 2026-10-27
**Status:** per-slice plan, **amended 2026-10-27** after the coordinator's ruling took wrinkle 2 into this slice. The oracle and the premise checks are as originally written; the two marked sections below changed. Parent: [U9 decomposition](2026-10-26-u9-clipping-scroll-decomposition.md), `### U9c`.
**Ships:** the field and both providers. **No consumer** — `UiScroll` and anything that
reads the delta is U9d.

---

## The oracle, written before anything was measured

**The observation that would prove this slice wrong:**

> Both callers of the pointer provider within one tick observe the **same** wheel delta. If
> the second caller sees `0` where the first saw a non-zero value, the accumulator is being
> consumed by the read, and the two systems disagree about the pointer inside a single tick —
> which is exactly what `app/main.cpp:300`'s provider comment forbids.

Concretely, as a test: read the provider twice with no frame boundary between the reads and
require the two `wheel` values equal. Second, the complement: with a frame boundary between
them, require the second to be `0` — a delta that survives its frame is applied twice.

A secondary oracle, for the regression this slice can introduce:

> A non-zero wheel delta reaching `UiSystem` with no scroll region present changes
> **nothing** — the same emitted events, the same hover, the same `press_consumed()` — as the
> identical sequence with a zero delta.

"`ctest` is green" is not the oracle; the suite was green before the field existed.

---

## Premises, checked

**P1. The provider is invoked TWICE per tick.** HOLDS. `UiSystem::update` calls `pointer_()`
at `engine/ui/ui_system.hpp:51`; `ClickNudgeSystem::update` calls it at
`app/run/click_nudge.hpp:49-50`. Both are registered on the same `World`
(`setup_runtime` adds `UiSystem`, `register_windowed_systems` adds `ClickNudgeSystem` after
it), both receive the **same** `glfw_pointer` lambda, and `app/main.cpp:300-301` states the
requirement in words. **Consequence, which is the design:** the read must not consume.

**P2. The frame loop ticks BEFORE it polls, and may tick 0..8 times per frame.** HOLDS, and
it is stronger than "once per frame". `FramePump::run` calls `loop_.advance(world, real_dt)`
and *then* `deps_.on_frame()` (`app/run/frame_pump.hpp:64-72`), and `on_frame` is where
`glfwPollEvents()` lives (`app/main.cpp:411`). `RuntimeLoop::advance`
(`engine/runtime/runtime_loop.hpp:27-46`) is a fixed-timestep accumulator capped at
`max_steps_per_advance = 8`. So one frame's scroll events can be observed by **zero, one, or
up to eight** ticks. See "Two wrinkles" below; neither is observable in U9c and both belong
to U9d, which is the slice that decides what *applying* a delta means.

**P3. `inside`'s precedent is "last field, neutral default, a comment saying why".** HOLDS
(`engine/ui/pointer.hpp:25` and its comment). Both providers set it, and both are pinned.

**P4. The editor needs no accumulator of its own.** HOLDS. `ImGui::GetIO().MouseWheel` is
already a per-ImGui-frame accumulated value that ImGui resets in `NewFrame`. The editor's
reset is therefore "the next frame's `set_pointer_sample` overwrites the sample" — still
frame-loop driven, still not read-driven. `ViewportPanel::pointer_state` is `const` and
cannot consume anything, which is the property that makes P1 safe on this side for free.

**P5. GLFW's scroll is event-driven only.** HOLDS. There is no `glfwGetScroll`; only
`glfwSetScrollCallback`. An accumulator exists somewhere by necessity.

---

## Where the accumulator lives, who resets it, and what two reads see

`mosaic::app::WheelAccumulator`, in `app/run/window_pointer.hpp` beside the policy it feeds.

- **Written by** the GLFW scroll callback (`add`), which may fire many times per poll.
- **Read by** the provider lambda (`value()`, `const`, non-consuming). Two reads in one tick,
  and sixteen reads across eight ticks of one `advance`, all return the same number.
- **Reset by** the frame loop: `on_frame` calls `wheel.reset()` **immediately before**
  `glfwPollEvents()`. That position is the whole point — it clears exactly the deltas the
  `advance` that just ran observed, and clears them before the poll that produces the next
  batch. Resetting after the poll would discard every event; resetting from the read would
  hand the second caller zero.

**One tick with two reads:** identical `wheel`, because nothing between them mutates the
accumulator. **One frame with eight ticks:** all eight see the same delta (wrinkle 1).
**One frame with zero ticks:** the delta is cleared unobserved (wrinkle 2).

### Two wrinkles. One deferred, one closed here (AMENDED)

1. **A delta can be observed by up to eight ticks of one `advance`.** A consumer that adds it
   to an offset each tick would scroll up to 8x. Unobservable here (nothing reads it) and not
   fixable here either: the correct answer depends on what "apply" means, which is U9d's
   decision. **Still deferred**, backlogged.
2. **A frame that runs zero ticks drops its delta.** Offered as deferred; **the coordinator
   ruled it into U9c.** The frequency decided it: at the default `fps == fixed_hz == 60`
   jitter alone produces zero-tick frames, and `--fps` above `--fixed-hz` makes them the
   majority, so the dropped fraction would be set by an unrelated CLI flag — and U9d carries
   the human's eye check, where "it stutters" would be indistinguishable from a bug in U9d's
   own arithmetic. **Closed:** `drop_observed(tick_count)` clears only once
   `RuntimeLoop::tick_count()` has moved, with no unconditional `reset()` left callable. Its
   price — unbounded accumulation across a tickless stretch — is bounded by saturating at
   `kMaxPendingWheel = 120.0f`, sign-preserving and clamping the sum, not discarding events. **That number is a chosen bound, not a measured one** — no wheel-rate measurement exists here and no human has scrolled against it; what would settle it is a real gesture on real hardware once U9d has a consumer, i.e. U9d's eye pass. The shape is argued; the value is replaceable.

---

## Shape

- `engine/ui/pointer.hpp`: `float wheel = 0.0f;` **last**, with a comment covering the
  default, the position, and the no-consume rule.
- `app/run/window_pointer.hpp`: `WheelAccumulator`; `window_pointer_state` takes a sixth
  parameter `wheel_delta` with **no default**, so the compiler — not a test — catches a call
  site that forgets it. Gated on presence, like `inside` and `down`: a scroll arriving while
  the cursor is off the window, or while the window is degenerate, reports `0`.
- `app/main.cpp`: a function-scoped `WheelAccumulator`, a `glfwSetScrollCallback` reaching it
  through a **file-scope `g_wheel`** beside the existing `g_stop`, and the tick-gated clear at
  the top of `on_frame`.

  **NOT through `glfwSetWindowUserPointer` (AMENDED — the first draft did, and it was a real
  bug).** `VulkanRenderDevice` owns that slot: it writes `this` there
  (`modules/render_vulkan/vulkan_render_device.cpp:364`) and its `framebuffer_resize_callback`
  casts it back and dereferences it with no null check (`:2785`). Clobbering it made the first
  window resize of `mosaic run --window` write a bool through a pointer to the wrong type.
  No gate in this repo can see that — `ctest` creates no `VkDevice`, the renderer gate runs the
  *editor* path, and a windowed smoke run under `timeout` never resizes — so it is now held by
  a textual pin on `app/main.cpp`. The backend half (the null check, and nothing declaring
  ownership of the slot) is a Rendering backlog entry.
- `studio/viewport.hpp`: `ViewportPointerSample::wheel` (last), and
  `pointer_state` gating it on `hovered`.
- `studio/studio_app.cpp`: `sample.wheel = ImGui::GetIO().MouseWheel;`.

## Non-goals

- **Multi-button pointer support.** Named beside the wheel in the source bullet; U9 does not
  need it. (Already in the backlog from the parent spec.)
- **Horizontal wheel (`xoffset`).** Both APIs offer it; no planned consumer wants it, and a
  second field with no reader is a second default to get wrong.
- **Any consumer of the delta**, `UiScroll` included — U9d.
- **Resolving wrinkle 1** (one delta observed by up to eight ticks of a single `advance`).
  Wrinkle 2 is no longer a non-goal — it was closed in this slice.
- **The backend half of the window-user-pointer hazard**: a null check in
  `framebuffer_resize_callback`, and a comment on the device constructor claiming the slot.
  Backlogged under Rendering; this slice touches no backend file.
- **Hoisting `read_repo_source`** into a shared textual-pin helper, so that `app/`-pinning
  cases need not live under `tests/unit/studio/`. Backlogged as a 🟢 row; several existing pins
  depend on its exact `normalize_ws(code_only(...))` behaviour.
