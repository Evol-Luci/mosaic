# foundation/jobs — Design

**Issue:** [#34 foundation/jobs: task/job system for parallel work](https://github.com/Evol-Luci/mosaic/issues/34)
**Status:** Accepted
**Date:** 2026-08-17

## Purpose

A minimal task/job system in the `foundation/` tier: a thread pool for submitting
work and a `parallel_for` for data-parallel loops. It is the eventual scheduler that
`World::tick` will use to run systems concurrently (honoring each `System`'s
read/write access sets from #9) — but that wiring is a separate engine follow-up;
this issue delivers the primitive only.

## Scope

- A thread pool: submit a void task, get a waitable `std::future<void>`.
- A `parallel_for(begin, end, body)` primitive for data-parallel work.
- Deterministic completion semantics (wait/join-all).
- Clean shutdown — no leaked or detached threads.
- `foundation/` tier, dependency-free (standard library only).

## Out of scope (future)

- Work-stealing, job dependencies/graphs, fibers, priority scheduling.
- **Wiring the pool into `World::tick`** (parallel system execution honoring
  `ComponentAccess`) — a separate engine follow-up once this exists. Today systems
  run single-threaded in registration order, by design.
- Returning task results (`std::future<T>` for non-void tasks). v0.1 tasks are
  `void`; results are communicated through captured state observed after wait.

## Design decisions

### 1. `std::future<void>` as the waitable handle

`submit` wraps the task in a `std::packaged_task<void()>` and returns its
`std::future<void>`. This is the standard-library path: minimal code, correctness
is easy to get right, and exception propagation is built in (a throwing task is
captured into the future and rethrown on `.get()`). It matches the "minimal task
system" scope. A custom lightweight handle / wait-group is the post-v0.1 evolution
if per-task heap shared-state ever shows up in a profile.

### 2. `parallel_for` blocks, using an internal latch (not N futures)

`parallel_for` splits the range into ~`thread_count` chunks and **blocks until all
complete**, which is the deterministic completion the issue calls for. Completion is
tracked by a lightweight **latch** — an atomic remaining-count plus a
`condition_variable` — rather than collecting N `std::future`s, avoiding per-chunk
heap shared-state on the hot data-parallel path.

### 3. Compiled `mosaic_jobs` static library

A thread pool needs out-of-line thread/worker definitions, so jobs is a compiled
static library (mirroring `mosaic_serialization`), not a header-only INTERFACE
module. It links `mosaic_foundation` and the system threads library; it pulls in no
third-party dependency.

### 4. Clean shutdown drains the queue

The destructor stops accepting new work, lets workers **drain the already-queued
tasks**, then joins every thread. This guarantees every submitted future is
fulfilled before threads exit (no future left hanging) and no thread is detached or
leaked.

## Architecture

One unit: `foundation/jobs/thread_pool.hpp` + `foundation/jobs/thread_pool.cpp`,
namespace `mosaic::foundation::jobs`.

### `ThreadPool`

```cpp
class ThreadPool
{
public:
    // Spins up `thread_count` workers, clamped to >= 1. Default: hardware_concurrency().
    explicit ThreadPool(unsigned thread_count = std::thread::hardware_concurrency());

    // Drains the queue, joins all workers. No detached/leaked threads.
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a task; returns a future fulfilled when it finishes (or carrying the
    // task's exception, rethrown on get()).
    std::future<void> submit(std::function<void()> task);

    // Split [begin, end) into ~thread_count chunks (respecting `grain`), run `body`
    // over each index across the pool, and block until all complete. Empty/inverted
    // range is a no-op. An exception thrown by any body is rethrown from the calling
    // thread after all chunks finish.
    void parallel_for(std::size_t begin, std::size_t end,
                      const std::function<void(std::size_t)>& body, std::size_t grain = 1);

    unsigned thread_count() const noexcept;

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              stopping_ = false;
};
```

**Worker loop:** wait on `cv_` until a task is available or `stopping_`; pop and run
the task; repeat. Exit only when `stopping_` **and** the queue is empty (drain).

**`parallel_for`:** compute `chunk_count = min(thread_count, ceil((end-begin)/grain))`;
partition `[begin,end)` into that many contiguous sub-ranges. Use a stack latch
`struct { std::atomic<std::size_t> remaining; std::mutex m; std::condition_variable cv;
std::exception_ptr error; }` (the calling frame outlives the chunks because it
blocks). Each chunk task runs `body(i)` for its sub-range inside a `try/catch`; the
first exception is stored in `error` under the latch mutex; on completion the chunk
decrements `remaining` and notifies. `parallel_for` waits until `remaining == 0`,
then rethrows `error` if set.

## Data flow

```
submit(task)
  -> packaged_task<void()> pt(task); future f = pt.get_future()
  -> lock; tasks_.push(move(pt)); unlock; cv_.notify_one()
  -> return f                          worker: pop -> run -> (future fulfilled)

parallel_for(begin,end,body,grain)
  -> partition into K chunks; latch{ remaining=K }
  -> for each chunk: submit-internal { try body(i..) catch store err; if(--remaining==0) notify }
  -> wait until remaining==0
  -> if latch.error: rethrow
```

## Error handling

- **Submitted task throws** → captured by `std::packaged_task` into the future;
  rethrown on `.get()`. `submit` never throws from the task body.
- **`parallel_for` body throws** → caught per chunk; the first exception is stored
  and **rethrown from the calling thread** after all chunks finish (the latch still
  completes, so no worker terminates and no chunk is abandoned).
- **Empty / inverted range** (`begin >= end`) → no-op, returns immediately.
- **`thread_count == 0`** → clamped to 1 (a pool always has a worker, so futures
  cannot hang).
- **Reentrancy** (documented precondition, not enforced in v0.1): `parallel_for`
  must be called from a non-worker thread. Calling it from inside a running job can
  deadlock — the caller blocks on the latch while its chunks queue behind it with no
  free worker. The eventual scheduler will handle nested parallelism.

## Testing

`tests/unit/jobs/`, tag `[jobs]`:

- **submit runs all** — submit N tasks each incrementing a `std::atomic<int>`; wait
  all futures; assert the counter == N.
- **future orders a side effect** — a value written by a task is observed after that
  task's `future.wait()`.
- **submitted task exception** — `future.get()` rethrows the task's exception.
- **parallel_for coverage** — over `[0,N)`, each index writes its own slot in a
  size-N vector; after return every slot is set exactly once.
- **parallel_for empty range** — `begin >= end` invokes the body zero times and
  returns.
- **parallel_for body exception** — a body that throws surfaces from the
  `parallel_for` call on the calling thread.
- **clean shutdown** — submit many tasks into a scoped pool, let it destruct; assert
  all ran (counter) and the destructor returns (no hang).
- **single-thread pool** — `ThreadPool(1)` completes both submit and parallel_for.
- **concurrency stress** — `parallel_for` over a large N with each item touching a
  distinct slot; assert full, correct coverage (a genuine parallel exercise, race-free
  by construction since slots are disjoint).

## CMake

- New `foundation/jobs/CMakeLists.txt`:
  `add_library(mosaic_jobs STATIC thread_pool.cpp)`, `target_include_directories
  PUBLIC ${CMAKE_SOURCE_DIR}`, `target_link_libraries PUBLIC mosaic_foundation
  PRIVATE Threads::Threads`, `target_compile_features PUBLIC cxx_std_20`.
- `foundation/CMakeLists.txt`: `add_subdirectory(jobs)`.
- Root `CMakeLists.txt`: `find_package(Threads REQUIRED)` (once).
- `tests/CMakeLists.txt`: link `mosaic_jobs` into `mosaic_unit_tests`; add
  `unit/jobs/thread_pool_test.cpp`.

## Acceptance criteria (from #34)

- [ ] Submit N tasks; all run and complete; results/side-effects observed after wait.
- [ ] `parallel_for` splits a range across the pool and completes deterministically.
- [ ] Clean shutdown (no leaked/detached threads).
- [ ] Unit tests under `tests/unit/`.

## Tier / dependency contract

`foundation/jobs` depends only on the standard library (`<thread>`, `<mutex>`,
`<condition_variable>`, `<future>`, `<functional>`, `<queue>`, `<vector>`,
`<atomic>`) and the system threads library — nothing else in `foundation/`, nothing
above it. It is consumed later by the runtime/engine scheduler (a separate
follow-up).
