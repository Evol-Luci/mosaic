# Script Worker Thread (async, one-tick-deferred) — Design

## Problem

Scripts run inline inside `World::tick` on the main thread, so a slow
`on_update` stalls the whole frame (render + physics wait on it). We want script
execution **off the main loop** so a heavy script never stalls rendering.

CPython has a single GIL and the backend is single-threaded, so **true parallel
script execution is impossible with one interpreter**. The achievable win is
*decoupling*: run scripts asynchronously on a dedicated thread and apply their
results at a safe sync point. That inherently costs ~1 frame of result latency
(agreed with the maintainer: uniform 1-frame latency, whole-system, opt-in).

## Goal

An **opt-in** threaded mode for `ScriptSystem`: script `on_update`s run on a
dedicated Python-owning worker thread against an immutable snapshot and emit a
component-write batch the main thread applies one tick later. Inline mode stays
the **default** — zero change to existing behaviour/tests.

## Non-goals (v0.1)

- Parallelism across scripts (GIL forbids it).
- Same-frame writes / per-script inline override (reintroduces the races we
  avoid; a clean future addition).
- Entity lifecycle (`create_entity`/`destroy_entity`) from a threaded script —
  those touch `World` directly and would race; **unsupported in threaded mode**
  (documented; `get_component`/`set_component` are the supported data path).

## Architecture

**One dedicated worker thread owns all Python.** The interpreter and every
per-entity engine live on the worker; the main thread never calls into Python.
The GIL simply stays on the worker — no `gil_scoped_release`/`acquire` dance.

### One-tick pipeline (`ScriptSystem::update`, main thread)

```
1. If a job from the previous tick is in flight: wait for it, take its write
   batch, and apply each write to World (reflection.set_value).
   (Waiting bounds latency to exactly 1 tick and is deterministic; it only
    actually blocks if a script overran a whole frame — the degenerate case.)
2. Build snapshot S: for each scripted entity, marshal every component
   (reflection.components_on -> component_value) into Values, plus its script
   source + dt.
3. Dispatch S to the worker (non-blocking) and return.
```

Trace (`x += dt`): tick 1 applies nothing, snapshots x=0, dispatches; worker
writes x=dt; tick 2 applies x=dt, snapshots x=dt, dispatches; … after N ticks
`x == (N-1)*dt` — exactly one tick behind inline. Self-accumulation is correct
(a script always reads back its own last write, since step 1 applies it before
step 2 snapshots).

Ordering vs physics is preserved: script writes apply at the top of the tick
(before `PhysicsSystem`), so a physics write-back still wins on a shared
component — identical to inline, one frame delayed.

### Components

1. **`engine/script/script_context.hpp` (new)** — the sandbox seam:

```cpp
struct ScriptContext
{
    virtual ~ScriptContext() = default;
    virtual std::optional<Value> get(EntityID e, std::string_view comp) = 0;
    virtual bool set(EntityID e, std::string_view comp, const Value& v) = 0;
};

// Reads an immutable snapshot; buffers writes. Owned by the worker per job.
class SnapshotContext : public ScriptContext
{
public:
    using Snapshot = std::unordered_map<std::uint64_t,
                        std::unordered_map<std::string, Value>>;
    struct Write { std::uint64_t entity; std::string comp; Value value; };

    explicit SnapshotContext(const Snapshot& snap) : snap_(snap) {}
    std::optional<Value> get(EntityID e, std::string_view comp) override;
    bool set(EntityID e, std::string_view comp, const Value& v) override; // appends writes_
    const std::vector<Write>& writes() const { return writes_; }
private:
    const Snapshot& snap_;
    std::vector<Write> writes_;
};
```

2. **`ScriptEngine::bind_context(ScriptContext&)`** — installs
   `get_component`/`set_component` routed through the context (no
   entity-lifecycle globals in this mode). Non-pure (default no-op) so other
   impls/mocks are unaffected. The Python backend overrides it: same
   `value_to_py`/`py_to_value` marshaling as `bind_world`, but reading/writing
   the context instead of `World`.

3. **`engine/script/script_worker.hpp` + `.cpp` (new)** — `ScriptWorker`:
   a single `std::thread` with a mutex/condvar job queue. Owns the engines
   (`unordered_map<uint64, {unique_ptr<ScriptEngine>, source}>`), created via
   the injected factory **on the worker thread** (so the interpreter/GIL live
   there). Interface (main-thread-safe):

```cpp
class ScriptWorker
{
public:
    using Job = { SnapshotContext::Snapshot snapshot; float dt;
                  std::vector<{uint64 entity, std::string source}> scripts; };
    explicit ScriptWorker(ScriptSystem::EngineFactory factory, ScriptHost host = {});
    ~ScriptWorker(); // stops + joins the thread

    void dispatch(Job job);                       // enqueue (non-blocking)
    std::vector<SnapshotContext::Write> collect(); // wait for the in-flight job, return its writes
    bool has_pending() const;
};
```
   Per job the worker: recompiles engines whose source changed (as
   `ScriptSystem` does today), binds a fresh `SnapshotContext` + `bind_host`,
   `set_global("entity"/"dt")`, runs `on_update(dt)`, and accumulates all
   contexts' writes into one batch.

4. **`engine/script/script_system.hpp`** — a threaded mode. A new ctor path
   (defaulted `bool threaded = false`) constructs a `ScriptWorker` (owning a
   copy of the factory/host/registry-derived source resolution). `update()`
   branches: **inline** = today's code unchanged; **threaded** = the pipeline
   above (collect+apply previous, snapshot, dispatch). Snapshot resolves each
   entity's effective source with the existing precedence (asset id → path →
   inline) on the **main thread** (reads World/registry safely), passing the
   resolved source string to the worker.

5. **`app/run/demo_scene.hpp`** — `setup_runtime` gains a defaulted
   `bool script_thread = false`; forwards it to the `ScriptSystem` ctor.

6. **`app/cli/options.hpp` + `app/main.cpp`** — a `--script-thread` flag on
   `mosaic run`; when set, `setup_runtime(..., script_thread=true)`. Default
   off. Gives a real headless acceptance path.

## Testing (all headless, deterministic)

1. **`SnapshotContext`** — `get` returns snapshot values / `nullopt` for
   absent; `set` buffers a `Write`; multiple sets accumulate in order.
2. **Engine `bind_context`** — bind a capturing/stub context, run a script that
   `get`s then `set`s; assert the context saw the get and recorded the set
   (Python routes through the context, not World).
3. **`ScriptWorker`** — construct with the Python factory; `dispatch` a job
   whose script increments a Transform's `x`; `collect()` returns a batch with
   the expected write. (Deterministic: `collect` waits for the job.)
4. **`ScriptSystem` threaded pipeline** — an entity with `x += dt`; after N
   `world.tick`s, `x == (N-1)*dt` (one-tick latency, exact). And the same
   script inline yields `N*dt` — proving the modes differ only by the frame.
5. **Default unchanged** — inline mode (default ctor) is byte-for-byte today's
   behaviour; all existing script/runtime tests stay green.
6. **Manual acceptance** — `mosaic run --script-thread --ticks N` (headless)
   exits 0; the default demo's orbiter still animates (one frame behind).

## Risks / mitigations

- **Thread lifetime / teardown** — `ScriptWorker`'s dtor sets a stop flag,
  notifies, and **joins**; no detached thread, no use-after-free (mirrors
  `ThreadPool`'s drain-then-join contract).
- **Interpreter thread affinity** — all engine create/use is on the worker
  thread; the main thread never calls Python. `ensure_interpreter()` runs on the
  worker at first engine creation. In a process that *also* runs inline engines
  (main-thread GIL), mixing is a hazard — so a threaded `ScriptSystem` must be
  the only script driver in that run (it is: `setup_runtime` adds exactly one).
- **Snapshot cost** — one Value marshal per component per scripted entity per
  tick. Acceptable for v0.1 scene sizes; noted as a future optimization
  (dirty-tracking / partial snapshots).

## Out of scope / follow-ups

- Non-blocking "apply whatever's ready" (variable >1-frame latency).
- Per-interpreter GILs (PEP 684) for real parallelism.
- Partial/dirty snapshots; script→script read-set narrowing.
