# External .py-file script hot-reload (design)

**Phase:** 3 (Programming) — second hot-reload slice (after the in-editor Script Editor).
**Builds on:** S4 `ScriptComponent`/`ScriptSystem` (recompile-on-source-change), S1 `ScriptEngine`.
**Date:** 2026-09-03.

> Built autonomously (user authorized unattended roadmap work). The core is headless-testable; the
> "edit in your own editor, watch it live in Studio" payoff is the visual manifestation of the same
> mechanism (user confirms visually on return).

## Summary

A `ScriptComponent` can reference an external **`.py` file**; `ScriptSystem` loads the script from
that file, **polls its modification time each tick, and reloads (recompiles) it when the file
changes on disk** — so you edit the script in your own editor (vim/VSCode/…), save, and the running
world picks up the change without a restart. The inline `source` stays the fallback when no path is
set. This is the classic hot-reload workflow, built on S4's existing recompile-on-change seam.

## Scope

**In:**
- `ScriptComponent` gains an optional `std::string path` (a `.py` file). Reflected, so a
  file-referenced script serializes with the scene and round-trips.
- `ScriptSystem`: when `path` is non-empty, the effective source is the **file contents**, and the
  file's mtime is polled each tick; a changed mtime → reload the file → recompile (S4's seam). When
  `path` is empty, the inline `source` is used (current behavior, unchanged).

**Deferred (documented):**
- An **OS file-watcher** (inotify) instead of mtime polling — polling is simplest, cross-platform,
  and cheap for a handful of scripted entities; a watcher is a later optimization.
- **`.py` as an asset** (import a `.py` under `assets/`, reference it by `AssetID`, cook/bundle it) —
  this slice uses a plain filesystem `path`; asset-managed scripts + path-resolution-relative-to-the-
  project are a follow-up.
- Editing the *file* from the in-editor Script Editor (which edits inline `source`) — the two
  coexist (inline scripts use the editor; file scripts use external editing + this reload).

## Architecture

### 1. `ScriptComponent` (`engine/script/script_component.hpp`)

```cpp
struct ScriptComponent
{
    std::string source; // inline Python (used when `path` is empty)
    std::string path;   // optional .py file; when set, the script loads from it and hot-reloads
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
    visitor.member("path", c.path);
}
```
Both are `std::string` → reflect to `field_readonly` in the inspector and serialize via #10; a
`.py`-referenced script (path set, source empty) round-trips through scene save/load.

### 2. `ScriptSystem` (`engine/script/script_system.hpp`)

The per-entity `Entry` cache grows to track the file path + last-seen mtime; `update()` resolves the
**effective source** before the existing recompile check:

```cpp
struct Entry
{
    std::unique_ptr<ScriptEngine>   engine;
    std::string                     source; // last-compiled source
    std::string                     path;   // last-seen path (detect a path change / switch to inline)
    std::filesystem::file_time_type mtime{}; // last-seen file mtime (detect an on-disk edit)
};
```

In `update()`, per scripted entity, compute `desired` (the source to run this tick):

```cpp
std::string desired;
if (sc.path.empty())
{
    entry.path.clear();          // switched back to inline
    desired = sc.source;
}
else
{
    std::error_code ec;
    const auto mtime = std::filesystem::last_write_time(sc.path, ec);
    const bool reload = !ec && (!entry.engine || entry.path != sc.path || mtime != entry.mtime);
    if (reload)
    {
        std::ifstream f(sc.path, std::ios::binary);
        if (f)
        {
            desired = std::string(std::istreambuf_iterator<char>(f), {});
            entry.path  = sc.path;
            entry.mtime = mtime;
        }
        else { desired = entry.source; } // open failed -> keep last-good (retry next tick)
    }
    else { desired = entry.source; }     // no change / missing file -> keep what we have
}

if (!entry.engine || entry.source != desired)
{
    entry.engine = factory_();
    entry.source = desired;
    entry.engine->bind_world(world, types_);
    entry.engine->set_global("entity", Value(uint64(e.value())));
    entry.engine->run_string(desired);   // (re)compile: define on_update + top-level
}
// ... unchanged: set_global("dt"), run_string("on_update(dt)")
```

The recompile itself is S4's existing mechanism — this slice only changes *where the source comes
from* and *what triggers a recompile* (an on-disk edit, in addition to a source-string change).
`ScriptSystem` stays Python-free (`std::filesystem`/`std::ifstream` are fine in the engine tier).

## Data flow

```
ScriptComponent{ path = "behaviors/mover.py" }
tick:  last_write_time(path) -> mtime
       mtime changed (or first sight) ? read file -> desired = contents (update entry.mtime)
                                       : desired = cached source
       desired != last-compiled ? recreate engine + run_string(desired)   [S4 recompile]
       run_string("on_update(dt)")
edit mover.py in your editor + save  ->  next tick sees a new mtime  ->  reloads  ->  new behavior live
```

## Error handling

- **Missing / unreadable file:** `last_write_time` error or `ifstream` open failure → keep the
  last-compiled source (a running script isn't broken by a transient save); on first sight with no
  file, the effective source is empty → `on_update` undefined → non-fatal (entity idle) until the
  file appears. Never throws.
- **Syntax error in the file** (incl. a partial mid-save read): the recompile's `run_string(desired)`
  fails → `on_update` undefined → the per-tick call is a captured error, ignored (S4's non-fatal
  contract). The next valid save (new mtime) recompiles cleanly.
- **Switching path → inline (or vice-versa):** `entry.path` is cleared when `path` is empty and
  compared when set, so a switch recompiles with the new effective source.
- Per-entity reload resets script state (the recompile recreates the engine) — expected.

## Testability

Headless, deterministic (`tests/unit/script/script_hotreload_test.cpp`, links the CPython backend):
- **Loads from a file:** write a `.py` that sets `Transform.position.x = 1.0` in `on_update`; an
  entity with `ScriptComponent{ path }`; a `ScriptSystem`; `world.tick` → `position.x == 1.0f`.
- **Hot-reloads on edit:** after the above, rewrite the file to set `x = 2.0`, and bump its mtime
  explicitly (`std::filesystem::last_write_time(path, now + 2s)` — deterministic regardless of
  filesystem timestamp resolution); `world.tick` → `position.x == 2.0f`.
- **Missing file is non-fatal:** `ScriptComponent{ path = "does/not/exist.py" }`; `world.tick` does
  not throw and leaves the entity's Transform unchanged.
- **Reflection round-trip:** `component_value`/`set_value` (or a scene save/load) round-trips a
  `ScriptComponent` with a non-empty `path` (added to the existing reflection test).
- Full suite green. (No `--window`/GUI step; the visual "edit-and-watch-live" is the user's on
  return.)

## Ground rules honored

- **Tier wall:** `engine/script` stays Python-free; `std::filesystem`/`std::ifstream` are standard
  engine-tier tools. No pybind11.
- **Reflection-derived / #10:** `path` is an ordinary reflected string; a file-referenced script
  serializes and round-trips. **S4 seam reused:** the recompile is unchanged; only the source
  provider + reload trigger are new.
- No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as `uint64`.

## Acceptance

- [ ] `ScriptComponent` has a reflected `path`; a `.py`-referenced script round-trips.
- [ ] `ScriptSystem` loads the source from `path` when set, polls mtime, and reloads on an on-disk
      edit; inline `source` is used when `path` is empty (unchanged behavior).
- [ ] Headless tests: load-from-file, hot-reload-on-edit, missing-file-non-fatal, reflection
      round-trip. Full suite green.

## Depends on / relates

- **Depends on:** S4 (`ScriptComponent`/`ScriptSystem` recompile), S1 (`ScriptEngine`), the runtime
  integration (`ScriptSystem` ticks in editor + `mosaic run`). **Relates:** the in-editor Script
  Editor (#84 — inline editing), and the deferred `.py`-as-asset + OS-watcher refinements.
