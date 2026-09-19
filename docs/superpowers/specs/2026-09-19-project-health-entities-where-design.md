# Project Health & Bulk Editing — `entities_where` — Design

## Goal (sub-project 1 of 4: query/filter primitive)

Phase 6 (Agent ecosystem) shipped 21 MCP tools, every one a thin wrapper
over an already-existing, already-tested function. "Bulk refactoring /
project health checks" — recorded in
`docs/project/deferred-work-backlog.md` as the highest-leverage item left
on the backlog — is different: it's genuinely novel, no existing machinery
to wrap. It bundles at least three independent capabilities (bulk/mass
editing, orphaned-asset detection, broken-reference detection), each
needing a way to select which entities to act on. This is sub-project 1 of
an ordered 4-part sequence:

1. **Query/filter primitive** (this spec) — find entities matching a
   criteria, richer than the existing `entities_with(component)`'s
   has-component-only check.
2. Broken reference detection — a component field holding an `AssetID`/
   `EntityID` that doesn't resolve to anything live.
3. Orphaned asset detection — a registered/on-disk asset nothing
   references.
4. Bulk editing — mass rename/set-field/find-replace across matched
   entities. Highest risk (mutates many things at once); built last, on
   top of the validated read-only tooling from 1-3.

Sub-projects 2-4 are out of scope here — each gets its own spec once this
one ships and the query primitive exists to build on.

## `entities_where`: extends `entities_with` with a field filter

The existing `entities_with(component)` tool answers "which entities have
this component." `entities_where` answers "which entities have this
component **and** a field matches a condition":

```json
{"component": "Transform", "field": "position.x", "op": "gt", "value": 5.0}
```

returning, for every match, both the entity id and the field's actual
value (so an agent doesn't need a follow-up `get_component` round-trip to
see what it matched):

```json
{"matches": [{"entity": 123, "value": 7.5}, {"entity": 456, "value": 10.0}]}
```

## Field path resolution: reusing `AnimationSystem`'s dotted-path helper

`engine/animation/animation_system.hpp` already has:

```cpp
std::vector<std::string> split_path(const std::string& s, char delim);
```

Used by `AnimationSystem`/the sequencer to reach nested scalar fields
(e.g. `"position.x"`) inside a component's `Value` tree for keyframe
writes. `entities_where` reuses `split_path` for splitting the `field`
argument — the read-side counterpart doesn't exist yet (the animation
system only *writes* a known-existing leaf via `set_scalar_at`), so this
slice adds a small, MCP-local JSON-tree walker:

```cpp
std::optional<nlohmann::json> json_at_path(const nlohmann::json& root,
                                           const std::vector<std::string>& path)
{
    const nlohmann::json* cur = &root;
    for (const std::string& seg : path)
    {
        if (!cur->is_object() || !cur->contains(seg)) { return std::nullopt; }
        cur = &cur->at(seg);
    }
    return *cur;
}
```

Operating on the same JSON tree `get_component` already produces (via the
existing `value_to_json`/`component_value` pipeline) rather than the raw
`Value` DOM `set_scalar_at` operates on — `entities_where` is read-only,
so there's no need to touch the `Value`-level writer at all.

## Operators (v1): `eq`/`ne` (any type) + numeric comparisons

`eq`/`ne` compare any JSON type directly (`nlohmann::json`'s own `==`/`!=`
already handle this correctly across strings/numbers/bools/null). `gt`/
`gte`/`lt`/`lte` require both sides to be numbers — if the field's actual
value turns out not to be numeric for a given entity, that entity is a
**graceful non-match**, not an error (a per-entity runtime fact, not a
malformed request). An unrecognized `op` string, by contrast, **is**
`McpToolError` — validated once against a fixed set (`eq`, `ne`, `gt`,
`gte`, `lt`, `lte`) before scanning any entities, since that's a malformed
request the same way a missing required field already is elsewhere in
this tool surface.

**Deliberately deferred to keep v1 tight:** string ordering comparisons,
`contains`/substring matching, and existence checks (`exists`/
`not_exists` — "does this field/path exist at all, regardless of value").
Each is a real, plausible need once bulk find-and-replace (sub-project 4)
or broken-reference detection (sub-project 2) actually needs it — not
designed further here.

## A missing component or path is a graceful non-match

Consistent with every other "not found" case in this tool surface
(`get_component` returning `null`, `has_component` returning `false`): an
entity that doesn't have the queried component, or whose component lacks
the given field path, simply isn't included in `matches` — never an error.
`McpToolError` stays reserved for malformed request parameters (missing
`component`/`field`/`op`/`value`, or an unrecognized `op`).

## Wiring: no special-casing, like `script_profile`

`entities_where` needs only `World&` and `const ComponentReflection&`,
both already parameters on every `ToolHandler`. Added directly to the
`tool_handlers()` map — the same "simplest of the thin-wrapper shapes"
pattern `script_profile` already established, no `handle_tool_call`
signature change.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended): `eq`/`ne` on a
  string field (`Name.value`); `gt`/`gte`/`lt`/`lte` on a numeric field
  (`Transform.position.x`) across several entities with different values,
  asserting exactly the expected subset matches and each match's returned
  `value` is correct; an entity missing the queried component is excluded,
  not an error; an entity whose component lacks the given field path is
  excluded, not an error; a numeric operator (`gt`) against a non-numeric
  field value (e.g. a string) excludes that entity gracefully; an unknown
  `op` string throws `McpToolError`; missing any of `component`/`field`/
  `op`/`value` throws `McpToolError`.
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (21 → 22) — the same class of gap
  self-caught in every prior sub-project, called out again so it isn't
  missed here either.

## Files

- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 21 → 22. No
  signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "engine/animation/animation_system.hpp"`,
  `#include <unordered_set>`, a new `json_at_path` helper, a new `compare`
  helper, a new `tool_entities_where` function (added to `tool_handlers()`,
  no special-casing), a new `tools_list_schema()` entry, a new
  `schema_entities_where()` helper.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, the
  existing `tools_list_schema` count assertion (21 → 22).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (21 → 22).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  sub-project 1 of 4 in the project-health/bulk-editing arc, with
  broken-reference detection named as what's next.
- Modify: `docs/project/deferred-work-backlog.md` — append a short "(in
  progress: sub-project 1/4 shipped, see
  `2026-09-19-project-health-entities-where-design.md`)" note after the
  "Bulk refactoring / project health checks" bullet (in both the Agent
  API section and the Effort vs. impact table's callout at the bottom) —
  the file already documents itself as a point-in-time snapshot rather
  than a live index, so this is a light annotation, not a rewrite.

## Non-goals (explicit future expansion, within this sub-project's own scope)

- String ordering comparisons, `contains`/substring matching, `exists`/
  `not_exists` operators — see Operators above.
- Multi-condition filters (AND/OR across several field checks) — v1 is
  single-condition, matching `entities_with`'s existing simplicity. A
  richer query DSL is real future work if single-condition proves
  insufficient once sub-projects 2-4 are built on top of it.
- Everything in sub-projects 2-4 themselves (broken references, orphaned
  assets, bulk editing) — each gets its own spec.
