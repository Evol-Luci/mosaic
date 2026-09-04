# Script Worker Thread — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD, commit after each task.

**Goal:** An opt-in threaded mode for `ScriptSystem` — scripts run on a
dedicated Python-owning worker thread against a snapshot and emit a write batch
the main thread applies one tick later. Inline stays the default.

**Architecture:** `ScriptContext`/`SnapshotContext` sandbox seam; engine
`bind_context`; a `ScriptWorker` thread owning the interpreter + engines;
`ScriptSystem` threaded pipeline (wait-previous → apply → snapshot → dispatch).

**Tech Stack:** C++20, Catch2, pybind11, `std::thread`/`mutex`/`condition_variable`.

**Spec:** `docs/superpowers/specs/2026-09-03-script-worker-thread-design.md`

## Global Constraints

- Tier wall: `foundation/`→`engine/`→`modules/`→`app/`. New script code is
  `engine/script/`; the Python backend implements `bind_context` in `modules/`.
- Threading only in `engine/script/script_worker.*`; all Python create/use on
  the worker thread. Main thread never calls Python in threaded mode.
- Every new ctor/function param is **defaulted**; inline mode = today's
  behaviour, all existing tests green.
- `bind_context` is **non-pure** (default no-op).
- Catch2 `TEST_CASE` names must not start with `--`. No new dependencies.

---

### Task 1: `ScriptContext` + `SnapshotContext`

**Files:**
- Create: `engine/script/script_context.hpp`
- Create: `tests/unit/script/script_context_test.cpp`
- Modify: `tests/CMakeLists.txt` (after `unit/script/script_asset_test.cpp`)

**Interfaces produced:** `ScriptContext` (abstract), `SnapshotContext` with
`Snapshot`/`Write` types + `writes()`.

- [ ] **Step 1: Write `engine/script/script_context.hpp`:**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
using Value    = mosaic::foundation::serialization::Value;
using EntityID = mosaic::foundation::EntityID;

// Sandbox seam: a script's get_component/set_component route through this
// instead of touching World, so worker-thread scripts never race the main loop.
class ScriptContext
{
public:
    virtual ~ScriptContext() = default;
    virtual std::optional<Value> get(EntityID e, std::string_view comp) = 0;
    virtual bool set(EntityID e, std::string_view comp, const Value& v) = 0;
};

// Reads an immutable snapshot; buffers writes for the main thread to apply.
class SnapshotContext : public ScriptContext
{
public:
    using Snapshot =
        std::unordered_map<std::uint64_t, std::unordered_map<std::string, Value>>;
    struct Write { std::uint64_t entity; std::string comp; Value value; };

    explicit SnapshotContext(const Snapshot& snap) : snap_(&snap) {}

    std::optional<Value> get(EntityID e, std::string_view comp) override
    {
        auto ent = snap_->find(e.value());
        if (ent == snap_->end()) { return std::nullopt; }
        auto it = ent->second.find(std::string(comp));
        if (it == ent->second.end()) { return std::nullopt; }
        return it->second;
    }

    bool set(EntityID e, std::string_view comp, const Value& v) override
    {
        writes_.push_back(Write{e.value(), std::string(comp), v});
        return true;
    }

    const std::vector<Write>& writes() const { return writes_; }

private:
    const Snapshot*    snap_;
    std::vector<Write> writes_;
};
} // namespace mosaic::engine::script
```

- [ ] **Step 2: Write the test** `tests/unit/script/script_context_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_context.hpp"

using mosaic::engine::script::SnapshotContext;
using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::Value;

TEST_CASE("SnapshotContext reads the snapshot and buffers writes", "[script]")
{
    SnapshotContext::Snapshot snap;
    snap[1]["Health"] = Value(static_cast<std::int64_t>(42));

    SnapshotContext ctx(snap);
    auto got = ctx.get(EntityID(1), "Health");
    REQUIRE(got.has_value());
    REQUIRE(ctx.get(EntityID(1), "Missing").has_value() == false);
    REQUIRE(ctx.get(EntityID(2), "Health").has_value() == false);

    REQUIRE(ctx.set(EntityID(1), "Health", Value(static_cast<std::int64_t>(7))));
    REQUIRE(ctx.set(EntityID(1), "Mana", Value(static_cast<std::int64_t>(3))));
    REQUIRE(ctx.writes().size() == 2);
    REQUIRE(ctx.writes()[0].entity == 1);
    REQUIRE(ctx.writes()[0].comp == "Health");
    REQUIRE(ctx.writes()[1].comp == "Mana");
}
```

Add to `tests/CMakeLists.txt` after `unit/script/script_asset_test.cpp`:
```
  unit/script/script_context_test.cpp
```

> NOTE: confirm the `Value` ctor for an integer (`std::int64_t`) against
> `foundation/serialization/value.hpp` — mirror whatever `value_marshal_test.cpp`
> uses. If Value has no int64 ctor, use a type it does (e.g. double/string).

- [ ] **Step 3: Build + run** `ctest --test-dir build/debug -R "buffers writes"` → PASS.
- [ ] **Step 4: Commit** `git add engine/script/script_context.hpp tests/unit/script/script_context_test.cpp tests/CMakeLists.txt && git commit -m "feat(script): ScriptContext + SnapshotContext sandbox seam"`

---

### Task 2: Engine `bind_context` routes get/set through a context

**Files:**
- Modify: `engine/script/script_engine.hpp` (include + non-pure `bind_context`)
- Modify: `modules/script_python/python_script_engine.cpp` (override + `context_` member)
- Create: `tests/unit/script/script_context_engine_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: `engine/script/script_engine.hpp`** — add include + method:
```cpp
#include "engine/script/script_context.hpp"
```
```cpp
    // Route get_component/set_component through a sandbox context (worker mode).
    // Optional; default no-op. Entity-lifecycle globals are NOT installed here.
    virtual void bind_context(ScriptContext& context) { (void)context; }
```

- [ ] **Step 2: Write the failing test** `tests/unit/script/script_context_engine_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_context.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::SnapshotContext;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_context routes get/set through the context, not World", "[script]")
{
    SnapshotContext::Snapshot snap;
    // A Transform-like value the script reads and writes back.
    snap[5]["Score"] = Value(1.0); // double

    SnapshotContext ctx(snap);
    auto engine = create_python_script_engine();
    engine->bind_context(ctx);
    engine->set_global("entity", Value(static_cast<std::uint64_t>(5)));

    auto r = engine->run_string(
        "v = get_component(entity, 'Score')\n"
        "set_component(entity, 'Score', v + 4.0)\n");
    REQUIRE(r.ok);
    REQUIRE(ctx.writes().size() == 1);
    REQUIRE(ctx.writes()[0].entity == 5);
    REQUIRE(ctx.writes()[0].comp == "Score");
}
```

Add to `tests/CMakeLists.txt` after `unit/script/script_context_test.cpp`:
```
  unit/script/script_context_engine_test.cpp
```

- [ ] **Step 3: Run to verify FAIL** — `get_component` undefined in context mode
  (backend doesn't override `bind_context`) → `r.ok == false`.

- [ ] **Step 4: Implement in `modules/script_python/python_script_engine.cpp`.**
  Add `#include "engine/script/script_context.hpp"`. Add member near `host_`:
  `mosaic::engine::script::ScriptContext* context_ = nullptr;`. Add the override
  (after `bind_host`):
```cpp
    void bind_context(mosaic::engine::script::ScriptContext& context) override
    {
        context_ = &context;
        globals_["get_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> py::object
            {
                auto v = context_->get(mosaic::foundation::EntityID(eid), name);
                return v ? value_to_py(*v) : py::none();
            });
        globals_["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                return context_->set(mosaic::foundation::EntityID(eid), name,
                                     py_to_value(value));
            });
    }
```
  (Do not install create_entity/destroy_entity here — unsupported in this mode.)

- [ ] **Step 5: Run to verify PASS** → the context test passes; full suite green.
- [ ] **Step 6: Commit** `git add engine/script/script_engine.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_context_engine_test.cpp tests/CMakeLists.txt && git commit -m "feat(script): engine bind_context routes get/set through a sandbox context"`

---

### Task 3: `ScriptWorker` — dedicated Python-owning thread + job queue

**Files:**
- Create: `engine/script/script_worker.hpp`
- Create: `engine/script/script_worker.cpp`
- Modify: `engine/script/CMakeLists.txt` (add `script_worker.cpp` to the script lib sources — check how the lib is defined; mirror existing .cpp entries)
- Create: `tests/unit/script/script_worker_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces produced:** `ScriptWorker` (`dispatch`, `collect`, `has_pending`,
`Job`), reusing `SnapshotContext::{Snapshot,Write}` and `ScriptSystem::EngineFactory`.

- [ ] **Step 1: Write `engine/script/script_worker.hpp`:**

```cpp
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "engine/script/script_context.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/script/script_host.hpp"

namespace mosaic::engine::script
{
// Runs script on_update()s on ONE dedicated thread that owns the interpreter and
// all engines. Main thread communicates only via dispatch()/collect(); it never
// touches Python. dispatch() enqueues; collect() waits for the in-flight job and
// returns its write batch. One job in flight at a time (a 1-tick pipeline).
class ScriptWorker
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;
    struct ScriptRef { std::uint64_t entity; std::string source; };
    struct Job
    {
        SnapshotContext::Snapshot snapshot;
        float                     dt = 0.0f;
        std::vector<ScriptRef>    scripts;
    };

    explicit ScriptWorker(EngineFactory factory, ScriptHost host = {});
    ~ScriptWorker();

    ScriptWorker(const ScriptWorker&) = delete;
    ScriptWorker& operator=(const ScriptWorker&) = delete;

    void dispatch(Job job);                          // enqueue (non-blocking)
    std::vector<SnapshotContext::Write> collect();   // wait for the job, return its writes
    bool has_pending() const;

private:
    void run();                                      // worker loop
    std::vector<SnapshotContext::Write> execute(const Job& job); // on worker thread

    EngineFactory factory_;
    ScriptHost    host_;

    // Engines live on the worker thread only.
    struct Entry { std::unique_ptr<ScriptEngine> engine; std::string source; };
    std::unordered_map<std::uint64_t, Entry> engines_;

    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::optional<Job>      job_;         // pending input
    std::vector<SnapshotContext::Write> result_;
    bool                    result_ready_ = false;
    bool                    pending_      = false;
    bool                    stop_         = false;
    std::thread             thread_;
};
} // namespace mosaic::engine::script
```

- [ ] **Step 2: Write `engine/script/script_worker.cpp`:**

```cpp
#include "engine/script/script_worker.hpp"

#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{

ScriptWorker::ScriptWorker(EngineFactory factory, ScriptHost host)
    : factory_(std::move(factory)), host_(std::move(host))
{
    thread_ = std::thread([this] { run(); });
}

ScriptWorker::~ScriptWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) { thread_.join(); }
}

void ScriptWorker::dispatch(Job job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_          = std::move(job);
        pending_      = true;
        result_ready_ = false;
    }
    cv_.notify_all();
}

std::vector<SnapshotContext::Write> ScriptWorker::collect()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!pending_ && !result_ready_) { return {}; } // nothing in flight
    cv_.wait(lock, [this] { return result_ready_ || stop_; });
    result_ready_ = false;
    return std::move(result_);
}

bool ScriptWorker::has_pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ || result_ready_;
}

void ScriptWorker::run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return pending_ || stop_; });
            if (stop_) { return; }
            job      = std::move(*job_);
            job_.reset();
            pending_ = false;
        }
        std::vector<SnapshotContext::Write> writes = execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_       = std::move(writes);
            result_ready_ = true;
        }
        cv_.notify_all();
    }
}

std::vector<SnapshotContext::Write> ScriptWorker::execute(const Job& job)
{
    SnapshotContext ctx(job.snapshot); // reads snapshot, buffers writes
    for (const ScriptRef& ref : job.scripts)
    {
        Entry& entry = engines_[ref.entity];
        if (!entry.engine || entry.source != ref.source)
        {
            entry.engine = factory_();
            entry.source = ref.source;
            entry.engine->bind_context(ctx);
            entry.engine->bind_host(host_);
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
    return std::vector<SnapshotContext::Write>(ctx.writes());
}

} // namespace mosaic::engine::script
```

> NOTE: `bind_context` stores a pointer to `ctx`, which lives for the whole
> `execute` call — every engine used this job shares the one context, so all
> their writes accumulate into `ctx.writes()`. Good. Prune of dead entities is
> omitted for v0.1 (engines for removed entities linger harmlessly); add later
> if needed.

- [ ] **Step 3: Add `script_worker.cpp` to the engine library.** `engine/script`
  is header-only (no CMakeLists); engine `.cpp`s compile in
  `engine/CMakeLists.txt`'s `add_library(mosaic_engine STATIC ...)` list (which
  currently has `ecs/entity_registry.cpp`, `world/world.cpp`). Add
  `script/script_worker.cpp` to that list. `script_worker.cpp` uses `std::thread`,
  and `mosaic_engine` does NOT currently link Threads (foundation links it
  PRIVATE), so also add to `engine/CMakeLists.txt`:
```cmake
find_package(Threads REQUIRED)
target_link_libraries(mosaic_engine PRIVATE Threads::Threads)
```
  `script_worker.cpp` includes only engine-tier headers (`ScriptEngine`
  interface, `ScriptContext`, `ScriptHost`) — NO pybind11 — so no backend link is
  needed; engines arrive via the injected factory.

- [ ] **Step 4: Write the test** `tests/unit/script/script_worker_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_worker.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptWorker;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("ScriptWorker runs a job on its thread and returns the write batch", "[script]")
{
    ScriptWorker worker([] { return create_python_script_engine(); });

    ScriptWorker::Job job;
    job.dt = 0.5f;
    job.snapshot[1]["Score"] = Value(2.0);
    job.scripts.push_back(ScriptWorker::ScriptRef{
        1, "def on_update(dt):\n"
           "    v = get_component(entity, 'Score')\n"
           "    set_component(entity, 'Score', v + dt)\n"});

    worker.dispatch(std::move(job));
    const auto writes = worker.collect();

    REQUIRE(writes.size() == 1);
    REQUIRE(writes[0].entity == 1);
    REQUIRE(writes[0].comp == "Score");
    // value 2.0 + dt 0.5 = 2.5 — marshal back to double and check.
    REQUIRE(writes[0].value.as<double>() == 2.5);
}
```

Add to `tests/CMakeLists.txt` after `unit/script/script_context_engine_test.cpp`:
```
  unit/script/script_worker_test.cpp
```

> NOTE: `Value` accessor is the template `value.as<double>()` (from
> `foundation/serialization/value.hpp`: `template<class T> const T& as() const`).
> Numeric ctors: `Value(double)`, `Value(std::int64_t)`, `Value(std::uint64_t)`.
> Guard with `value.is<double>()` if a script might return a non-double.

- [ ] **Step 5: Build + run** `ctest --test-dir build/debug -R "returns the write batch"` → PASS. Then full suite green.
- [ ] **Step 6: Commit** `git add engine/script/script_worker.hpp engine/script/script_worker.cpp engine/script/CMakeLists.txt tests/unit/script/script_worker_test.cpp tests/CMakeLists.txt && git commit -m "feat(script): ScriptWorker — dedicated Python-owning thread + job queue"`

---

### Task 4: `ScriptSystem` threaded pipeline (opt-in)

**Files:**
- Modify: `engine/script/script_system.hpp` (threaded branch + worker member)
- Modify: `tests/unit/script/script_system_test.cpp` (pipeline + latency tests)

- [ ] **Step 1: Write the failing tests** in `tests/unit/script/script_system_test.cpp`
  (add `#include "engine/script/script_worker.hpp"` if needed):

```cpp
TEST_CASE("threaded ScriptSystem applies script writes one tick later", "[script]")
{
    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    mosaic::engine::World world;
    // threaded = true (4th/5th ctor arg per the implemented signature)
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, nullptr, /*threaded=*/true);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    world.add_component<mosaic::engine::script::ScriptComponent>(
        e, mosaic::engine::script::ScriptComponent{
               "def on_update(dt):\n"
               "    t = get_component(entity, 'Transform')\n"
               "    t['position']['x'] += dt\n"
               "    set_component(entity, 'Transform', t)\n"});

    // One-tick pipeline: after N ticks, x == (N-1)*dt.
    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    REQUIRE(world.get_component<mosaic::engine::transform::Transform>(e)->position.x == 0.75f);
}
```

> NOTE: match the final ctor signature you implement. `0.75f == 3 * 0.25f`
> (N=4 ticks → 3 applied). The `Transform` position path mirrors the existing
> inline `script_runtime_test`.

- [ ] **Step 2: Run to verify FAIL** (ctor arity / x != 0.75).

- [ ] **Step 3: Implement in `engine/script/script_system.hpp`.** Add includes:
  `#include "engine/script/script_worker.hpp"` and `#include <memory>`.
  Extend the ctor with a defaulted `bool threaded = false`; when true, construct
  the worker:
```cpp
    ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory,
                 ScriptHost host = {}, mosaic::engine::assets::AssetRegistry* registry = nullptr,
                 bool threaded = false)
        : types_(types), factory_(factory), host_(host), registry_(registry)
    {
        if (threaded)
        {
            worker_ = std::make_unique<ScriptWorker>(factory, host);
        }
    }
```
  Add member: `std::unique_ptr<ScriptWorker> worker_;`.

  In `update()`, branch at the top:
```cpp
    void update(World& world, float dt) override
    {
        if (worker_) { update_threaded(world, dt); return; }
        update_inline(world, dt); // the existing body, renamed
    }
```
  Rename the existing update body to `update_inline`. Add:
```cpp
    void update_threaded(World& world, float dt)
    {
        // 1. Apply the previous job's writes (main thread).
        for (const SnapshotContext::Write& w : worker_->collect())
        {
            types_.set_value(world, mosaic::foundation::EntityID(w.entity), w.comp, w.value);
        }
        // 2. Snapshot every scripted entity's components + resolve its source.
        ScriptWorker::Job job;
        job.dt = dt;
        world.for_each<ScriptComponent>(
            [&](mosaic::foundation::EntityID e, ScriptComponent& sc)
            {
                const std::string source = resolve_source(sc); // asset id -> path -> inline
                if (source.empty()) { return; }
                std::unordered_map<std::string, mosaic::foundation::serialization::Value> comps;
                for (const std::string& name : types_.components_on(world, e))
                {
                    if (auto v = types_.component_value(world, e, name)) { comps[name] = *v; }
                }
                job.snapshot[e.value()] = std::move(comps);
                job.scripts.push_back(ScriptWorker::ScriptRef{e.value(), source});
            });
        // 3. Dispatch (non-blocking).
        worker_->dispatch(std::move(job));
    }
```
  Factor the effective-source resolution (asset id → path → inline) used by
  `update_inline` into a helper `std::string resolve_source(const ScriptComponent& sc)`
  that reads the registry/inline (the file/mtime branch may stay inline-only —
  for threaded v0.1, resolve asset id and inline; if `path` is set with no asset,
  read the file here on the main thread). Keep it simple: asset → file → inline.

> NOTE: the existing `update_inline` keeps its per-entity engine cache + mtime
> logic exactly as-is (do not disturb it). Threaded mode uses its own snapshot
> path and the worker's engine cache.

- [ ] **Step 4: Run to verify PASS** — `ctest --test-dir build/debug -R "one tick later"` → x == 0.75. Full suite green (inline default unchanged).
- [ ] **Step 5: Commit** `git add engine/script/script_system.hpp tests/unit/script/script_system_test.cpp && git commit -m "feat(script): opt-in threaded ScriptSystem (one-tick-deferred pipeline)"`

---

### Task 5: Runtime wiring + `--script-thread` flag

**Files:**
- Modify: `app/run/demo_scene.hpp` (`setup_runtime` gains `bool script_thread = false`)
- Modify: `app/cli/options.hpp` (add `bool script_thread = false;`)
- Modify: `app/main.cpp` (register `--script-thread` flag; pass to `setup_runtime`)
- Modify: `tests/unit/app/options_test.cpp` (flag parses)

- [ ] **Step 1:** `demo_scene.hpp` — add param + forward:
```cpp
inline RuntimeScene setup_runtime(..., mosaic::engine::script::ScriptHost host = {},
                                  bool script_thread = false)
{
    ...
    world.add_system<mosaic::engine::script::ScriptSystem>(
        reflection, std::move(script_factory), std::move(host), &registry, script_thread);
    ...
}
```
  (Insert `script_thread` after the existing `host` param; `main.cpp` passes both.)

- [ ] **Step 2:** `app/cli/options.hpp` — add `bool script_thread = false; // --script-thread` to `Options`.

- [ ] **Step 3:** `app/main.cpp` — where `run` flags are registered
  (near `--window`), add:
```cpp
    run->add_flag("--script-thread", result.options.script_thread,
                  "Run scripts on a background thread (1-frame deferred)");
```
  And pass it through the (single, hoisted) `setup_runtime` call:
```cpp
    setup_runtime(world, asset_registry, types, factory, script_host, options.script_thread)
```

- [ ] **Step 4: Add an options test** in `tests/unit/app/options_test.cpp`
  (mirror an existing flag test): parsing `run x --script-thread` sets
  `options.script_thread == true`; absent → false.

- [ ] **Step 5: Build + full suite** `ctest --test-dir build/debug` → all pass.

- [ ] **Step 6: Manual acceptance.** `<bin> run --script-thread --ticks 5`
  headless → exit 0, `run: done: 5 ticks`. (The demo orbiter animates one frame
  behind; no crash proves the thread starts/joins cleanly.) Also run WITHOUT the
  flag to confirm inline still works.

- [ ] **Step 7: Commit** `git add app/run/demo_scene.hpp app/cli/options.hpp app/main.cpp tests/unit/app/options_test.cpp && git commit -m "feat(app): --script-thread flag opts mosaic run into the script worker"`

---

### Task 6: next-steps doc update

**Files:** Modify `docs/project/next-steps.md`.

- [ ] Mark the script **worker thread — DONE** (opt-in `--script-thread`;
  dedicated Python-owning thread; one-tick-deferred snapshot/command pipeline;
  link this spec/plan). Note the honest caveats (GIL → no parallelism; ~1-frame
  latency; entity-lifecycle unsupported in threaded mode) and future items
  (per-interpreter GILs, dirty snapshots). This completes the scripting bucket.
  Commit: `docs: mark script worker thread done in next-steps`.
