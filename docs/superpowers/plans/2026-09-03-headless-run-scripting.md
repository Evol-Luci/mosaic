# Headless `mosaic run` Scripting — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD, commit after each task.

**Goal:** `mosaic run <scene>` without `--window` ticks `ScriptSystem` +
`PhysicsSystem` (same simulation as windowed, minus rendering).

**Architecture:** Hoist `setup_runtime` (and project-asset load) out of the
`if (options.window)` block in `app/main.cpp` so both modes wire the simulation
systems. `setup_runtime` is already device-free.

**Tech Stack:** C++20, Catch2, existing `FramePump`/`RuntimeLoop`, CPython script backend.

**Spec:** `docs/superpowers/specs/2026-09-03-headless-run-scripting-design.md`

## Global Constraints

- Tier wall: `foundation/` → `engine/` → `modules/` → `app/`. `main.cpp` is app-tier; may use all lower tiers.
- Windowed-run behaviour must not change.
- Catch2 `TEST_CASE` names must not start with `--`.
- No new dependencies.

---

### Task 1: Headless-run integration test (failing first)

**Files:**
- Create: `tests/unit/app/headless_run_test.cpp`
- Modify: `tests/CMakeLists.txt` (add under the `unit/app/` block, near `script_runtime_test.cpp` ~line 61)

**Interfaces consumed:** `mosaic::app::setup_runtime` (from `app/run/demo_scene.hpp`),
`mosaic::app::FramePump` / `PumpDeps` / `RunSummary` (from `app/run/frame_pump.hpp`),
`mosaic::engine::runtime::RuntimeLoop`, `create_python_script_engine`.

- [ ] **Step 1: Write the test** `tests/unit/app/headless_run_test.cpp`:

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
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::FramePump;
using mosaic::app::Options;
using mosaic::app::PumpDeps;
using mosaic::app::RunSummary;
using mosaic::app::setup_runtime;
using mosaic::app::StatusReporter;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::script::ScriptComponent;
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

struct StepClock
{
    double t = 0.0;
    double step = 1.0;
    double operator()() { double now = t; t += step; return now; }
};
} // namespace

TEST_CASE("a headless run (no window) ticks scripts through the frame pump", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);

    World world;
    world.add_system<TransformSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"});

    AssetRegistry registry;
    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    RuntimeLoop loop(1.0F / 60.0F, 1); // one tick per advance -> exact tick count
    Options options; options.ticks = 4; options.fixed_hz = 60.0;
    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};
    NullReporter reporter;

    FramePump pump(loop, deps, reporter, options);
    const RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "ticks");
    REQUIRE(loop.tick_count() == 4);
    // 4 ticks at fixed dt = 1/60 -> position.x advanced by 4/60.
    REQUIRE(world.get_component<Transform>(e)->position.x > 0.0f);
}
```

Add to `tests/CMakeLists.txt` after the `unit/app/script_runtime_test.cpp` line:
```
  unit/app/headless_run_test.cpp
```

> NOTE: verify the exact `TransformSystem` header path (`engine/transform/transform_system.hpp`)
> and `RunSummary::stopped_by` field name against the tree (frame_pump_test.cpp
> uses both). If `PumpDeps` aggregate-init differs, mirror frame_pump_test.cpp exactly.

- [ ] **Step 2: Run to verify it FAILS as a real assertion, not a compile error.**
  Build first: `cmake --build build/debug -j`, then
  `ctest --test-dir build/debug -R "headless run"`.
  Expected: **FAILS** on `position.x > 0.0f` (currently the test itself calls
  `setup_runtime`, so it would PASS — see note). If it passes, that only proves
  the pipeline; the real regression guard is Task 2's main.cpp change. Keep this
  test; it locks the headless composition.

> IMPORTANT: this test calls `setup_runtime` directly, so it passes even before
> the main.cpp fix — it guards the *composition* (pump + scripts headless). The
> main.cpp fix in Task 2 is what makes the actual CLI honour it; verify that
> manually per Task 2 Step 4.

- [ ] **Step 3: Commit** `git add tests/unit/app/headless_run_test.cpp tests/CMakeLists.txt && git commit -m "test(app): headless frame-pump run ticks scripts"`

---

### Task 2: Hoist `setup_runtime` out of the window block in `main.cpp`

**Files:**
- Modify: `app/main.cpp` (the `if (options.window)` block, ~lines 158-221)

**Current shape (for reference):**
- `setup_runtime(...).render_system` is called at ~line 189, INSIDE the window block.
- `load_project_assets(...)` is at ~line 194, also inside.
- `deps.on_frame` (window render) and `deps.stop_requested` are set per-branch.

- [ ] **Step 1:** Move the `setup_runtime` call and the `load_project_assets`
  block to BEFORE the `if (options.window)` block (right after
  `populate_world` + the `RuntimeLoop`/`PumpDeps` setup, before
  `GLFWwindow* glfw_window = nullptr;` or just after `asset_registry` is
  declared — it must be declared first). Result:

```cpp
    namespace render = mosaic::engine::render;

    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); })
            .render_system;

    if (options.assets_dir)
    {
        const int n = load_project_assets(asset_registry, *options.assets_dir);
        if (n > 0) { mosaic::foundation::log::info("import", "loaded {} project asset(s)", n); }
    }

    if (options.window)
    {
        // glfw/vulkan init (unchanged) ...
        auto* device = render_device.get();
        deps.on_frame = [glfw_window, device, &world, render_system, &asset_registry] {
            // unchanged render body
        };
        deps.stop_requested = [glfw_window] {
            return g_stop.load() || glfwWindowShouldClose(glfw_window) == GLFW_TRUE;
        };
    }
    else
    {
        deps.stop_requested = [] { return g_stop.load(); };
    }
```

  Remove the now-duplicated `setup_runtime` + `load_project_assets` lines from
  inside the window block. Keep the `namespace render`/`transform` aliases
  wherever they are still needed (the render alias is now used above).

> NOTE: `asset_registry` is declared at ~line 156 (before the window block) —
> good, `setup_runtime` writes into it there. Ensure the moved code sits AFTER
> that declaration. The `render_system` shared_ptr is unused in headless mode;
> that is intentional and warning-free (it is assigned).

- [ ] **Step 2: Build** `cmake --build build/debug -j` → clean, no unused-variable warning that breaks `-Werror` (check the build config; if `-Werror` is on and `render_system` warns, mark it `[[maybe_unused]]` — but it is assigned, so it should not warn).

- [ ] **Step 3: Full suite** `ctest --test-dir build/debug` → all pass (previous total + 1 new).

- [ ] **Step 4: Manual headless acceptance.** Build a tiny scene with a logging
  script and run the real binary headless:
  - Find the binary: `find build/debug -name mosaic -type f -executable`.
  - `<bin> run --ticks 3` (the default demo scene has the orbiting scripted
    object). Confirm exit 0 and that the process ran 3 ticks (status line
    `run: done: 3 ticks`). Because the demo's orbit script mutates a Transform,
    a windowed run would show motion; headless proves no crash + scripts wired.
    Optionally add `print(...)` to a script to see per-tick stdout.

- [ ] **Step 5: Commit** `git add app/main.cpp && git commit -m "feat(app): run scripts + physics in headless mosaic run (hoist setup_runtime out of the window block)"`

---

### Task 3: next-steps doc update (rides in this branch)

**Files:** Modify `docs/project/next-steps.md`.

- [ ] In the **Scripting** bullet, mark **scripting in headless `mosaic run` — DONE**
  (link this spec/plan), leaving Play-from-script, `.py`-as-asset, and the
  worker thread as the remaining scripting items. Commit:
  `docs: mark headless-run scripting done in next-steps`.
