# Automated Testing — `tick` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 5 of 7)

Sub-projects 1-4 gave an agent a validated way to create/mutate entities and
components, inspect a scene's structure, author a gameplay script, and
import assets — but the MCP server itself never calls `World::tick()`
anywhere. It is purely a static edit surface today: an agent can attach a
`ScriptComponent` via `set_script`, but has no way to actually **run** it
and observe the results through MCP — no "press Play" equivalent exists
yet. This slice closes that gap: the loop of "write a script → run it →
check what happened" that this whole Phase 6 effort has been building
toward.

## Scope decision: the full `setup_runtime` systems, not a narrow subset

Two shapes were considered: wiring only `ScriptSystem` (the one system
sub-project 3's `set_script` actually produces testable code for), or
reusing `app/run/demo_scene.hpp::setup_runtime(...)` wholesale — the exact
function `mosaic run`/`mosaic editor` already use to wire a full play
session (`ScriptSystem`, `AnimationSystem`, `SequenceSystem`,
`ParticleSystem`, `TrailSystem`, `AudioSystem`, `PhysicsSystem` via Box3D,
`UiSystem`). Investigation found `setup_runtime`'s optional dependencies
(`audio_backend = nullptr`, no `net_transport`) are already gracefully
handled — `nullptr` audio backend makes `AudioSystem` a no-op — and this
**is already the exact code path headless `mosaic run` (no `--window`)
exercises routinely**, not new or risky territory. Given that, reusing it
wholesale costs about the same implementation effort as a narrow
`ScriptSystem`-only slice but gives an agent real physics-driven behavior
testing too, not just scripts — chosen as the shape for this slice.

## Startup: `run_mcp` gains an `AssetRegistry` + `setup_runtime`

`run_mcp` (`app/run/mcp.cpp`) currently builds `World` + `ComponentReflection`
+ `CommandStack`, loads the scene, and stops there. This slice adds, mirroring
`mosaic editor`'s own startup sequence (`app/run/editor.cpp`) exactly:

1. An `AssetRegistry`, populated via the existing `load_project_assets(registry,
   <project_dir>/assets)` (already handles a missing/empty `assets/` directory
   gracefully — a no-op, not an error).
2. A call to `setup_runtime(world, asset_registry, types, script_factory)` —
   reusing the **same** `script_factory` already constructed for `set_script`
   (sub-project 3). `ScriptSystem::EngineFactory` and `mcp::ScriptEngineFactory`
   are the identical `std::function<std::unique_ptr<ScriptEngine>()>` type
   under two different aliases — no conversion needed, the same factory value
   is passed to both `ScriptEditorModel` (via `set_script`) and `ScriptSystem`
   (via `setup_runtime`).
3. The returned `RuntimeScene`'s `render_system` field is discarded — MCP is
   headless and never renders; `setup_runtime`'s own doc comment already notes
   `RenderSystem` "needs no device (GPU uploads happen at render time)".

This adds systems to the `World`, but systems only run inside `World::tick()`
— since no MCP tool calls `tick()` before this slice, and no tool other than
the new `tick` tool calls it after, this startup change is inert for every
one of the 18 existing tools. No behavior change to anything already shipped.

## `tick`: the only tool that actually advances simulation time

Unlike `set_script`/`import_asset`, `tick` fits the existing uniform
`ToolHandler` shape (`World&, const ComponentReflection&, CommandStack&,
const std::string&, const nlohmann::json&`) as-is — it only needs `World&`
— so it's added directly to the `tool_handlers()` map, no special-casing.

Parameters, both optional:
- `count` (default `1`): number of ticks to run. Capped at **10,000 per
  call** — a safety valve against a single pathological request blocking
  the server (at the default `dt`, that's ~167 seconds of simulated time
  per call, ample headroom for a test scenario without an unbounded loop).
  A `count` above the cap is `McpToolError`, not a silent clamp — an agent
  should notice it asked for more than the tool allows, not get a quietly
  truncated result.
- `dt` (default `1.0/60.0`): seconds per tick, matching the engine's usual
  fixed timestep.

Runs `world.tick(static_cast<float>(dt))` `count` times in a loop, returns
`{"ticks": count}` (echoing the actual count run — always equal to the
requested `count` on success, since nothing here can partially fail
mid-loop; useful for an agent's own bookkeeping without needing to track
the value itself).

## Accepted limitation: imported assets don't hot-reload into the live registry

A newly `import_asset`'d file (sub-project 4) is cooked and written to disk,
but `load_project_assets` only runs once, at `run_mcp` startup — an asset
imported mid-session isn't visible to the live `AssetRegistry` until the
next `mosaic mcp` process restart. An agent that imports an asset and
immediately references it on a component this same session won't have it
resolve during `tick`. Not solved here; a real future slice if it matters
in practice (would need either a registry-refresh tool or teaching
`import_asset` to register directly into the live registry, not just write
files).

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): `tick` advancing a
  scripted entity's state over multiple ticks (a `ScriptComponent` whose
  `on_update` increments a counter component field, verified via
  `get_component` before/after — the actual "write a script, run it, check
  the result" loop this slice exists for); `tick` with no arguments
  defaulting to a single tick at the standard timestep; `count` exceeding
  the 10,000 cap throwing `McpToolError`; malformed `dt`/`count` (wrong
  JSON type) throwing `McpToolError`. This needs a real
  `create_python_script_engine` factory (same test-setup pattern
  `set_script`'s tests already established) *and* the new `Fixture` startup
  path actually calling `setup_runtime` — see Files below for how the test
  fixture accommodates this without disturbing the other ~30 existing test
  cases that never need systems wired at all.
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (18 → 19) — the same class of gap
  self-caught in each of the three prior sub-projects, called out here
  again so it isn't missed a fourth time.

## Files

- Modify: `app/run/mcp.cpp` — new `AssetRegistry`, `load_project_assets`
  call, `setup_runtime` call (discarding `RuntimeScene::render_system`).
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 18 → 19.
- Modify: `app/mcp/mcp_tools.cpp` — new `tool_tick` function (added to
  `tool_handlers()`, no special-casing needed), a new
  `tools_list_schema()` entry, a new `schema_tick()` helper, and small
  `optional_uint64`/`optional_double` argument helpers alongside the
  existing `require_uint64`/`require_string`.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, plus a new
  `RuntimeFixture` struct (parallel to the existing `Fixture`, not a
  modification of it — the ~30 existing tests using `Fixture` must not pay
  for `setup_runtime`'s cost, most obviously constructing a real Box3D
  `PhysicsSystem`, on every one of their constructions). `RuntimeFixture`
  adds an `AssetRegistry` member and calls `setup_runtime(world,
  asset_registry, types, factory)` in its constructor with a real
  `create_python_script_engine` factory (the same one `set_script`'s tests
  already construct); only `tick`'s own test cases use it.
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (18 → 19).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 5 of 7, with the asset-hot-reload limitation and
  sub-project 6 (project maintenance) named as what's next.

## Non-goals (explicit future expansion)

- **Hot-reloading imported assets into the live registry mid-session** —
  see Accepted limitation above.
- **A "run until a condition is met" tool** (e.g., "tick until this
  component's field changes" or "tick until N seconds of sim time with a
  timeout") — `tick`'s fixed-count shape is the simplest useful primitive;
  a smarter condition-driven variant is real future work, not designed
  further here.
- **Exposing `ScriptHost`'s other injectable hooks** (custom raycast
  overrides, etc.) — `setup_runtime`'s defaults are used as-is.
