# Optimization — `script_profile` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 7 of 7, the last one)

Sub-projects 1-6 gave an agent everything needed to author and run gameplay
behavior through MCP — create/mutate entities and components, inspect
scene structure, write a script (`set_script`), import assets, actually
run the simulation (`tick`), and verify the project still packages
(`build_project`). None gives an agent a way to see **how expensive** a
script actually is once it's running — the tuning half of "write it, run
it, check the result." This slice closes Phase 6's entire arc with one
tool, `script_profile`.

## Scope decision: the existing `ScriptSystem::profile()`, not new instrumentation

Two shapes were considered: wrapping `ScriptSystem::profile()` — a real,
already-tested per-entity `on_update` timing accumulator that already backs
`mosaic run --script-profile` — or building new per-`ecs::System` timing
instrumentation (script, physics, animation, etc. each timed per tick) with
no existing machinery to reuse. This slice ships **only** the former,
following the exact "thin wrapper over already-validated machinery"
pattern every one of sub-projects 3, 4, and 6 used. Broader system-level
timing is recorded as a real, separate future capability, not designed
further here.

## `script_profile`: the simplest of Phase 6's four thin-wrapper tools

`engine/script/script_system.hpp` already has:

```cpp
struct ScriptProfileEntry { std::uint64_t entity; std::uint64_t calls; double total_ms; double max_ms; };
std::vector<ScriptProfileEntry> ScriptSystem::profile() const;
```

One row per scripted entity that has run at least once: total `on_update`
calls, cumulative wall-time, and the single slowest call — already
accumulated by `ScriptSystem` across every `tick()` (sub-project 5) for the
life of the MCP session; `script_profile` doesn't reset or sample
anything, it just reads the running total.

The new tool:

```cpp
nlohmann::json tool_script_profile(World& world, const ComponentReflection&, CommandStack&,
                                   const std::string&, const nlohmann::json&)
{
    auto* sys = world.get_system<mosaic::engine::script::ScriptSystem>();
    if (sys == nullptr) { return {{"entries", nlohmann::json::array()}}; }
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : sys->profile())
    {
        entries.push_back({{"entity", e.entity}, {"calls", e.calls},
                           {"total_ms", e.total_ms}, {"max_ms", e.max_ms}});
    }
    return {{"entries", entries}};
}
```

Retrieved via `World::get_system<T>()` — the same generic accessor already
used elsewhere in the engine (e.g. `PhysicsSystem` lookups). Since
`setup_runtime` (sub-project 5) always adds a `ScriptSystem` to every
`mosaic mcp` session at startup, `sys` should never actually be `nullptr`
in practice — but the tool handles absence gracefully (`{"entries": []}`)
rather than assuming a system that's always been there so far will always
be there, matching this codebase's consistent defensive-but-not-paranoid
style.

## Wiring: no special-casing, unlike the last three new tools

Unlike `set_script`, `import_asset`, and `build_project` (each needed
something `handle_tool_call`'s uniform dispatch didn't already carry —
a script factory, or `project_dir`), `script_profile` needs only
`World&`, which every tool already receives. It's added directly to the
`tool_handlers()` map, exactly like the original 16 CRUD/`describe_scene`/
`tick` tools — no new special-case branch, no `handle_tool_call` signature
change at all. The simplest of the four "thin wrapper" additions across
Phase 6's back half.

## Non-goals (explicit future expansion)

- **Broader per-system timing** (physics, animation, particles, etc., not
  just scripts) — a real, separate capability with no existing machinery;
  not designed further here.
- **Resetting/clearing the profile mid-session** — `ScriptSystem::profile()`
  is a pure read of the running accumulator; no clear/reset tool is added.
  An agent that wants a clean before/after comparison restarts the
  `mosaic mcp` session (the same limitation `mosaic run --script-profile`
  already has — it dumps once, at process exit).
- **Wall-clock timing beyond `on_update`** (e.g. per-tool-call server
  overhead, or non-script system costs) — out of scope; this tool exposes
  exactly what `ScriptSystem` already tracks, nothing more.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): using the existing
  `RuntimeFixture` (from sub-project 5's `tick` work — no new fixture
  needed), a scripted entity ticked several times shows up in
  `script_profile`'s `entries` with `calls` matching the tick count and
  `total_ms`/`max_ms` both non-negative; an entity that was never ticked
  (or a fresh `RuntimeFixture` before any `tick` call) produces an empty
  `entries` array, not a crash; a plain `Fixture` (no `setup_runtime`, so
  no live `ScriptSystem`) calling `script_profile` returns `{"entries":
  []}` gracefully, exercising the `nullptr` branch directly.
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (20 → 21) — the same class of gap
  self-caught in each of the prior four sub-projects, called out here
  again so it isn't missed a sixth time.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 20 → 21. No
  signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "engine/script/script_system.hpp"`,
  a new `tool_script_profile` function (added to `tool_handlers()`, no
  special-casing), a new `tools_list_schema()` entry (reusing the existing
  `schema_none()` helper — no new schema helper needed, since this tool
  takes no parameters).
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, the
  existing `tools_list_schema` count assertion (20 → 21).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (20 → 21).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 7 of 7 — **the last one** — closing out Phase 6, and
  with it every phase `docs/project/roadmap.md` currently defines (Phase 6
  — Agent ecosystem — is the roadmap's last listed phase; there is no
  Phase 7). Names broader per-system timing as the one clearly-scoped
  future upgrade within this sub-project's own scope, and notes that
  what comes next is genuinely open — a call for the user/maintainer to
  make, not something this spec can name.
