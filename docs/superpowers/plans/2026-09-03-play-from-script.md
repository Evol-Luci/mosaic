# Play-from-script (neutral host-callback) — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD, commit after each task.

**Goal:** A script can call `stop()` / `pause()` / `play()` to drive the host's
run-state via a neutral `ScriptHost` callback; the runtime wires it so a
headless script can end the run.

**Architecture:** Neutral `ScriptHost` (std::function callbacks) bound into each
engine (mirrors `bind_world`/`bind_commands`); Python backend exposes three
globals; `ScriptSystem` binds the host; `setup_runtime`/`main.cpp` supply it.

**Tech Stack:** C++20, Catch2, pybind11 (`pybind11/functional.h` already included).

**Spec:** `docs/superpowers/specs/2026-09-03-play-from-script-design.md`

## Global Constraints

- Tier wall: `foundation/` → `engine/` → `modules/` → `app/`. `ScriptHost` is
  pure `<functional>`, lives in `engine/script/`; no backend/studio type crosses
  the `ScriptEngine` interface.
- All new ctor/function params are **defaulted** so existing callers keep compiling.
- `bind_host` is **non-pure** (default no-op) — do not break other impls.
- Catch2 `TEST_CASE` names must not start with `--`.
- No new dependencies.

---

### Task 1: Neutral `ScriptHost` + `bind_host` seam + Python backend + backend test

**Files:**
- Create: `engine/script/script_host.hpp`
- Modify: `engine/script/script_engine.hpp` (include + non-pure `bind_host`)
- Modify: `modules/script_python/python_script_engine.cpp` (override + `host_` member)
- Create: `tests/unit/script/script_host_test.cpp`
- Modify: `tests/CMakeLists.txt` (add after `unit/script/script_hotreload_test.cpp` ~line 108)

**Interfaces produced:**
- `mosaic::engine::script::ScriptHost { std::function<void(bool)> set_playing; std::function<void()> request_stop; }`
- `ScriptEngine::bind_host(const ScriptHost&)` — installs `play`/`pause`/`stop` globals.

- [ ] **Step 1: Write `engine/script/script_host.hpp`:**

```cpp
#pragma once

#include <functional>

namespace mosaic::engine::script
{
// Host hooks a script may call to drive the run-state. Callbacks are optional:
// an unset (empty) function is a silent no-op. Supplied by the host
// (runtime/editor); ScriptSystem binds them into every engine.
struct ScriptHost
{
    std::function<void(bool)> set_playing;  // play(true) / pause(false)
    std::function<void()>     request_stop; // end the run
};
} // namespace mosaic::engine::script
```

- [ ] **Step 2: Modify `engine/script/script_engine.hpp`** — add the include near
  the other engine includes and the method after `bind_commands`:

```cpp
#include "engine/script/script_host.hpp"
```
```cpp
    // Bind host run-state callbacks (play/pause/stop). Optional; default no-op so
    // engines that do not support it (or are built before a host exists) still work.
    virtual void bind_host(const ScriptHost& host) { (void)host; }
```

- [ ] **Step 3: Write the failing backend test** `tests/unit/script/script_host_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptHost;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_host exposes play/pause/stop to scripts", "[script]")
{
    int  playing_calls = 0;
    bool last_playing  = false;
    int  stop_calls    = 0;

    ScriptHost host;
    host.set_playing  = [&](bool p) { ++playing_calls; last_playing = p; };
    host.request_stop = [&] { ++stop_calls; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    REQUIRE(engine->run_string("pause()").ok);
    REQUIRE(playing_calls == 1);
    REQUIRE(last_playing == false);

    REQUIRE(engine->run_string("play()").ok);
    REQUIRE(playing_calls == 2);
    REQUIRE(last_playing == true);

    REQUIRE(engine->run_string("stop()").ok);
    REQUIRE(stop_calls == 1);
}

TEST_CASE("bind_host with empty callbacks is a safe no-op", "[script]")
{
    auto engine = create_python_script_engine();
    engine->bind_host(ScriptHost{}); // both callbacks empty
    REQUIRE(engine->run_string("stop()\npause()\nplay()").ok); // no crash, no throw
}
```

Add to `tests/CMakeLists.txt` after the `unit/script/script_hotreload_test.cpp` line:
```
  unit/script/script_host_test.cpp
```

- [ ] **Step 4: Run to verify FAIL** — build, then
  `ctest --test-dir build/debug -R "bind_host"`. Expected: FAIL (`play`/`pause`/`stop`
  undefined → `run_string` returns ok=false → `REQUIRE(...ok)` fails), because
  the Python backend does not yet override `bind_host`.

- [ ] **Step 5: Implement in `modules/script_python/python_script_engine.cpp`.**
  Add the include:
```cpp
#include "engine/script/script_host.hpp"
```
  Add the override (place after `bind_commands`):
```cpp
    void bind_host(const mosaic::engine::script::ScriptHost& host) override
    {
        host_ = host;
        globals_["play"] = py::cpp_function(
            [this] { if (host_.set_playing) { host_.set_playing(true); } });
        globals_["pause"] = py::cpp_function(
            [this] { if (host_.set_playing) { host_.set_playing(false); } });
        globals_["stop"] = py::cpp_function(
            [this] { if (host_.request_stop) { host_.request_stop(); } });
    }
```
  Add the member (near `stack_`):
```cpp
    mosaic::engine::script::ScriptHost host_;
```

- [ ] **Step 6: Run to verify PASS** — build, `ctest --test-dir build/debug -R "bind_host"` → both pass.

- [ ] **Step 7: Commit** `git add engine/script/script_host.hpp engine/script/script_engine.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_host_test.cpp tests/CMakeLists.txt && git commit -m "feat(script): neutral ScriptHost + bind_host exposes play/pause/stop to scripts"`

---

### Task 2: `ScriptSystem` binds the host into each engine

**Files:**
- Modify: `engine/script/script_system.hpp` (ctor param + member + bind call)
- Modify: `tests/unit/script/script_system_test.cpp` (add integration test)

**Interfaces consumed:** Task 1's `ScriptHost` / `bind_host`.
**Interfaces produced:** `ScriptSystem(const ComponentReflection&, EngineFactory, ScriptHost host = {})`.

- [ ] **Step 1: Write the failing integration test** in
  `tests/unit/script/script_system_test.cpp` (add a new TEST_CASE; include
  `engine/script/script_host.hpp` if not already):

```cpp
TEST_CASE("ScriptSystem binds the host so a script can call stop()", "[script]")
{
    using mosaic::engine::script::ScriptHost;
    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    bool stopped = false;
    ScriptHost host;
    host.request_stop = [&] { stopped = true; };

    mosaic::engine::World world;
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); }, host);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::script::ScriptComponent>(
        e, mosaic::engine::script::ScriptComponent{"def on_update(dt):\n    stop()\n"});

    world.tick(0.016f);
    REQUIRE(stopped);
}
```

> NOTE: match the file's existing includes/usings (it already constructs
> `ScriptSystem` + a python factory). Reuse them rather than duplicating.

- [ ] **Step 2: Run to verify FAIL** — build (compile error: ctor takes 2 args)
  then it should fail. `ctest --test-dir build/debug -R "binds the host"`.

- [ ] **Step 3: Implement in `engine/script/script_system.hpp`:**
  - Add include: `#include "engine/script/script_host.hpp"`.
  - Change ctor to:
```cpp
    ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory,
                 ScriptHost host = {})
        : types_(types), factory_(std::move(factory)), host_(std::move(host))
    {
    }
```
  - Add member (near `factory_`): `ScriptHost host_;`
  - After the existing `entry.engine->bind_world(world, types_);` line, add:
```cpp
                    entry.engine->bind_host(host_);
```

- [ ] **Step 4: Run to verify PASS** — `ctest --test-dir build/debug -R "binds the host"` → pass.

- [ ] **Step 5: Full suite** `ctest --test-dir build/debug` → all pass.

- [ ] **Step 6: Commit** `git add engine/script/script_system.hpp tests/unit/script/script_system_test.cpp && git commit -m "feat(script): ScriptSystem binds ScriptHost into each engine"`

---

### Task 3: Runtime wiring (`setup_runtime` + `main.cpp`) + headless run test

**Files:**
- Modify: `app/run/demo_scene.hpp` (`setup_runtime` gains `ScriptHost host = {}`)
- Modify: `app/main.cpp` (build a host bound to `g_stop`, pass to `setup_runtime`)
- Create: `tests/unit/app/play_from_script_test.cpp`
- Modify: `tests/CMakeLists.txt` (add after `unit/app/headless_run_test.cpp`)

- [ ] **Step 1: Write the failing runtime integration test**
  `tests/unit/app/play_from_script_test.cpp` (mirror `headless_run_test.cpp`'s
  harness — StepClock, NullReporter, PumpDeps whose `stop_requested` reads a
  local flag the host sets):

```cpp
#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using namespace mosaic::app;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::script::ScriptHost;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace
{
class NullReporter : public StatusReporter
{
public:
    void progress(std::uint64_t, double) override {}
    void summary(const RunSummary&) override {}
    void error(const std::string&) override {}
};
struct StepClock { double t=0.0, step=1.0; double operator()(){ double n=t; t+=step; return n; } };
} // namespace

TEST_CASE("a script calling stop() ends a headless run early", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);

    World world;
    world.add_system<TransformSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(
        e, ScriptComponent{"def on_update(dt):\n    stop()\n"});

    bool stop_flag = false;
    ScriptHost host;
    host.request_stop = [&] { stop_flag = true; };

    AssetRegistry registry;
    setup_runtime(world, registry, types,
                  [] { return create_python_script_engine(); }, host);

    RuntimeLoop loop(1.0F / 60.0F, 1);
    Options options; options.ticks = 100; options.fixed_hz = 60.0;
    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [&] { return stop_flag; }};
    NullReporter reporter;

    FramePump pump(loop, deps, reporter, options);
    const RunSummary summary = pump.run(world);

    REQUIRE(stop_flag);                       // the script drove the host callback
    REQUIRE(summary.stopped_by == "signal");  // and it ended the run
    REQUIRE(loop.tick_count() < 100);         // well before the 100-tick bound
}
```

Add to `tests/CMakeLists.txt` after `unit/app/headless_run_test.cpp`:
```
  unit/app/play_from_script_test.cpp
```

- [ ] **Step 2: Run to verify FAIL** — build (compile error: `setup_runtime`
  takes 4 args). Expected fail.

- [ ] **Step 3: Implement `setup_runtime` change in `app/run/demo_scene.hpp`:**
  add the parameter and forward it:
```cpp
inline RuntimeScene setup_runtime(
    mosaic::engine::World&                                  world,
    mosaic::engine::assets::AssetRegistry&                  registry,
    const mosaic::engine::reflection::ComponentReflection&  reflection,
    mosaic::engine::script::ScriptSystem::EngineFactory     script_factory,
    mosaic::engine::script::ScriptHost                      host = {})
{
    ...
    world.add_system<mosaic::engine::script::ScriptSystem>(
        reflection, std::move(script_factory), std::move(host));
    ...
}
```
  Add `#include "engine/script/script_host.hpp"` to `demo_scene.hpp` if not
  transitively present.

- [ ] **Step 4: Implement `app/main.cpp` wiring.** Where `setup_runtime` is
  called (hoisted above the window block), build and pass a host:
```cpp
    mosaic::engine::script::ScriptHost script_host;
    script_host.request_stop = [] { g_stop.store(true); };
    script_host.set_playing  = [](bool playing) { if (!playing) { g_stop.store(true); } };

    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      script_host)
            .render_system;
```
  Add `#include "engine/script/script_host.hpp"` to `main.cpp` if needed.

- [ ] **Step 5: Run to verify PASS** — `ctest --test-dir build/debug -R "ends a headless run early"` → pass.

- [ ] **Step 6: Full suite** `ctest --test-dir build/debug` → all pass.

- [ ] **Step 7: Manual acceptance.** Build; find the binary; a demo script that
  calls `stop()` isn't in the default scene, so this is covered by the test.
  Sanity: `<bin> run --ticks 3` still exits 0 (host wiring didn't regress the
  normal path).

- [ ] **Step 8: Commit** `git add app/run/demo_scene.hpp app/main.cpp tests/unit/app/play_from_script_test.cpp tests/CMakeLists.txt && git commit -m "feat(app): wire ScriptHost so a script can stop/pause a headless run"`

---

### Task 4: next-steps doc update (rides in this branch)

**Files:** Modify `docs/project/next-steps.md`.

- [ ] In the **Scripting** bullet, mark **Play-from-script — DONE** (neutral
  `ScriptHost` host-callback; scripts call `stop()`/`pause()`/`play()`; runtime
  wired to `g_stop`; link this spec/plan). Note the **editor `PlayState`-from-script**
  wiring as the thin remaining follow-up. Leave `.py`-as-asset and the worker
  thread as the remaining scripting items. Commit:
  `docs: mark play-from-script done in next-steps`.
