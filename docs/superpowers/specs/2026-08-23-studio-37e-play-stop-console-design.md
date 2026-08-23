# Studio 37e — Play/Stop + Console (design)

**Tracking issue:** [#37 Studio shell](https://github.com/Evol-Luci/mosaic/issues/37).
**Decomposition:** [studio-decomposition](2026-08-22-studio-decomposition.md) (slice 37e).
**Builds on:** 37a (editor shell), 37d (viewport + editor frame loop), #22 runtime loop,
#33 logging.
**Date:** 2026-08-23.

## Summary

The final v0.1 Studio slice: a toolbar **Play/Stop** (plus **Step**) that drives the
world's simulation through the `RuntimeLoop` (#22), and a **Console** panel that surfaces
#33 logging output live inside the editor. With this, `mosaic editor` is operable — you can
pause/resume the running scene and watch log output without leaving the window.

## Scope

**In:**
- A headless **`PlayState`** model (studio): is-playing state + play/stop/toggle and a
  one-shot step request.
- Editor wiring: each frame syncs `PlayState` to the `RuntimeLoop` (pause = `stop()`,
  resume = `start()`, step = one `RuntimeLoop::step()` while paused); Play/Stop/Step
  buttons in the menu bar.
- A **`ConsoleBuffer`** (studio): a thread-safe, capacity-capped ring buffer of captured
  log lines (owned copies).
- A **`ConsoleSink`** (studio): a `log::Sink` forwarding records into a `ConsoleBuffer`,
  registered on the process-global default logger for the editor's lifetime.
- The Console panel body renders the buffered lines.

**Deferred (post-v0.1 / YAGNI):** log level-filter and category-filter UI, search,
copy-to-clipboard, a clear button, rich per-level coloring, and reset-world-on-stop
(play/stop only pauses/resumes; it does not reset simulation state). A separate, unrelated
gap — **the app installs no logging sink at all, so `mosaic run` produces no output** — is
noted below as a follow-up, not folded into this slice.

## Background: logging is built but unwired

The #33 logging facility exists (`foundation/logging/log.hpp`: `Level`, `Record`, `Sink`,
`ConsoleSink`, `Logger`, and process-global `log::info/warn/error(...)` forwarding to
`default_logger()`), but **no code anywhere installs a sink**. `default_logger()` starts
with an empty sink list, so every `log::` call — `StatusReporter` progress/summary, Vulkan
validation warnings/errors — is currently dropped. Verified: `mosaic run --ticks 3` emits
nothing on stdout/stderr despite the text reporter logging a summary.

Two consequences:
1. **The editor's `ConsoleSink` is the first real logging destination** — registering it
   makes StatusReporter output and Vulkan validation visible in the Console panel.
2. A "silent stderr" functional check does **not** prove log-worthy errors were absent
   (they would be dropped, not printed). This slice therefore relies on **unit tests** for
   the capture path, not on stderr observation.

## Architecture

### `PlayState` (studio tier — headless)

```cpp
// The editor's simulation run-state. The toolbar mutates it; the editor frame loop
// reads it each frame to drive the RuntimeLoop. Default is playing (the viewport is
// live on launch, matching 37d).
class PlayState
{
public:
    bool is_playing() const;
    void play();           // idempotent
    void stop();           // idempotent (pause; does not reset the world)
    void toggle();

    void request_step();          // queue a single tick (honored while paused)
    bool consume_step_request();  // returns true once if a step was queued, clearing it
};
```

`stop()` is a **pause**: it does not reset entities/components. Resuming with `play()`
continues from the current state. Because `RuntimeLoop::advance` returns early while
stopped *before* banking `real_dt`, no time accumulates during a pause and resuming
triggers no catch-up burst.

### Editor frame integration (`app/run/editor.cpp`)

At the top of the existing `on_frame` lambda (before `begin_frame`, so any tick lands
before the render, consistent with `FramePump`'s `advance()` → `on_frame` order):

```cpp
PlayState& play = studio.play_state();
if (play.is_playing()) { loop.start(); } else { loop.stop(); }
if (play.consume_step_request()) { loop.step(world); }
```

`FramePump::run` already calls `loop.start()` on entry and `loop.advance()` each frame;
this block lets the toolbar override the running state mid-run without ending the pump.
Default `PlayState` is playing, so the entry `start()` and the model agree — no startup
one-tick leak.

### `ConsoleBuffer` (studio tier — headless, thread-safe)

```cpp
struct ConsoleLine
{
    mosaic::foundation::log::Level level;
    std::string                    category;
    std::string                    message;
};

// A bounded ring buffer of captured log lines. push() is called from the logger's
// dispatch (possibly on non-UI threads: Vulkan validation, jobs); snapshot() is read
// from the UI thread. Both take an internal mutex. Capacity-capped: the oldest line is
// dropped when full (kCapacity, default 1000).
class ConsoleBuffer
{
public:
    explicit ConsoleBuffer(std::size_t capacity = 1000);
    void push(const mosaic::foundation::log::Record& record); // copies the string_views
    std::vector<ConsoleLine> snapshot() const;                // ordered oldest -> newest
    std::size_t size() const;
    void clear();
};
```

`push` **must copy** `record.category` / `record.message` — they are `string_view`s valid
only for the duration of the sink's `write` call.

### `ConsoleSink` (studio tier)

```cpp
// A log::Sink that forwards records into a shared ConsoleBuffer. Held by the
// process-global default logger for the editor's lifetime. write() is noexcept and
// never calls back into the logger (honoring the Sink contract).
class ConsoleSink : public mosaic::foundation::log::Sink
{
public:
    explicit ConsoleSink(std::shared_ptr<ConsoleBuffer> buffer);
    void write(const mosaic::foundation::log::Record& record) noexcept override;
};
```

### Registration & lifetime (`app/run/editor.cpp`)

- `StudioApp` owns `std::shared_ptr<ConsoleBuffer> console_` (constructed in its ctor).
- At editor start: `log::add_sink(std::make_shared<ConsoleSink>(studio.console()));`
- At editor exit (before `run_editor` returns): `log::clear_sinks();` so the global logger
  never references the buffer after the studio/buffer is gone. (The buffer is a
  `shared_ptr`, so even out-of-order teardown is memory-safe; `clear_sinks` keeps the
  global logger clean between runs.)

### Studio panels (`studio/studio_app.{hpp,cpp}`, `studio/studio_layout.cpp`)

- `StudioApp` gains `PlayState play_` and `std::shared_ptr<ConsoleBuffer> console_`, with
  `play_state()` / `console()` accessors.
- The menu bar (already drawn) gains **Play/Stop** (a single toggle button showing the
  opposite action) and **Step** (enabled only while paused) after the `File` menu.
- The `"Console"` panel (already registered in the layout) gets a real body:
  iterate `console_->snapshot()`, render each line as `"[LEVEL] category: message"`, and
  auto-scroll to the bottom when already at the bottom.

## Testability boundary

Consistent with every render/studio slice, and the standing note that the sandbox cannot
screenshot the GUI. Given the unwired-logging finding, the capture path is covered by unit
tests rather than stderr observation.

- **Unit-tested (no window):**
  - `PlayState`: default is playing; `toggle` flips; `play`/`stop` idempotent; `stop` then
    `play` returns to playing; `request_step` then `consume_step_request` returns true once
    then false.
  - `ConsoleBuffer` / `ConsoleSink`: logging through a real `log::Logger` with a
    `ConsoleSink` captures the record (level/category/message) as **owned copies** that
    outlive the transient `string_view`s; the capacity cap drops the oldest line; ordering
    is oldest→newest; `clear` empties it.
- **Verified functionally:** `mosaic editor --ticks N` launches, exits 0; the toolbar
  Play/Stop/Step and the Console panel render. Not screenshotted.

## Ground rules honored

- **Tier contract:** `PlayState`, `ConsoleBuffer`, `ConsoleSink` live in `studio/`, which
  already depends on `engine/` (for `RuntimeLoop` is engine/runtime) and `foundation/` (for
  `log::Sink`). `foundation/`/`engine/` gain nothing and never depend on `studio/`.
- **`Sink` contract:** `ConsoleSink::write` is `noexcept` and never re-enters the logger.
- **No new third-party dependencies.**
- **Catch2 test names must not start with `--`.**

## Acceptance

- [ ] `PlayState` unit tests green (default playing, toggle, idempotent play/stop, step
      request set/consume).
- [ ] `ConsoleBuffer`/`ConsoleSink` unit tests green (capture via a real Logger, owned
      copies, capacity cap, ordering, clear).
- [ ] `mosaic editor` shows a Play/Stop (+ Step) toolbar that pauses/resumes the spinning
      cube in the viewport, and a Console panel showing captured log lines; launches, exits
      0.
- [ ] Full unit suite green.

## Follow-up (out of scope, flagged separately)

**The app installs no logging sink**, so `mosaic run` (and headless runs) produce no output
even though `StatusReporter` logs through `log::`. This is a pre-existing gap unrelated to
the Studio console; a small follow-up should install a `ConsoleSink` (to stderr) in the CLI
entry path so non-editor runs are observable — which would also restore the render-slice
"validation → stderr" verification assumption.

## Depends on / relates

- **Depends on:** #22 `RuntimeLoop` (start/stop/step), #33 logging (`Sink`/`Record`/
  `Logger`), 37d (the editor frame loop this extends).
- **Relates:** completes the v0.1 Studio (#37) lean-core scope; the deferred asset browser
  and Blender live-link remain post-v0.1.
