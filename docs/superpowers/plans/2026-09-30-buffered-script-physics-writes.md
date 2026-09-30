# Buffered Off-Thread Physics Writes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `apply_impulse` work under `--script-thread` by buffering impulses the way that path already buffers component writes.

**Architecture:** `ScriptWorker` binds its own `apply_impulse` into the engines it owns: it records the impulse into a per-job buffer and answers `True`/`False` from the job snapshot. `collect()` returns writes and impulses together, and `ScriptSystem` replays the impulses on the main thread through the neutral `host_.apply_impulse` it already holds — so `ScriptSystem` never learns what physics is. The `!script_thread` guard added in #207 is then removed.

**Tech Stack:** C++20, std::thread/mutex/condition_variable, embedded CPython via pybind11, Box3D, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-30-buffered-script-physics-writes-design.md`

## Global Constraints

- No script code may touch the live physics world from the worker thread. The worker records; the main thread applies.
- `apply_impulse`'s return value means the same thing in both modes: "this entity has a simulated body and the impulse will reach it."
- The worker answers validity from the **job snapshot**, not the live world. Body creation requires a `RigidBody` **and** one of `Collider`, `SphereCollider`, `CapsuleCollider` — those exact reflected names, as registered in `engine/reflection/builtin_components.hpp:50-53`.
- `ScriptSystem` must not include or reference any physics header. It replays through `host_.apply_impulse`.
- Do not change `raycast` in any way — its off-thread read is explicitly out of scope and separately tracked.
- Do not change the single-threaded path's behaviour.
- Introduce no new synchronization primitive: the worker's existing `mutex_` already guards the result hand-off.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`.
- **Those two excluded tests are the known embedded-Python threading flakes.** This plan touches the threaded path, so run them explicitly (not just the gate) and judge failures on their own merits rather than assuming the flake.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the previous commit, and amending then rewrites that one.
- Catch2 `TEST_CASE` names: no commas inside the name, no leading `--`, no unmatched `[`.

## Review Focus

Input classes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **A script pushing an entity that has a `RigidBody` but no collider.** Checking only `RigidBody` is the natural shortcut and over-reports: the entity never gets a body, so `True` would be a lie. → Task 1.
2. **A script pushing an entity that is not in the snapshot at all** (an id it invented, or a non-scripted entity). The snapshot only contains scripted entities, so a lookup miss must be `False` and must not buffer. → Task 1.
3. **Two impulses on the same entity in one tick.** Both must survive to the main thread and both must be applied — a map keyed by entity would silently drop one. → Task 1.
4. **A tick where scripts buffer impulses but no component writes** (and vice versa). The two channels must be independent; a shared "did anything happen" flag would drop one. → Task 2.
5. **The single-threaded path after the guard is removed.** Removing `&& !script_thread` must not change single-threaded behaviour at all, and the guard must be genuinely gone rather than bypassed. → Task 3.

---

### Task 1: The worker records impulses and answers from the snapshot

Adds the buffered-impulse type and the worker's own `apply_impulse`. Self-contained: testable through `ScriptWorker` directly, with no `ScriptSystem` or physics involved.

**Files:**
- Modify: `engine/script/script_worker.hpp`
- Modify: `engine/script/script_worker.cpp`
- Test: `tests/unit/script/script_worker_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `mosaic::engine::script::DeferredImpulse { std::uint64_t entity; Vec3 impulse; Vec3 point; }` and `ScriptWorker::WorkerResult { std::vector<SnapshotContext::Write> writes; std::vector<DeferredImpulse> impulses; }`; `ScriptWorker::collect()` now returns `WorkerResult`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/script/script_worker_test.cpp`:

```cpp
TEST_CASE("ScriptWorker buffers apply_impulse instead of calling the host", "[script]")
{
    // The whole point: on the worker thread the call must NOT reach the host, which
    // on the real path writes into the live Box3D body while the main thread may be
    // mid-step. It is recorded and replayed on the main thread instead.
    bool host_called = false;
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, mosaic::foundation::math::Vec3,
                             mosaic::foundation::math::Vec3) -> bool
    { host_called = true; return true; };

    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[7] = {{"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"SphereCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({7, "def on_update(dt):\n"
                              "    ok = apply_impulse(7, [2.0, 0.0, -1.0], [0.0, 0.5, 0.0])\n"
                              "    assert ok is True\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE_FALSE(host_called); // never reached the live world
    REQUIRE(result.impulses.size() == 1);
    REQUIRE(result.impulses[0].entity == 7);
    REQUIRE(result.impulses[0].impulse.x == 2.0f);
    REQUIRE(result.impulses[0].impulse.z == -1.0f);
    REQUIRE(result.impulses[0].point.y == 0.5f);
}

TEST_CASE("ScriptWorker reports False for an entity with a RigidBody but no collider", "[script]")
{
    // Review Focus 1: checking only RigidBody is the natural shortcut and over-reports.
    // Body creation needs a RigidBody AND a collider, so this entity never gets one.
    ScriptWorker worker([] { return create_python_script_engine(); },
                        mosaic::engine::script::ScriptHost{});

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[3] = {{"RigidBody", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({3, "def on_update(dt):\n"
                              "    ok = apply_impulse(3, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty()); // nothing buffered for a body that cannot exist
}

TEST_CASE("ScriptWorker reports False for an entity with no RigidBody", "[script]")
{
    // A distinct early return from the no-collider case above: this entity is in the
    // snapshot and has a collider, but nothing makes it simulated.
    ScriptWorker worker([] { return create_python_script_engine(); },
                        mosaic::engine::script::ScriptHost{});

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[4] = {{"SphereCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({4, "def on_update(dt):\n"
                              "    ok = apply_impulse(4, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty());
}

TEST_CASE("ScriptWorker reports False for an entity absent from the snapshot", "[script]")
{
    // Review Focus 2: the snapshot holds only scripted entities. An id the script
    // invented must be a clean False, not a buffered impulse aimed at nothing.
    ScriptWorker worker([] { return create_python_script_engine(); },
                        mosaic::engine::script::ScriptHost{});

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[1] = {{"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"Collider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({1, "def on_update(dt):\n"
                              "    ok = apply_impulse(999999, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty());
}

TEST_CASE("ScriptWorker keeps both impulses when one entity is pushed twice in a tick", "[script]")
{
    // Review Focus 3: a map keyed by entity would silently drop one. Impulses are
    // additive in physics, so both must survive.
    ScriptWorker worker([] { return create_python_script_engine(); },
                        mosaic::engine::script::ScriptHost{});

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[5] = {{"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"CapsuleCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({5, "def on_update(dt):\n"
                              "    apply_impulse(5, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    apply_impulse(5, [0.0, 2.0, 0.0], [0.0, 0.0, 0.0])\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.size() == 2);
    REQUIRE(result.impulses[0].impulse.x == 1.0f);
    REQUIRE(result.impulses[1].impulse.y == 2.0f);
}
```

Then update the existing test in that file that calls `worker.collect()` (around line 23): it currently binds the result to `const auto writes = worker.collect();` and indexes it as a vector. Change it to `const auto result = worker.collect();` and use `result.writes` wherever `writes` was used. **Do not weaken any of its assertions** — the writes channel must stay as meaningful as it is today.

Add these includes to the test file if absent: `#include "engine/script/script_host.hpp"` and `#include "foundation/math/vec3.hpp"`.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `'struct mosaic::engine::script::ScriptWorker::WorkerResult' has not been declared`, or `'DeferredImpulse' was not declared in this scope`.

- [ ] **Step 3: Add the types and the new collect signature**

In `engine/script/script_worker.hpp`, inside `namespace mosaic::engine::script` and **above** `class ScriptWorker`:

```cpp
// A physics impulse a script asked for on the worker thread, recorded rather than
// applied. The main thread replays it through the neutral ScriptHost callback -- the
// worker must never touch the live physics world, because it runs while the main
// thread may be inside PhysicsSystem's step.
struct DeferredImpulse
{
    std::uint64_t                  entity = 0;
    mosaic::foundation::math::Vec3 impulse{};
    mosaic::foundation::math::Vec3 point{};
};
```

Add `#include "foundation/math/vec3.hpp"` to that header's includes.

Inside `class ScriptWorker`, in the `public:` section above `dispatch`:

```cpp
    // What one job produced. Two independent channels: a tick may buffer writes with
    // no impulses, impulses with no writes, or both.
    struct WorkerResult
    {
        std::vector<SnapshotContext::Write> writes;
        std::vector<DeferredImpulse>        impulses;
    };
```

Change the declarations:

```cpp
    WorkerResult collect();   // wait for the job, return its writes + impulses
```

```cpp
    WorkerResult execute(const Job& job); // on worker thread
```

and the member:

```cpp
    WorkerResult result_;
```

Add a private member for the host the worker actually binds:

```cpp
    // host_ with apply_impulse replaced by the recording version. Bound into every
    // engine instead of host_, so a script's push is buffered rather than applied.
    ScriptHost worker_host_;
    // Worker-thread-only state, valid for the duration of one execute() call.
    const Job*                   current_job_ = nullptr;
    std::vector<DeferredImpulse> pending_impulses_;
```

- [ ] **Step 4: Build the recording host and record in execute**

In `engine/script/script_worker.cpp`, replace the constructor body so it builds `worker_host_`:

```cpp
ScriptWorker::ScriptWorker(EngineFactory factory, ScriptHost host)
    : factory_(std::move(factory)), host_(std::move(host))
{
    worker_host_ = host_;
    // Record instead of applying. Answer from the CURRENT job's snapshot: body
    // creation needs a RigidBody plus one of the three collider components, so
    // checking RigidBody alone would report True for an entity that never gets a body.
    worker_host_.apply_impulse = [this](std::uint64_t entity,
                                        mosaic::foundation::math::Vec3 impulse,
                                        mosaic::foundation::math::Vec3 point) -> bool
    {
        if (current_job_ == nullptr) { return false; }
        const auto ent = current_job_->snapshot.find(entity);
        if (ent == current_job_->snapshot.end()) { return false; }
        if (ent->second.find("RigidBody") == ent->second.end()) { return false; }
        const bool has_collider = ent->second.find("Collider") != ent->second.end()
                               || ent->second.find("SphereCollider") != ent->second.end()
                               || ent->second.find("CapsuleCollider") != ent->second.end();
        if (!has_collider) { return false; }
        pending_impulses_.push_back(DeferredImpulse{entity, impulse, point});
        return true;
    };
    thread_ = std::thread([this] { run(); });
}
```

In `run()`, change the result hand-off block from the writes-only form to:

```cpp
        WorkerResult result = execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_       = std::move(result);
            result_ready_ = true;
        }
```

Change `collect()`'s early return and signature:

```cpp
ScriptWorker::WorkerResult ScriptWorker::collect()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!pending_ && !result_ready_) { return {}; } // nothing in flight
    cv_.wait(lock, [this] { return result_ready_ || stop_; });
    result_ready_ = false;
    return std::move(result_);
}
```

Change `execute()` to set up and tear down the per-job state, bind `worker_host_`, and return both channels:

```cpp
ScriptWorker::WorkerResult ScriptWorker::execute(const Job& job)
{
    current_job_ = &job;
    pending_impulses_.clear();
    SnapshotContext ctx(job.snapshot); // reads snapshot, buffers writes
    for (const ScriptRef& ref : job.scripts)
    {
        Entry& entry = engines_[ref.entity];
        if (!entry.engine || entry.source != ref.source)
        {
            entry.engine = factory_();
            entry.source = ref.source;
            entry.engine->bind_context(ctx);
            entry.engine->bind_host(worker_host_);
            entry.engine->set_global(
                "entity", mosaic::foundation::serialization::Value(ref.entity));
            entry.engine->run_string(ref.source); // (re)compile: define on_update
        }
        else
        {
            entry.engine->bind_context(ctx); // rebind: fresh context each job
        }
        entry.engine->set_global(
            "dt", mosaic::foundation::serialization::Value(static_cast<double>(job.dt)));
        entry.engine->run_string("on_update(dt)");
    }
    WorkerResult out;
    out.writes   = std::vector<SnapshotContext::Write>(ctx.writes());
    out.impulses = std::move(pending_impulses_);
    pending_impulses_.clear();
    current_job_ = nullptr;
    return out;
}
```

Note `bind_host(worker_host_)` replaces `bind_host(host_)` — that single word is what makes the whole slice work, because engines are bound once at creation and reused across jobs.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[script]"
```

Expected: PASS. Then run the two normally-excluded threading tests explicitly, because this task changes the threaded path:

```bash
./build/debug/tests/mosaic_unit_tests "ScriptWorker runs a job on its thread and returns the write batch"
./build/debug/tests/mosaic_unit_tests "threaded ScriptSystem applies script writes one tick later"
```

Expected: PASS. The second is a known intermittent SIGSEGV under full-suite ordering; run it a second time before concluding anything, and if it fails twice in isolation, treat it as caused by this change rather than as the flake.

- [ ] **Step 6: Commit**

```bash
git add engine/script/script_worker.hpp engine/script/script_worker.cpp tests/unit/script/script_worker_test.cpp
git commit -m "feat(script): buffer worker-thread impulses instead of applying them"
```

---

### Task 2: `ScriptSystem` replays buffered impulses on the main thread

Drains the second channel through the neutral host callback.

**Files:**
- Modify: `engine/script/script_system.hpp`
- Test: `tests/unit/script/script_system_test.cpp`

**Interfaces:**
- Consumes: `ScriptWorker::WorkerResult { writes, impulses }` and `DeferredImpulse { entity, impulse, point }` from Task 1.
- Produces: nothing consumed by a later task.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/script/script_system_test.cpp`:

```cpp
TEST_CASE("threaded ScriptSystem replays buffered impulses through the host", "[script]")
{
    // The impulses must reach the main thread and go out through the SAME neutral
    // callback the single-threaded path uses -- ScriptSystem must never learn what
    // physics is.
    struct Seen { std::uint64_t entity; float ix; float py; };
    std::vector<Seen> seen;

    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [&](std::uint64_t e, mosaic::foundation::math::Vec3 imp,
                             mosaic::foundation::math::Vec3 pt) -> bool
    { seen.push_back({e, imp.x, pt.y}); return true; };

    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    World world;
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); },
                                   host, nullptr, /*script_thread=*/true);

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<mosaic::engine::physics::RigidBody>(
        e, mosaic::engine::physics::RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::SphereCollider>(
        e, mosaic::engine::physics::SphereCollider{0.5f});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    apply_impulse(entity, [4.0, 0.0, 0.0], [0.0, 0.25, 0.0])\n"});

    // Tick 1 dispatches the job; tick 2 collects and replays it. That one-tick lag is
    // the threaded path's existing contract, not a new delay.
    world.tick(1.0f / 60.0f);
    world.tick(1.0f / 60.0f);

    REQUIRE(seen.size() >= 1);
    REQUIRE(seen[0].entity == e.value());
    REQUIRE(seen[0].ix == Catch::Approx(4.0f));
    REQUIRE(seen[0].py == Catch::Approx(0.25f));
}

TEST_CASE("threaded ScriptSystem handles a tick with impulses but no component writes", "[script]")
{
    // Review Focus 4: the two channels are independent. A shared "did anything
    // happen" flag would drop one of them; this script writes no component at all.
    int calls = 0;
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, mosaic::foundation::math::Vec3,
                             mosaic::foundation::math::Vec3) -> bool
    { ++calls; return true; };

    TypeRegistry reg;
    ComponentReflection types;
    register_builtin_components(reg, types);

    World world;
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); },
                                   host, nullptr, /*script_thread=*/true);

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<mosaic::engine::physics::RigidBody>(
        e, mosaic::engine::physics::RigidBody{true, 1.0f});
    world.add_component<mosaic::engine::physics::Collider>(
        e, mosaic::engine::physics::Collider{mosaic::foundation::math::Vec3{0.5f, 0.5f, 0.5f}});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    apply_impulse(entity, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"});

    world.tick(1.0f / 60.0f);
    world.tick(1.0f / 60.0f);

    REQUIRE(calls >= 1);
}
```

Add includes to that test file if absent: `#include "engine/physics/physics_components.hpp"`, `#include "engine/script/script_host.hpp"`, `#include "foundation/math/vec3.hpp"`, `#include <catch2/catch_approx.hpp>`.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "threaded ScriptSystem replays buffered impulses through the host"
```

Expected: FAIL — `seen` stays empty, because nothing drains the impulse channel yet. (It compiles: `collect()` already returns `WorkerResult` from Task 1, and the existing loop reads `.writes`.)

- [ ] **Step 3: Drain the second channel**

In `engine/script/script_system.hpp`, `update_threaded` currently starts by applying the previous tick's writes:

```cpp
        for (const SnapshotContext::Write& w : worker_->collect())
        {
            types_.set_value(world, mosaic::foundation::EntityID(w.entity), w.comp, w.value);
        }
```

Replace that with:

```cpp
        // 1. Apply the PREVIOUS tick's buffered results on the main thread.
        const ScriptWorker::WorkerResult batch = worker_->collect();
        for (const SnapshotContext::Write& w : batch.writes)
        {
            types_.set_value(world, mosaic::foundation::EntityID(w.entity), w.comp, w.value);
        }
        // Impulses go back out through the same neutral host callback the
        // single-threaded path uses, so this system still never depends on physics.
        // ScriptSystem is registered before PhysicsSystem, so a replayed impulse lands
        // before this tick's step.
        if (host_.apply_impulse)
        {
            for (const DeferredImpulse& imp : batch.impulses)
            {
                host_.apply_impulse(imp.entity, imp.impulse, imp.point);
            }
        }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[script]"
```

Expected: PASS. Then the two normally-excluded threading tests explicitly:

```bash
./build/debug/tests/mosaic_unit_tests "ScriptWorker runs a job on its thread and returns the write batch"
./build/debug/tests/mosaic_unit_tests "threaded ScriptSystem applies script writes one tick later"
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/script/script_system.hpp tests/unit/script/script_system_test.cpp
git commit -m "feat(script): replay buffered impulses on the main thread"
```

---

### Task 3: Remove the guard and prove the whole path

Deletes #207's `!script_thread` clause and adds the end-to-end test that the guard made impossible.

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Modify: `docs/project/deferred-work-backlog.md`
- Test: `tests/unit/app/script_runtime_test.cpp`

**Interfaces:**
- Consumes: the replay from Task 2.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/script_runtime_test.cpp`:

```cpp
TEST_CASE("a script can push a body on the threaded path via setup_runtime", "[app]")
{
    // This is what the guard made impossible: under --script-thread, apply_impulse was
    // not wired at all, so a script that works single-threaded silently did nothing.
    // The push is now buffered on the worker and replayed on the main thread.
    using mosaic::engine::physics::Collider;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::RigidBody;
    using mosaic::engine::physics::SphereCollider;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    const auto ground = world.create_entity();
    world.add_component<Transform>(
        ground, Transform{Vec3{0, 0, 0}, Quat::identity(), Vec3{10, 1, 10}, {}});
    world.add_component<RigidBody>(ground, RigidBody{false, 1.0f});
    world.add_component<Collider>(ground, Collider{Vec3{5.0f, 0.5f, 5.0f}});

    const auto ball = world.create_entity();
    world.add_component<Transform>(
        ball, Transform{Vec3{0, 1, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    world.add_component<ScriptComponent>(ball, ScriptComponent{
        "_done = False\n"
        "def on_update(dt):\n"
        "    global _done\n"
        "    if not _done:\n"
        "        if apply_impulse(entity, [3.0, 0.0, 0.0], [0.0, 0.5, 0.0]):\n"
        "            _done = True\n"});

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); },
                  mosaic::engine::script::ScriptHost{}, /*script_thread=*/true);

    for (int i = 0; i < 60; ++i) { world.tick(1.0f / 60.0f); }

    REQUIRE(world.get_component<Transform>(ball)->position.x > 0.1f);
}
```

Check `setup_runtime`'s parameter order in `app/run/demo_scene.hpp` before writing the call — `host` precedes `script_thread`, and both follow the script factory. Pass a default-constructed `ScriptHost{}` so `setup_runtime` wires its own physics callbacks.

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "a script can push a body on the threaded path via setup_runtime"
```

Expected: FAIL — `position.x` stays at or near `0.0`, because the guard still refuses to wire `apply_impulse` when `script_thread` is true.

- [ ] **Step 3: Remove the guard**

In `app/run/demo_scene.hpp`, `setup_runtime` currently contains an eight-line comment followed by:

```cpp
    if (!host.apply_impulse && !script_thread)
    {
        host.apply_impulse = make_physics_apply_impulse(world);
    }
```

Replace the whole block — comment included — with:

```cpp
    // Scripts get apply_impulse() on both paths. On --script-thread the ScriptWorker
    // binds its own recording version over this one, buffering the impulse and
    // replaying it on the main thread, so this callback is never invoked from the
    // worker thread even though it is wired unconditionally.
    if (!host.apply_impulse) { host.apply_impulse = make_physics_apply_impulse(world); }
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]"
```

Expected: PASS, including the pre-existing single-threaded push test — Review Focus 5 is that the single-threaded path is unchanged, and that test is what proves it.

- [ ] **Step 5: Mark the backlog entry shipped**

In `docs/project/deferred-work-backlog.md`, find the entry beginning:

```
- **Script-thread impulses are disabled, not buffered.**
```

Replace its opening marker so it reads as shipped, keeping the original text beneath it for context:

```
- ~~**Script-thread impulses are disabled, not buffered**~~ — **SHIPPED
  2026-09-30.** Impulses are now recorded by `ScriptWorker` against the job
  snapshot and replayed on the main thread through the neutral host callback,
  so the same script works on both paths with the one-tick lag the threaded
  path already has for component writes. The `!script_thread` guard is gone.
  `raycast`'s off-thread read is unchanged and still tracked below. Original
  entry follows for context.

- **Script-thread impulses are disabled, not buffered.**
```

- [ ] **Step 6: Run the full gate and verify both entry points**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"
./build/debug/tests/mosaic_unit_tests "ScriptWorker runs a job on its thread and returns the write batch"
./build/debug/tests/mosaic_unit_tests "threaded ScriptSystem applies script writes one tick later"
```

Expected: all PASS.

```bash
timeout 40 ./build/debug/app/mosaic run --window --duration 10
timeout 40 ./build/debug/app/mosaic run --window --script-thread --duration 10
```

Expected for both: exit 0, **zero Vulkan validation errors**, no Python errors. The second is the one that matters — the demo sphere should now be nudged on the threaded path too, which it was not before this branch.

- [ ] **Step 7: Commit**

```bash
git add app/run/demo_scene.hpp docs/project/deferred-work-backlog.md tests/unit/app/script_runtime_test.cpp
git commit -m "feat(app): wire apply_impulse on the threaded path too"
```
