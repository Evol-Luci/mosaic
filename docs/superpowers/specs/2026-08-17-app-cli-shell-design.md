# `mosaic run` App / CLI Shell — Design

**Issue:** [#35 App / CLI shell (mosaic run): real-time entry point driving RuntimeLoop](https://github.com/Evol-Luci/mosaic/issues/35)
**Status:** Accepted
**Date:** 2026-08-17

## Purpose

The process entry point that finally **runs a world outside a test**. The headless
engine core is complete — `World`, systems, `Scene` load/save, assets, transform,
and the `RuntimeLoop` (#22) — but nothing owns a `main` or a real clock. This is
the smallest next step to a running engine and the first thing that produces
observable output.

## Scope

- A `main` / CLI: `mosaic run [--scene <file>] [--duration <s>] [--ticks <n>]
  [--fps <n>] [--fixed-hz <n>] [--json]`.
- Construct a `World`, register systems (`TransformSystem`), optionally load a
  scene (#19), and drive a `RuntimeLoop` (#22).
- The **real-time frame pump** the runtime loop deliberately left out: measure
  wall-clock `dt` with `std::chrono`, call `RuntimeLoop::advance(world, dt)`, sleep
  to a target frame rate.
- Graceful stop on SIGINT (Ctrl-C); `--duration` / `--ticks` bound a headless run
  for scripted use.
- `--json` machine-readable status (periodic progress + a final summary) on
  stdout, alongside human-readable status through the logging facility (#33).
- A new `app/` target wired into CMake.

## Out of scope (future)

- `mosaic create/editor/build`, project generator, asset compiler (later Tools
  issues). The CLI is structured (CLI11) so these subcommands slot in later.
- Any rendering / window — headless for now.
- Parallel/threaded ticking (jobs #34).

## Dependencies

- **Depends on** #22 (`RuntimeLoop`), #9 (`World`). Uses #19 (`Scene` load) and
  #21 (`TransformSystem`). **Uses #33 logging** for human-readable status — #33 is
  in [PR #39](https://github.com/Evol-Luci/mosaic/pull/39), so this implementation
  rebases on that once merged.

## Design decisions

### 1. Two targets: a `mosaic_app` library + a thin `mosaic` executable

`main.cpp` is the one part that cannot be unit-tested — it owns the real
`std::chrono` clock, the SIGINT handler, and stdout. Everything else (arg parsing,
the frame-pump loop, status reporting) lives in a `mosaic_app` static library
behind test seams. `main.cpp` is thin wiring over that library, and the unit tests
link `mosaic_app`. This is the mechanism that satisfies the acceptance criterion
"frame-pump logic unit-tested with an injected clock (no real sleeps in tests)".

### 2. CLI11 for argument parsing, confined to one translation unit

The flag set is small now but the roadmap adds `mosaic create/editor/build`
subcommands. CLI11 (header-only, FetchContent) gives `--help`, validation, and
subcommand support for free. It is confined to `app/cli/options.cpp`: parsing
produces a plain `Options` struct, so nothing downstream — including the tested
frame pump — depends on CLI11. The `foundation/` and `engine/` tiers stay
dependency-free; only the app tier gains CLI11.

### 3. `--json` emits periodic progress + a final summary

Headless with no rendering, per-frame detail is noise, but a long scripted run
wants progress. So `--json` emits a periodic `{"event":"progress", ...}` line on a
fixed cadence plus a final `{"event":"summary", ...}` line. Emission is driven from
inside the frame pump via a `StatusReporter` seam (not direct stdout), so it is
deterministic under the injected clock and capturable in tests.

### 4. Shared `register_builtin_components` engine helper

`Scene::load_from_json` needs a `ComponentReflection` with component types
registered by name, or it throws "unknown component type". Nothing registers
built-in components today. A small engine-tier `register_builtin_components(
ComponentReflection&)` registers `Transform` (and future components) in one place,
so the app and the future Studio editor (#37) cannot drift.

### 5. Injected clock / sleep / stop / reporter

The frame pump takes its time source (`now()`), its `sleep(duration)`, its
`StopCondition`, and its `StatusReporter&` as injected dependencies. Production
wires a `steady_clock` and a real sleep; tests wire a fake clock, a no-op sleep,
and a capturing reporter — every test runs in zero real time and is fully
reproducible.

## Architecture

New top-level `app/`. Two CMake targets: `mosaic_app` (library) and `mosaic`
(executable). Namespace `mosaic::app`.

### `app/cli/options.{hpp,cpp}`

```cpp
struct Options
{
    std::optional<std::string>   scene;        // --scene <file>
    std::optional<double>        duration_s;   // --duration <s>
    std::optional<std::uint64_t> ticks;        // --ticks <n>
    bool                         json = false; // --json
    double                       fps = 60.0;   // --fps  (pump wake target)
    double                       fixed_hz = 60.0; // --fixed-hz (sim timestep)
};

// Parse argv with CLI11. Returns the parsed Options, or a parse-exit code the
// caller returns from main (CLI11 already printed help/error to stderr).
struct ParseResult { Options options; std::optional<int> exit_code; };
ParseResult parse(int argc, char** argv);
```

`exit_code` is set when CLI11 handled `--help` or an arg error; `main` returns it
directly. Otherwise `options` is used.

### `engine/reflection/builtin_components.hpp`

```cpp
inline void register_builtin_components(
    mosaic::engine::reflection::ComponentReflection& registry)
{
    registry.register_component<mosaic::engine::transform::Transform>("Transform");
    // future: Camera, Mesh, ...
}
```

### `app/run/status_reporter.hpp`

```cpp
struct RunSummary
{
    std::uint64_t ticks;
    double        elapsed_s;
    double        fixed_dt;
    std::string   scene;        // "" if none
    std::string   stopped_by;   // "signal" | "duration" | "ticks"
};

class StatusReporter
{
public:
    virtual ~StatusReporter() = default;
    virtual void progress(std::uint64_t tick, double elapsed_s) = 0;
    virtual void summary(const RunSummary&) = 0;
    virtual void error(const std::string& message) = 0;
};
```

`JsonStatusReporter` (in `status_reporter.cpp`, since it uses nlohmann) writes one
JSON object per line to stdout for `progress`/`summary`/`error`, and mirrors a
human-readable line through `mosaic::foundation::log`. A `TextStatusReporter`
(no `--json`) logs only. Tests use a `CapturingStatusReporter` recording calls.

### `app/run/frame_pump.hpp` (header-only)

```cpp
// Injected time source. Duration/time-point are plain doubles (seconds) at this
// seam so tests need no chrono. main adapts steady_clock into these.
struct PumpDeps
{
    std::function<double()>      now;    // monotonic seconds
    std::function<void(double)>  sleep;  // sleep this many seconds (no-op in tests)
    std::function<bool()>        stop_requested; // SIGINT flag; false in headless tests
};

class FramePump
{
public:
    FramePump(engine::runtime::RuntimeLoop& loop, PumpDeps deps,
              StatusReporter& reporter, Options options);

    // Drive world in real time until a stop source fires. Returns the summary.
    RunSummary run(engine::world::World& world);
};
```

Loop body per iteration:
1. `t = now()`; `real_dt = t - t_prev` (clamped `>= 0`); `t_prev = t`.
2. `loop.advance(world, real_dt)` (fixed-timestep ticks happen inside).
3. If a progress cadence boundary is crossed (every `progress_interval` sim
   seconds, default 1.0), `reporter.progress(loop.tick_count(), elapsed)`.
4. Check stop sources: `stop_requested()` → `signal`; `duration_s` and
   `elapsed >= duration_s` → `duration`; `ticks` and
   `loop.tick_count() >= ticks` → `ticks`. On any, break.
5. Else `sleep(max(0, target_frame_period - (now() - t)))` where
   `target_frame_period = 1.0 / fps`.

`loop` is constructed with `fixed_dt = 1.0 / fixed_hz`. The pump calls
`loop.start()` before the loop.

### `app/main.cpp`

Thin wiring, top to bottom:
1. `auto pr = parse(argc, argv); if (pr.exit_code) return *pr.exit_code;`
2. Build `World`; `world.add_system<TransformSystem>()`;
   `ComponentReflection types; register_builtin_components(types);`
3. If `options.scene`: read the file into a string (filesystem I/O lives here);
   on read failure report + return non-zero; else
   `try { scene::load_from_json(text, world, types); } catch (SerializationError&)
   { report + return non-zero; }`
4. `RuntimeLoop loop(1.0 / options.fixed_hz);`
5. Install SIGINT handler setting a `static std::atomic<bool> g_stop`.
6. Build real `PumpDeps` over `steady_clock` (now → seconds) and
   `std::this_thread::sleep_for`, `stop_requested = []{ return g_stop.load(); }`.
7. Choose reporter (`JsonStatusReporter` if `--json` else `TextStatusReporter`).
8. `FramePump pump(loop, deps, reporter, options); auto s = pump.run(world);`
9. `reporter.summary(s); return 0;`

## Data flow

```
argv --parse--> Options
World + TransformSystem + register_builtin_components(types)
  (optional) scene file --read--> string --load_from_json(types)--> populated World
RuntimeLoop(fixed_dt) --+
main clock/sleep/signal --> PumpDeps --+--> FramePump.run(world)
                                       |     loop: now -> advance -> progress? -> stop? -> sleep
Options (--json) ----------------------+--> StatusReporter (progress/summary lines)
                                             --> RunSummary --> exit 0
```

## Error handling

The app is a process boundary: failures become exit codes, not escaping
exceptions.

- **Bad args / `--help`** — CLI11 prints to stderr and yields an exit code that
  `main` returns (help → 0, error → non-zero).
- **`--scene` unreadable** — `main` reports via the reporter's `error` (and a
  `{"event":"error"}` JSON line under `--json`) and returns non-zero.
- **Malformed scene / unknown component** — `load_from_json` throws
  `SerializationError`; `main` catches, reports, returns non-zero.
- **Clean shutdown** — on SIGINT the pump finishes the in-flight `advance`, breaks,
  and returns a summary with `stopped_by == "signal"`; `main` returns 0. Reaching
  `--duration`/`--ticks` likewise returns 0.
- **Signal safety** — the handler only stores to `std::atomic<bool>`; no work runs
  in the handler.

## Testing

All logic is in `mosaic_app` behind seams; tests link it into the existing
`mosaic_unit_tests` executable under `tests/unit/app/`, plus one engine test.

- **`parse` — `[cli]`** — each flag maps to the right `Options` field; defaults
  (`fps == 60`, `fixed_hz == 60`, no `scene`/`duration`/`ticks`, `json == false`);
  `--ticks`/`--duration`/`--json` set their fields; an unknown flag yields a
  non-empty `exit_code`.
- **`FramePump` — `[app]`** — fake clock (a scripted sequence of seconds), no-op
  sleep, `CapturingStatusReporter`:
  - `--ticks N` → runs exactly N ticks, `stopped_by == "ticks"`, deterministic.
  - a clock schedule crossing `--duration` → stops, `stopped_by == "duration"`.
  - `stop_requested` returning true → stops, `stopped_by == "signal"`.
  - progress fires at the expected cadence and tick values.
  - `RunSummary` `ticks`/`elapsed_s`/`stopped_by`/`fixed_dt` correct.
  - the injected sleep is a counter that asserts the pump never sleeps a negative
    duration and (in a `--ticks` burst with a zero-cost clock) the loop still
    terminates — no reliance on real time.
- **`register_builtin_components` — `[reflection]`** — after registering, a scene
  JSON containing a `Transform` round-trips through `load_from_json` without
  throwing; an unregistered type still throws (guard remains intact).

**Not unit-tested:** `main.cpp` (real `std::chrono`, SIGINT, stdout) — kept thin
and exercised by a manual smoke run (`mosaic run --ticks 10 --json`).

## CMake

- New `app/CMakeLists.txt`:
  - FetchContent CLI11 (in the root `CMakeLists.txt` alongside the others).
  - `add_library(mosaic_app STATIC app/cli/options.cpp app/run/status_reporter.cpp)`
    linking `mosaic_engine`, `mosaic_foundation`, `mosaic_serialization`,
    `CLI11::CLI11`, `nlohmann_json::nlohmann_json`; `cxx_std_20`.
  - `add_executable(mosaic app/main.cpp)` linking `mosaic_app`.
- Root `CMakeLists.txt`: `add_subdirectory(app)` after `engine`.
- `tests/CMakeLists.txt`: link `mosaic_app` into `mosaic_unit_tests` and add the
  new `unit/app/*.cpp` + `unit/reflection/builtin_components_test.cpp` sources.

## Acceptance criteria (from #35)

- [ ] `mosaic run` builds a `World`, registers a system, and ticks it in real time
      via `RuntimeLoop` until stopped.
- [ ] `--scene <file>` loads an entity/component graph (#19) before running.
- [ ] `--duration` / `--ticks` bound a headless run; process exits cleanly.
      (`--ticks` is the exact deterministic bound; `--duration` bounds wall-time.)
- [ ] Frame-pump logic unit-tested with an injected clock (no real sleeps in
      tests).
- [ ] A new `app/` target wired into CMake.

## Tier / dependency contract

`app/` is a new tier above `engine/`: it may depend on `engine/` and
`foundation/` and on app-only third-party libraries (CLI11, nlohmann). Nothing in
`engine/` or `foundation/` depends on `app/`. The one engine-tier addition
(`register_builtin_components`) depends only on existing engine reflection +
transform, adding no third-party dependency.
