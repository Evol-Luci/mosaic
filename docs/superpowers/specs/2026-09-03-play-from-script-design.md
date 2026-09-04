# Play-from-script (neutral host-callback) — Design

## Problem

Scripts can read/write components and drive entity lifecycle, but they cannot
influence the **run-state** — a script has no way to end (or pause) the
simulation. There is no neutral seam by which host-controlled loop state
(`RuntimeLoop` stop, the editor's `PlayState`) is reachable from script code.

## Goal

A script can call **`stop()`** / **`pause()`** / **`play()`** to drive the
host's run-state, through a neutral callback the host supplies — no
backend-specific or studio-specific type crosses the `ScriptEngine` interface.
For the runtime (`mosaic run`), `stop()`/`pause()` end the run; this is fully
headless-verifiable. (Editor `PlayState` wiring is a thin follow-up — see Out
of scope.)

## Approach

Mirror the existing script-binding seam (`bind_world`, `bind_commands`): a
neutral `ScriptHost` of `std::function` callbacks, bound into each engine, and
exposed by the Python backend as global functions — the same
`globals_["name"] = py::cpp_function(...)` pattern already used for
`get_component` etc.

### 1. Neutral host struct — `engine/script/script_host.hpp` (new)

```cpp
#pragma once
#include <functional>
namespace mosaic::engine::script
{
// Host hooks a script may call to drive the run-state. Callbacks are optional:
// an unset (empty) function is a silent no-op. The host (runtime/editor)
// supplies these; ScriptSystem binds them into every engine.
struct ScriptHost
{
    std::function<void(bool)> set_playing;  // play(true) / pause(false)
    std::function<void()>     request_stop; // end the run
};
} // namespace mosaic::engine::script
```

### 2. `ScriptEngine` interface — `engine/script/script_engine.hpp`

Add an **optional, non-pure** method (default no-op, so no existing/other
implementation breaks):

```cpp
#include "engine/script/script_host.hpp"
...
// Bind host run-state callbacks (play/pause/stop). Optional; default no-op.
virtual void bind_host(const ScriptHost& host) { (void)host; }
```

### 3. Python backend — `modules/script_python/python_script_engine.cpp`

Store `ScriptHost host_;` and override `bind_host` to expose three globals
(guarding each callback for emptiness):

```cpp
void bind_host(const ScriptHost& host) override
{
    host_ = host;
    globals_["play"]  = py::cpp_function([this]{ if (host_.set_playing) host_.set_playing(true); });
    globals_["pause"] = py::cpp_function([this]{ if (host_.set_playing) host_.set_playing(false); });
    globals_["stop"]  = py::cpp_function([this]{ if (host_.request_stop) host_.request_stop(); });
}
```

### 4. `ScriptSystem` — `engine/script/script_system.hpp`

- Add a `ScriptHost host_;` member and a **defaulted** ctor param:
  `ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory, ScriptHost host = {})`.
- After each engine is created and `bind_world`'d, call
  `entry.engine->bind_host(host_);` (right after the existing `bind_world` line,
  before the first `run_string`).

### 5. `setup_runtime` — `app/run/demo_scene.hpp`

Add a defaulted `ScriptHost host = {}` param and forward it:
`world.add_system<ScriptSystem>(reflection, std::move(script_factory), host)`.

### 6. Runtime wiring — `app/main.cpp`

Build a host bound to the run's stop flag and pass it to `setup_runtime` (both
branches share the same hoisted call from the headless-run slice):

```cpp
mosaic::engine::script::ScriptHost host;
host.request_stop = [] { g_stop.store(true); };
host.set_playing  = [](bool playing) { if (!playing) { g_stop.store(true); } }; // headless: pause == stop
auto render_system = setup_runtime(world, asset_registry, types, factory, host).render_system;
```

Now a headless script that calls `stop()` (or `pause()`) ends the run.

## Data flow

`script calls stop()` → Python global → `host_.request_stop()` → `g_stop=true`
→ `deps.stop_requested()` true → `FramePump` ends with `stopped_by == "signal"`.

## Testing (headless)

1. **Backend seam** — `tests/unit/script/script_host_test.cpp` (new): create a
   Python engine, `bind_host` with capturing callbacks, `run_string` each of
   `play()` / `pause()` / `stop()`, assert the right callback fired with the
   right arg. Also assert calling them **before** `bind_host` (unbound globals)
   is a clean `NameError`-free no-op path is *not* required — they only exist
   after `bind_host`.
2. **System integration** — extend `ScriptSystem` coverage: an entity whose
   `on_update` calls `stop()`; after one `world.tick`, the capturing host's
   flag is set (proves `ScriptSystem` binds the host into the per-entity
   engine).
3. **Runtime integration** — a headless `FramePump` run (mirror
   `headless_run_test.cpp`) where the scripted entity calls `stop()` on its
   first `on_update`; assert the run ends **before** the `ticks` bound with
   `summary.stopped_by == "signal"`.

All three are headless, no GUI.

## Out of scope (YAGNI / follow-ups)

- **Editor `PlayState`-from-script**: `editor.cpp` has `studio.play_state()`
  near its loop; wiring a host whose `set_playing`/`request_stop` drive
  `PlayState` is a thin follow-up (not independently headless-testable, so kept
  out of this slice). The neutral seam built here is exactly what it will use.
- Resume/step control from script; per-entity host scoping.
