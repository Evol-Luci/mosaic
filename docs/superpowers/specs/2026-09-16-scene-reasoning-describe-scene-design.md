# Scene Reasoning — `describe_scene` — Design

## Goal (Phase 6, Agent ecosystem pillar — sub-project 2 of 7)

Sub-project 1 ([Agent API — MCP Server](2026-09-16-agent-api-mcp-server-design.md))
shipped `mosaic mcp <project-dir>` with a v1 tool set that's pure entity/
component CRUD: an agent can enumerate raw entities and components, but has
no way to ask a higher-level question about a scene ("what's in here, and
how is it organized?") without walking every entity and component by hand
via `entities`/`component_names`/`get_component`. This slice adds exactly
one new tool, `describe_scene`, to the same MCP server — no new transport,
no new protocol machinery, no new files beyond the tool's own handler and
schema entry in the existing `app/mcp/mcp_tools.*`.

## Scope decision: one tool, not a reasoning platform

"Scene reasoning" (the roadmap's phrase for this sub-project) bundles at
least four independent capabilities: a structural/summary view, spatial
queries (near-position/radius/bounding-box), asset-usage queries (which
entities reference asset X), and generic criteria-based search (a query
DSL). Building all four in one slice would repeat the mistake Agent API's
own spec explicitly avoided (CRUD-only v1, CLI-wrapping and live-Studio
attachment deferred). This slice ships **only** the structural summary;
the other three are recorded below as explicit, valuable future upgrades
to this same tool surface — not designed further here.

## `describe_scene`: no parameters, one snapshot

A 16th MCP tool, added to the existing `tool_handlers()` map and
`tools_list_schema()` in `app/mcp/mcp_tools.cpp` — no new file, no schema
changes to `mcp_protocol.*`. Takes no arguments (`schema_none()`, already
defined) and returns:

```json
{
  "entity_count": 12,
  "component_counts": {"Transform": 12, "MeshRenderer": 8, "Name": 3},
  "hierarchy": [
    {"entity": 5, "name": "Player", "children": [
      {"entity": 9, "name": null, "children": []}
    ]}
  ]
}
```

- **`entity_count`**: `world.entity_count()`, unchanged from the existing
  `entity_count` tool's own value — this is a convenience so an agent
  doesn't need two round-trips to see the count alongside the breakdown.
- **`component_counts`**: a name→count map, one entry per name in
  `ComponentReflection::registered_names()` that at least one entity
  carries (a registered-but-unused component type is omitted, not shown
  with a zero — keeps the map's size proportional to what's actually in
  the scene, not the engine's full registry). Built by iterating every
  entity once and tallying `components_on(world, e)` per entity, not by
  iterating every registered name and re-scanning the whole world per
  name (avoids `O(registered_names × entities)`).
- **`hierarchy`**: a forest rooted at every entity whose `Transform.parent`
  is nil (`engine/transform/transform.hpp`'s `Transform{ position,
  rotation, scale, parent }` — `parent` nil means root). Each node is
  `{"entity": <id>, "name": <string-or-null>, "children": [...]}` — `name`
  is the entity's `Name` component value (`engine/world/name.hpp`) if
  present, else JSON `null` (not a synthesized "Entity <index>" fallback
  string the way Studio's Hierarchy panel renders it — an agent needs to
  be able to tell "labeled" apart from "not", which a fallback string
  would erase). **Entities with no `Transform` at all are excluded from
  this section entirely** — `hierarchy` is a spatial tree, not a
  catch-all for every entity; those entities are still counted in
  `entity_count` and `component_counts`.

## Cycle safety

The walk starts only from true roots (`parent` nil) and recursively follows
each entity's children (built via one pass collecting a `parent →
[children]` map first, then walking depth-first from the roots). A
malformed cycle among **non-root** entities (`A.parent == B`,
`B.parent == A`, neither nil) is never reached from any root, so it simply
never appears in `hierarchy` — no crash, no infinite loop, no depth guard
needed for this walk specifically (unlike `TransformSystem::compute_world`'s
own `kMaxDepth = 256` guard, which exists for a different reason: it walks
*up* the parent chain per-entity to compute a world matrix, an unrelated
code path this tool doesn't touch). `entity_count`/`component_counts`
still count those entities normally, since those two are flat scans, not
tree walks. This asymmetry (a cyclic entity is "invisible" in the
hierarchy but still counted elsewhere) is accepted and documented here, not
solved further — malformed cyclic parent chains are already an edge case
`TransformSystem` itself only guards against, not prevents at the data
model level.

## Non-goals (explicit future expansion, valuable but out of scope here)

Recorded because each is a genuinely good next step, not because they're
unwanted:

- **Spatial queries** — "what entities are within radius R of position P",
  "what's the scene's bounding box" — needs `WorldTransform` positions and
  new distance/containment query logic; no such machinery exists in the
  engine yet.
- **Asset-usage queries** — "what entities reference asset X", "list every
  asset used in this scene" — walks every component's `AssetID`-typed
  fields via the existing `FieldVisitor::field_asset` callback
  (`engine/reflection/field_visitor.hpp`); a real, buildable feature, just
  not this slice.
- **Generic criteria-based search** — a small query DSL ("find entities
  where component X's field Y matches Z") — the most powerful and most
  general of the four, and the most likely to need its own decomposition
  when it's actually designed.
- **Pagination / size limits on `describe_scene` itself** — a scene with
  thousands of entities produces a correspondingly large response; no
  truncation or paging is added here. Accepted for a first slice; revisit
  if a real scene size makes this a problem.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended, not a new file):
  `describe_scene` against a hand-built scene covering: an empty world
  (`entity_count: 0`, empty `component_counts`, empty `hierarchy`); a flat
  scene (several entities with `Transform`, no parents — all roots, no
  children); a real parent-child chain (root → child → grandchild,
  asserting nesting); a `Name`-labeled vs. unlabeled entity in the same
  tree (asserting `null` vs. the string); an entity with no `Transform` at
  all (present in `entity_count`/`component_counts`, absent from
  `hierarchy`); and a hand-constructed cycle among non-root entities
  (asserting it's silently absent from `hierarchy`, present in the flat
  counts, and the call completes — no hang, no crash).
- **`tests/unit/app/mcp_protocol_test.cpp`**: no changes needed — the
  protocol layer is generic over tool name and already covered.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — no signature changes; `tools_list_schema()`'s
  doc comment ("for all 15 v1 tools") becomes "for all 16 v1 tools".
- Modify: `app/mcp/mcp_tools.cpp` — new `tool_describe_scene` handler, new
  `schema_none()`-based entry in `tools_list_schema()`, new
  `"describe_scene"` entry in `tool_handlers()`.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases per Testing
  above.
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  Phase 6 sub-project 2 of 7, with the three deferred capabilities and
  sub-project 3 (code generation) named as what's next.
