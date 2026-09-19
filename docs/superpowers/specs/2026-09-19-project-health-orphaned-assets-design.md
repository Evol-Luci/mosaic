# Project Health & Bulk Editing — `orphaned_assets` — Design

## Goal (sub-project 3 of 4: orphaned asset detection)

Sub-project 1 shipped `entities_where` (the query/filter primitive) and
sub-project 2 shipped `broken_references` (component fields pointing at
assets that don't exist) — see `docs/project/deferred-work-backlog.md` and
their own specs. This is sub-project 3: **orphaned asset detection** — a
registered/on-disk asset that nothing currently references. Sub-project 4
(bulk editing) remains out of scope, gets its own spec once this ships.

`orphaned_assets` is the mirror image of `broken_references`: that tool
asks "does this reference point at a real asset?" (entity → asset,
checking existence); this one asks "does anything point at this asset?"
(asset → entities, checking use).

## `orphaned_assets`: diff the referenced-id set against the registry

No arguments, scans the whole project:

```json
{}
```

Two passes, both reusing machinery `broken_references` already
established:

1. **Gather every referenced `AssetID`.** Walk every entity's every
   component the same way `broken_references`'s `AssetRefCollector` does
   (`types.components_on()` + `types.inspect()`), but with a new, simpler
   visitor, `ReferencedAssetGatherer`, that only implements `field_asset`
   to insert every non-nil `AssetID` it sees into a `std::unordered_set<AssetID>`
   (`AssetID` already has a `std::hash` specialization,
   `foundation/serialization/reference.hpp:152`, so this needs no new
   infrastructure). Unlike `AssetRefCollector`, this visitor doesn't need
   to track entity/component/field context or a group-path stack — it
   only cares about which ids exist in the set, not where each came from.
2. **Diff against the registry.** `AssetRegistry::for_each(fn)`
   (`engine/assets/asset_registry.hpp:53`) enumerates every registered
   `AssetMetadata`. Any `meta.uuid` not present in the referenced set is
   orphaned.

Result: every orphaned asset's id plus its own metadata (`type`, `source`)
— since, unlike `broken_references`, there's no entity/component to anchor
the result to, the asset's own metadata is the only context an agent gets:

```json
{
  "orphaned": [
    {"asset_id": "3f9c...-...-...", "type": "image", "source": "textures/unused.png"}
  ]
}
```

An empty `orphaned` array means every registered asset is referenced by
at least one entity.

## Scope: entity references only, not `AssetMetadata.dependencies`

`AssetMetadata` has a `dependencies` field (`vector<AssetID>`, reflected —
`engine/assets/asset_metadata.hpp:25`) intended for one asset referencing
another (e.g. a Material asset depending on a Texture asset). Grepped the
whole codebase: `dependencies` is never populated anywhere in production
code today — only referenced in its own `reflect()` function and a test
schema fixture (`tests/unit/serialization/sample_schemas.hpp`). Treating
an asset listed in another asset's `dependencies` as non-orphaned is
explicitly **not** built here: the field is always empty in practice, so
that code path would be untestable with real data (a test would need to
hand-construct an `AssetMetadata` with a manually-set `dependencies` list,
exercising a code path nothing in the actual pipeline produces).
Deferred, explicit future work if `dependencies` ever gets populated by a
real producer — not designed further here (YAGNI).

## Accepted limitation: same `AssetRegistry` staleness as `broken_references`

Carries over unchanged from `broken_references`'s own design: the
`AssetRegistry` snapshot is loaded once at `mosaic mcp` startup and never
refreshed. For `orphaned_assets` specifically, this means an asset
imported via `import_asset` earlier in the same session is simply **not
enumerated at all** by `AssetRegistry::for_each()` — it can't show up as
a false-positive orphan (it's invisible to both passes, not present in
the registry to be checked), but it also can't be flagged as a real
orphan even if the agent never actually referenced it. Restated in the
tool's own `tools_list_schema()` description, matching `broken_references`'s
precedent of surfacing this explicitly rather than silently.

## Wiring: reuses `broken_references`'s existing `AssetRegistry` threading

`handle_tool_call` already carries `const AssetRegistry& asset_registry`
(added for `broken_references`) — `orphaned_assets` is a 5th special-cased
branch alongside it, using the same parameter with no signature change
needed. `app/run/mcp.cpp`'s `dispatch` lambda already passes
`asset_registry` through; no change needed there either.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`** (extended, `RuntimeFixture`): an
  asset registered in `asset_registry` but referenced by no entity's field
  is reported, with the correct `asset_id`/`type`/`source`; an asset
  registered and referenced by a `Material.texture` field is NOT reported;
  a nil `AssetID` field never counts as "referencing" anything (doesn't
  suppress an otherwise-orphaned asset); multiple orphaned assets across
  a mixed registry (some referenced, some not) are all reported; an empty
  registry returns an empty `orphaned` array; an asset referenced by an
  entity whose component holds a *different* asset's id is still reported
  orphaned (confirms the diff is by id, not just "something is set").
- **`tests/unit/app/mcp_protocol_test.cpp`**: the `tools/list` count
  assertion needs updating (23 → 24).

## Files

- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 23 → 24. No
  signature changes (reuses the existing `asset_registry` parameter).
- Modify: `app/mcp/mcp_tools.cpp` — new `ReferencedAssetGatherer` visitor,
  new `tool_orphaned_assets` function (special-cased in `handle_tool_call`,
  not in `tool_handlers()`), a new `tools_list_schema()` entry with the
  staleness caveat restated.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases using
  `RuntimeFixture`, the existing `tools_list_schema` count assertion
  (23 → 24).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (23 → 24).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  sub-project 3 of 4, with bulk editing named as what's next.
- Modify: `docs/project/deferred-work-backlog.md` — update the existing
  "(in progress: sub-project 2/4 shipped...)" annotations (both the Agent
  API section and the Effort vs. impact table) to "sub-project 3/4
  shipped", pointing at this spec.

## Non-goals (explicit future expansion, within this sub-project's own scope)

- `AssetMetadata.dependencies`-aware orphan detection — see Scope above.
- Deleting/archiving an orphaned asset, or any other mutation — this
  tool is read-only, like `broken_references`; a future bulk-editing
  capability (sub-project 4) could plausibly build a "delete orphaned
  assets" operation on top of this tool's output, but that's explicit
  future work, not built here.
- Fixing the `AssetRegistry` staleness itself — separately tracked, not
  this slice; see Accepted limitation above.
- Everything in sub-project 4 (bulk editing) — gets its own spec.
