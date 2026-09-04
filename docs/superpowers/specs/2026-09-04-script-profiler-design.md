# Script Profiler (per-`on_update` timing) — Design

## Problem

No visibility into script cost — a slow `on_update` can't be spotted. Phase 3
wants a profiler; the tractable, headless-measurable first cut is **per-entity
`on_update` wall-time**.

## Goal

`ScriptSystem` accumulates, per scripted entity, how long its `on_update` takes
(call count, total, max), and exposes it via a C++ `profile()` query. A
`mosaic run --script-profile` flag dumps the table at run end.

## Design

### `ScriptSystem` (`engine/script/script_system.hpp`)

- A result row:
```cpp
struct ScriptProfileEntry
{
    std::uint64_t entity;
    std::uint64_t calls;
    double        total_ms;
    double        max_ms;
};
```
- The per-engine `Entry` gains `std::uint64_t calls = 0; double total_ms = 0.0; double max_ms = 0.0;` (persist across recompiles — the map entry survives an engine swap; reset only when the entity is pruned).
- In `update_inline`, time the `on_update` call with `std::chrono::steady_clock`:
```cpp
const auto t0 = clock::now();
entry.engine->run_string("on_update(dt)");
const double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
entry.calls++; entry.total_ms += ms; entry.max_ms = std::max(entry.max_ms, ms);
```
- `std::vector<ScriptProfileEntry> profile() const` — one row per engine with `calls > 0`.

Inline mode only (the worker's `on_update` isn't timed here — a follow-up).
Overhead is two `clock::now()` calls per entity per tick (negligible); kept
always-on for simplicity.

### CLI — `app/cli/options.hpp` + `app/main.cpp`

- `Options` gains `bool script_profile = false; // --script-profile`.
- `run` registers `--script-profile`.
- After `pump.run`, if set: `world.get_system<ScriptSystem>()->profile()` is
  logged (one line per entity: `entity N: C calls, total T ms, max M ms`),
  sorted by `total_ms` descending so hotspots surface first.

## Testing (headless)

1. **`profile()` accounting** (`script_profiler_test.cpp`): a scripted entity
   with a small `on_update` loop; after N ticks, `profile()` has one row with
   `entity == e`, `calls == N`, `total_ms >= 0`, `max_ms >= 0`. (Assert the
   deterministic call count, not wall-time magnitudes.)
2. **Empty before ticks**: `profile()` is empty before any tick.
3. **Flag parses** (`options_test`): `run x --script-profile` sets the flag; absent ⇒ false.

Manual: `mosaic run --script-profile --ticks 60` prints the timing table and exits 0.

## Out of scope / follow-ups

- Per-**script-source** aggregation (group entities sharing a script).
- Threaded-mode profiling; `on_event` timing; per-line/`cProfile` integration.
- An editor panel view (the C++ `profile()` API is the seam it'll use).
