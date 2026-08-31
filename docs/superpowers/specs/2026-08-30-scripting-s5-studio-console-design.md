# Scripting S5 — Studio script console (design)

**Decomposition:** [scripting-decomposition](2026-08-30-scripting-decomposition.md) (slice S5 — the visible cap).
**Builds on:** S1 (`ScriptEngine`), S2 (`bind_world`/`get_component`/`set_component`), S3
(`bind_commands`/entity builtins/undo), S4 (`ScriptComponent`), the Studio shell (#37).
**Date:** 2026-08-30.

> Built semi-autonomously (user approved the scripting cadence at PR #77: build → PR each slice).

## Summary

A **Script Console** panel in the editor: type a Python script, press **Run**, and it executes against
the **live editor `World`** — creating/moving entities, editing components — with output and errors
shown inline; because the editor re-renders the world each frame, the viewport updates as the script
mutates it. This is the pillar's visible payoff, wiring S1–S3 into the editor. The console is a REPL:
one persistent engine per session, so `x = 5` in one run is visible in the next.

## Scope

**In:**
- **`ScriptConsoleModel`** (`studio/script_console.hpp`) — pure logic (no ImGui, testable headlessly):
  an injected engine factory, a lazily-created persistent `ScriptEngine`, and a `history` of entries
  (input echo / output / error). `run(const std::string& code, World&, const ComponentReflection&,
  CommandStack&)` binds the engine (once) to the world + reflection + command stack and executes
  `code`, appending results. The input *buffer* is a draw concern (a `char[]` in the ImGui glue), so
  the model stays purely execution + history — mirroring the Studio model/draw split (`PlayState`,
  `SceneFileModel`).
- **Enabling change:** the neutral `ScriptEngine::bind_world` takes `const ComponentReflection&`
  (its `component_value`/`set_value` are already `const`), so the editor's `const` reflection binds
  without a cast. Backward-compatible (non-const callers bind to `const&`).
- **StudioApp integration** — a `CommandStack` + a `ScriptConsoleModel` (built with an injected
  factory), a `draw_script_console()` ImGui panel (multiline input + Run + history), and a
  "Script Console" panel in the layout.
- **App wiring** — `app/run/editor.cpp` injects `create_python_script_engine` as the factory;
  `mosaic_app` links `mosaic_script_python`. Python enters at the app tier only.

**Deferred:**
- **Toggling Play from a script** — S3 deferred this to S5, but exposing `PlayState` (studio-tier) to
  a script needs a *neutral host-callback* mechanism the `ScriptEngine` interface doesn't have
  (registering an arbitrary C++ callback as a Python builtin). That is its own surface; v0.1 keeps
  Play a toolbar button. The console already scripts the world + commands (the substantive win).
- REPL niceties (history navigation, autocomplete, multi-line editing beyond ImGui's box), script
  files/`.py` loading, and a separate output pane styling. `on_update`-style live `ScriptComponent`
  editing from the console (S4 already ticks scripts).

## Architecture

### 1. `bind_world` const-correctness (`engine/script/script_engine.hpp` + backend)

```cpp
// was: virtual void bind_world(World&, ComponentReflection&) = 0;
virtual void bind_world(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& types) = 0;
```
The backend stores `const ComponentReflection* types_` and calls the (const) `component_value`/
`set_value`/`remove_value`. S4's `ScriptSystem` and every test pass a non-const reflection, which
binds to `const&` unchanged.

### 2. `ScriptConsoleModel` (`studio/script_console.hpp`) — Python-free logic

```cpp
namespace mosaic::studio
{
struct ConsoleEntry
{
    enum class Kind { Input, Output, Error };
    Kind kind;
    std::string text;
};

class ScriptConsoleModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptConsoleModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    const std::vector<ConsoleEntry>& history() const { return history_; }
    void clear_history() { history_.clear(); }

    // Execute `code` against `world` (reflection + command stack bound). Lazily creates the engine
    // on first run (persistent REPL). Appends an Input echo + Output/Error entries. Empty code is a
    // no-op. A null factory (scripting unavailable) appends one Error entry and no-ops thereafter.
    void run(const std::string& code, mosaic::engine::World& world,
             const mosaic::engine::reflection::ComponentReflection& reflection,
             mosaic::foundation::commands::CommandStack& commands);

private:
    EngineFactory factory_;
    std::unique_ptr<mosaic::engine::script::ScriptEngine> engine_; // lazily created, persistent
    std::vector<ConsoleEntry> history_;
};
}
```

`run` logic: empty `code` → no-op. If `!engine_`: if `!factory_` → push one `Error`
("scripting is not available in this build") and return; else `engine_ = factory_();`,
`engine_->bind_world(world, reflection);`, `engine_->bind_commands(commands);`. Push
`Input{code}`; `const auto r = engine_->run_string(code);`; if `!r.output.empty()` push
`Output{r.output}`; if `!r.ok` push `Error{r.error}`. Never throws (S1 guarantees `run_string`).
The engine is bound once to the (stable) editor world; subsequent runs reuse it → REPL state
persists.

### 3. StudioApp integration (`studio/studio_app.{hpp,cpp}`)

- Ctor gains an injected factory: `explicit StudioApp(std::string scene_path = "scene.json",
  ScriptConsoleModel::EngineFactory script_factory = {})`, storing
  `script_console_(std::move(script_factory))`.
- New members: `mosaic::foundation::commands::CommandStack commands_;`, `ScriptConsoleModel
  script_console_;`, and a draw-owned input buffer `char script_input_[8192] = {};` (no `imgui_stdlib`
  is vendored, so a fixed char buffer feeds `InputTextMultiline`).
- `draw_ui` already receives `world` + reflection; with §1 it can pass reflection (const) to the
  console. A `draw_script_console(world, reflection)` renders `ImGui::InputTextMultiline("##script",
  script_input_, sizeof(script_input_), ...)`, a **Run** button that calls
  `script_console_.run(script_input_, world, reflection, commands_)` then clears the buffer, a
  **Clear** button (`script_console_.clear_history()`), and the `history()` (color by kind: error
  red, output default, input dim). Add a "Script Console" panel to `StudioLayout`'s defaults and
  dispatch it in the panel loop (beside `draw_console()`).
- `draw_ui` gains access to `world`+reflection for the console (it already has them as params).

### 4. App wiring (`app/run/editor.cpp`, `app/CMakeLists.txt`)

```cpp
mosaic::studio::StudioApp studio(options.scene.value_or("scene.json"),
                                 [] { return mosaic::script::python::create_python_script_engine(); });
```
`app/CMakeLists.txt`: add `mosaic_script_python` to `mosaic_app`'s link libraries. `#include
"modules/script_python/python_script_engine.hpp"` in `editor.cpp`. Studio stays Python-free (holds a
`std::function`); the app is where the backend is chosen — same seam as everywhere else.

## Data flow

```
editor: StudioApp studio(scene, py_factory);  // factory injected
frame:  studio.draw_ui(device, world, reflection, assets);
console panel: input = "e = create_entity()\nt = get_component(e, 'Transform')..."
Run -> script_console_.run(world, reflection, commands_)
       -> lazily create engine, bind_world(world, reflection), bind_commands(commands_)
       -> run_string(input) -> ScriptResult -> history += Output/Error
next frame: hierarchy/viewport reflect the new entity (editor re-renders the World every frame)
```

## Error handling

- A syntax/runtime error → `run_string` returns `ok=false`; the console shows an `Error` entry with
  the traceback; never throws into the frame loop.
- A null/absent factory (a build without the Python backend) → a single clear `Error` entry; the
  editor stays fully functional otherwise.
- The engine binds to the editor's `World` once; the world is stable for the session (scene load
  repopulates the same object), so the binding stays valid. (If a future feature swaps the `World`
  instance, the console must re-bind — noted.)
- Console mutations go through the bound `CommandStack`, so `undo()`/`redo()` from the console (and a
  future editor Undo button) reverse them.

## Testability

- **`ScriptConsoleModel` unit** (`tests/unit/studio/script_console_test.cpp`; the unit-test exe
  already links `mosaic_studio` + `mosaic_script_python`):
  - `run("print(6*7)", ...)` → `history` has an `Output` entry containing `"42"` (+ an `Input` echo).
  - `run("e = create_entity()", ...)` → `world.entity_count()` grew; history records it.
  - **REPL persistence:** `run("x = 5", ...)` then `run("print(x)", ...)` → `Output` `"5"` (one
    engine reused across runs).
  - **Error:** `run("raise ValueError('boom')", ...)` → an `Error` entry containing `"boom"`,
    `ok`-safe (no throw); the model is still usable afterward.
  - **Commands:** `run` a `set_component` then `run("undo()", ...)` → the component reverts (stack
    bound).
  - **No factory:** `ScriptConsoleModel{}` (empty) `.run("print(1)", ...)` → one `Error` entry, no
    crash.
- **StudioApp** constructs with a factory (compile + link proves the wiring/tier boundary). The ImGui
  `draw_script_console` glue is thin and verified by the build + the standing functional-launch check
  (`mosaic run --window` → exit 0, silent stderr, confirming the render/UI path is intact) plus the
  user's visual acceptance in `mosaic editor` (type a script, watch the viewport). Per project memory,
  the sandbox cannot screenshot the GUI; the console's *logic* is fully covered by the model tests
  above, keeping the untested surface to thin ImGui calls.

## Ground rules honored

- **Tier wall:** `studio` uses only the neutral `ScriptEngine` + a `std::function` factory — no
  pybind11. The CPython backend is injected by the app, exactly like every other backend. `engine`
  stays Python-free; the `bind_world` const change is in the neutral interface.
- **Reflection-derived (#18) / #10 / #11:** the console reuses S2/S3 unchanged — generic
  get/set/entity/command surface; no new script API.
- No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as `uint64`.

## Acceptance

- [ ] `bind_world` takes `const ComponentReflection&`; full suite still green.
- [ ] `ScriptConsoleModel.run` executes against the live world (create/mutate), persists REPL state,
      surfaces output/errors, and is null-factory-safe — all unit-tested headlessly.
- [ ] StudioApp hosts a Script Console panel (input + Run + history) bound to the world + a
      `CommandStack`; the app injects the CPython factory; `mosaic_app` links the backend; build +
      full suite green; `mosaic run --window` smoke intact.
- [ ] The pillar is functionally complete: a user can script the running editor scene from the
      console and watch the viewport update (user visual acceptance).

## Depends on / relates

- **Depends on:** S1–S3 (the whole script surface), the Studio shell (#37), the editor frame loop.
  **Completes** the v0.1 Scripting pillar (and the v0.1 vertical slice: foundation → rendering →
  physics → tools → scripting).
- **Relates:** deferred Play-from-script (needs a neutral host-callback), an editor Undo button (the
  `CommandStack` seeded here), and the automation/agent API (same reflected surface).
