# Runtime Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the runtime loop (#22) — a deterministic fixed-timestep accumulator that drives `World::tick(fixed_dt)` from externally supplied elapsed time, with start/stop/single-step controls.

**Architecture:** A header-only `RuntimeLoop` in `engine/runtime`. `advance(world, real_dt)` banks real time and dispatches whole fixed ticks (capped by a spiral-of-death guard); `step` runs one tick manually; `start`/`stop` pause/resume. No wall-clock inside — the real `std::chrono` pump is the app's job.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-runtime-loop-design.md](../specs/2026-08-16-runtime-loop-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `engine/runtime` depends only on `engine/world`. Header-only.
- **Pacing only:** the per-tick order (drain events → run systems → commit destructions) stays in `World::tick`; the loop only decides *how many* ticks and passes `fixed_dt`.
- **Deterministic:** identical `(fixed_dt, real_dt schedule)` ⇒ identical tick count; no wall-clock reads inside the loop.
- **Namespace:** `mosaic::engine::runtime`.
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` methods, `PascalCase` types, trailing-underscore private members.
- **Testing:** Catch2, unit tests under `tests/unit/runtime/`, tag `[runtime]`; register each new `.cpp` in `tests/CMakeLists.txt`. Float compares use exactly-representable values (e.g. `0.25f`) or `Catch::Approx`. TDD — write the failing test first.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[runtime]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/practical-williams-bde237`).

## File Structure

| File | Responsibility |
|------|----------------|
| `engine/runtime/runtime_loop.hpp` (create) | `RuntimeLoop` — fixed-timestep accumulator over `World::tick`. |
| `tests/unit/runtime/runtime_loop_test.cpp` (create) | Task 1 core-behavior tests. |
| `tests/unit/runtime/runtime_stability_test.cpp` (create) | Task 2 populated-World stability test. |
| `tests/CMakeLists.txt` (modify) | Register the two new test sources. |

No `CMakeLists.txt` library changes: header-only; tests link existing libs.

---

### Task 1: `RuntimeLoop` core

**Files:**
- Create: `engine/runtime/runtime_loop.hpp`
- Test: `tests/unit/runtime/runtime_loop_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::World` (`tick`), `ecs::System` (in the test).
- Produces (`mosaic::engine::runtime::RuntimeLoop`):
  - `explicit RuntimeLoop(float fixed_dt, int max_steps_per_advance = 8);`
  - `void start(); void stop(); bool is_running() const;`
  - `int advance(World& world, float real_dt);`
  - `int step(World& world);`
  - `float fixed_dt() const; float accumulator() const; std::uint64_t tick_count() const;`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/runtime/runtime_loop_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;

namespace
{
// Records how many times it was updated and the last dt it received.
class TickCounter : public mosaic::engine::ecs::System
{
public:
    int updates = 0;
    float last_dt = -1.0f;
    void update(World&, float dt) override
    {
        ++updates;
        last_dt = dt;
    }
};
} // namespace

TEST_CASE("advance dispatches fixed ticks and passes fixed_dt", "[runtime]")
{
    World world;
    TickCounter& counter = world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();

    REQUIRE(loop.advance(world, 1.0f) == 4); // 1.0 / 0.25
    REQUIRE(counter.updates == 4);
    REQUIRE(counter.last_dt == 0.25f);
    REQUIRE(loop.tick_count() == 4);
}

TEST_CASE("advance remainder carries to the next advance", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();

    REQUIRE(loop.advance(world, 0.1f) == 0); // below one step
    REQUIRE(loop.advance(world, 0.2f) == 1); // 0.1 + 0.2 >= 0.25
}

TEST_CASE("stop pauses and ignores fed time; start resumes", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f);
    loop.start();
    loop.stop();

    REQUIRE_FALSE(loop.is_running());
    REQUIRE(loop.advance(world, 1.0f) == 0);   // ignored while stopped
    REQUIRE(loop.accumulator() == 0.0f);       // not banked

    loop.start();
    REQUIRE(loop.advance(world, 0.25f) == 1);
}

TEST_CASE("step runs exactly one tick regardless of state", "[runtime]")
{
    World world;
    TickCounter& counter = world.add_system<TickCounter>();
    RuntimeLoop loop(0.25f); // never started

    REQUIRE(loop.step(world) == 1);
    REQUIRE(counter.updates == 1);
    REQUIRE(loop.tick_count() == 1);
    REQUIRE_FALSE(loop.is_running());
}

TEST_CASE("same fixed_dt and dt schedule yield equal tick counts", "[runtime]")
{
    World a_world;
    a_world.add_system<TickCounter>();
    World b_world;
    b_world.add_system<TickCounter>();
    RuntimeLoop a(0.02f);
    RuntimeLoop b(0.02f);
    a.start();
    b.start();

    const float schedule[] = {0.033f, 0.017f, 0.05f, 0.001f, 0.1f};
    for (float dt : schedule)
    {
        a.advance(a_world, dt);
        b.advance(b_world, dt);
    }
    REQUIRE(a.tick_count() == b.tick_count());
}

TEST_CASE("advance is capped by max_steps_per_advance", "[runtime]")
{
    World world;
    world.add_system<TickCounter>();
    RuntimeLoop loop(0.01f, 4);
    loop.start();

    REQUIRE(loop.advance(world, 100.0f) == 4); // huge dt clamped to the cap
    REQUIRE(loop.tick_count() == 4);
}
```

Register in `tests/CMakeLists.txt` — add inside the `add_executable(mosaic_unit_tests ...)` source list, after `unit/transform/transform_system_test.cpp`:

```cmake
  unit/runtime/runtime_loop_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: engine/runtime/runtime_loop.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/runtime/runtime_loop.hpp`:

```cpp
#pragma once

#include <cstdint>

#include "engine/world/world.hpp"

namespace mosaic::engine::runtime
{

// Fixed-timestep accumulator over World::tick. Owns pacing only: the caller feeds
// elapsed real time via advance(); the loop dispatches whole fixed ticks. No
// wall-clock inside, so a given (fixed_dt, real_dt schedule) is deterministic.
class RuntimeLoop
{
public:
    explicit RuntimeLoop(float fixed_dt, int max_steps_per_advance = 8)
        : fixed_dt_(fixed_dt), max_steps_(max_steps_per_advance)
    {
    }

    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool is_running() const { return running_; }

    // Bank real_dt and run as many fixed ticks as fit (capped). No-op returning 0
    // while stopped. Remainder carries to the next call. Negative real_dt clamps to 0.
    int advance(World& world, float real_dt)
    {
        if (!running_)
        {
            return 0;
        }
        accumulator_ += (real_dt > 0.0f ? real_dt : 0.0f);
        int steps = 0;
        while (accumulator_ >= fixed_dt_ && steps < max_steps_)
        {
            world.tick(fixed_dt_);
            accumulator_ -= fixed_dt_;
            ++tick_count_;
            ++steps;
        }
        // Spiral-of-death guard: hit the cap with time still pending -> drop backlog.
        if (steps == max_steps_ && accumulator_ >= fixed_dt_)
        {
            accumulator_ = 0.0f;
        }
        return steps;
    }

    // One fixed tick regardless of running state / accumulator (editor/debug step).
    int step(World& world)
    {
        world.tick(fixed_dt_);
        ++tick_count_;
        return 1;
    }

    float fixed_dt() const { return fixed_dt_; }
    float accumulator() const { return accumulator_; }
    std::uint64_t tick_count() const { return tick_count_; }

private:
    float fixed_dt_;
    int max_steps_;
    float accumulator_ = 0.0f;
    bool running_ = false;
    std::uint64_t tick_count_ = 0;
};

} // namespace mosaic::engine::runtime
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[runtime]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/runtime/runtime_loop.hpp tests/unit/runtime/runtime_loop_test.cpp tests/CMakeLists.txt
git commit -m "Add RuntimeLoop: fixed-timestep driver for World::tick (#22)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Populated-World stability

**Files:**
- Create: `tests/unit/runtime/runtime_stability_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `RuntimeLoop` (Task 1), `TransformSystem`/`Transform`/`WorldTransform` (#21), `World`. No new production code — the acceptance "headless run of a populated World over many ticks is stable".

- [ ] **Step 1: Write the failing test**

Create `tests/unit/runtime/runtime_stability_test.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "engine/runtime/runtime_loop.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/math/vec3.hpp"

using Catch::Approx;
using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::engine::transform::WorldTransform;
using mosaic::foundation::math::transform_point;
using mosaic::foundation::math::Vec3;
using EntityID = World::EntityID;

TEST_CASE("populated World is stable over many ticks", "[runtime]")
{
    World world;
    world.add_system<TransformSystem>();

    const EntityID parent = world.create_entity();
    Transform pt;
    pt.position = Vec3{1.0f, 0.0f, 0.0f};
    world.add_component<Transform>(parent, pt);

    const EntityID child = world.create_entity();
    Transform ct;
    ct.position = Vec3{2.0f, 0.0f, 0.0f};
    ct.parent = parent;
    world.add_component<Transform>(child, ct);

    RuntimeLoop loop(0.016f);
    loop.start();
    for (int frame = 0; frame < 1000; ++frame)
    {
        loop.advance(world, 0.016f);
    }

    REQUIRE(loop.tick_count() >= 900);      // ~1000 ticks; tolerant of float drift
    REQUIRE(world.entity_count() == 2);     // graph intact
    REQUIRE(world.is_valid(parent));
    REQUIRE(world.is_valid(child));

    const WorldTransform* cw = world.get_component<WorldTransform>(child);
    REQUIRE(cw != nullptr);
    const Vec3 p = transform_point(cw->matrix, Vec3{0.0f, 0.0f, 0.0f});
    REQUIRE(p.x == Approx(3.0f)); // parent(1) composed with child(2)
}
```

Register in `tests/CMakeLists.txt`, after the Task 1 line:

```cmake
  unit/runtime/runtime_stability_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: This task adds only a test over already-built production code, so it should compile and PASS immediately once registered. Confirm the case is present and green:
Run: `./build/debug/tests/mosaic_unit_tests "populated World is stable over many ticks"`
Expected: the case runs and PASSES. (If the file is not yet registered, the run reports "No tests match" — add the CMake line, rebuild, and re-run.)

- [ ] **Step 3: (no new production code)**

This task only adds a test — RuntimeLoop (Task 1) + TransformSystem (#21) already provide the behavior. Skip to verification.

- [ ] **Step 4: Run the full suite**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[runtime]" && ctest --preset debug`
Expected: PASS — the `[runtime]` subset and the full suite are green.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/runtime/runtime_stability_test.cpp tests/CMakeLists.txt
git commit -m "Add populated-World stability test over many ticks (#22)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`:
- Add a "built and merged" paragraph for the runtime loop (#22) under *Where we are*: `engine/runtime/runtime_loop.hpp` (`RuntimeLoop` — deterministic fixed-timestep accumulator, start/stop/single-step, spiral-of-death guard), driving `World::tick` from externally supplied `real_dt`; note the real `std::chrono` pump is the app's job.
- State that **the headless engine core of the v0.1 vertical slice is now complete** (#9–#11, World, #18–#22 + #26). Update *What to build next* to point at the first *visible* subsystems (rendering / studio) and the remaining foundation pieces (logging, jobs), per the roadmap — the frontier is no longer a single headless unit.
- Update the *Suggested sequence* diagram to mark `#22 runtime loop` done (✓).
- Bump **Last updated** to note the runtime loop #22 built.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: runtime loop built; engine core complete (#22)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Fixed-timestep accumulator driving `World::tick` → Task 1 (`advance`).
- N ms → expected tick count (remainder carries) → Task 1 (dispatch + remainder tests).
- Start / stop / single-step controls → Task 1 (`start`/`stop`/`step` tests).
- Deterministic (same schedule → same count) → Task 1 (determinism test).
- Spiral-of-death guard → Task 1 (cap test + impl).
- Headless populated-World stability over many ticks → Task 2.
- Out-of-scope (chrono pump, interpolation alpha, threading) → not built; recorded in spec + Task 3 handoff.
- Unit tests under `tests/unit/runtime/` → Tasks 1–2.

**Placeholder scan:** No TBD/TODO in production or test steps. Task 2 Step 3 is an explicit "no production code" note; Task 3 is prose documentation.

**Type consistency:** `RuntimeLoop` ctor and methods (`start`/`stop`/`is_running`/`advance`/`step`/`fixed_dt`/`accumulator`/`tick_count`) are named identically in Task 1 impl and every test call in Tasks 1–2. `World::tick`/`add_system`/`create_entity`/`add_component`/`get_component`/`entity_count`/`is_valid` match the existing `World` API. `TickCounter`/`TransformSystem` override `System::update(World&, float)` with the exact signature. `Transform`/`WorldTransform`/`transform_point`/`Vec3` match the #21 definitions.
</content>
