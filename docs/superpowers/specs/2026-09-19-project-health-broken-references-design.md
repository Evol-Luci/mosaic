# Project Health & Bulk Editing — `broken_references` — Design

## Goal (sub-project 2 of 4: broken reference detection)

Sub-project 1 shipped `entities_where`, the query/filter primitive the
project-health/bulk-editing arc is built on (see
`docs/project/deferred-work-backlog.md` and
`2026-09-19-project-health-entities-where-design.md`). This is sub-project
2: **broken reference detection** — find component fields that hold an
`AssetID` pointing at an asset that no longer resolves. Sub-projects 3
(orphaned asset detection) and 4 (bulk editing) remain out of scope, each
gets its own spec once this one ships.

## Scope: `AssetID` references only, not `EntityID`

A component can hold two kinds of "reference": an `AssetID` (points at an
asset) or an `EntityID` (points at another entity). Only `AssetID`
references are in scope for this slice.

`engine/reflection/field_visitor.hpp`'s `FieldVisitor` interface has a
dedicated, overridable `field_asset(name, AssetID&)` callback — already
used by `app/docs/component_reference.hpp`'s `MarkdownFieldVisitor` for
the component-reference-docs generator. This is exactly the hook needed to
find every `AssetID`-typed field on any component, for any entity, via
`types.inspect(world, entity, component_name, visitor)`.

`EntityID` fields, by contrast, route through the same generic
`field_readonly` callback as `bool` and other arithmetic types — there is
no way to distinguish an `EntityID` reference field from any other
readonly scalar without extending `FieldVisitor` itself, a cross-cutting
change that also touches Studio's shared Inspector (previously flagged
out-of-scope in earlier specs, e.g. the doc-tooling "precise field type
labels" item in the deferred-work backlog). Building `EntityID` reference
checking is real future work gated on that separate `FieldVisitor`
extension; it is not attempted here.

## `broken_references`: scans the whole world, no arguments

Unlike `entities_where` (which filters on a caller-given condition),
`broken_references` is a health check — the point is surfacing problems
the agent didn't know to look for. It takes no arguments and walks every
entity's every component:

```json
{}
```

returning every `AssetID` field whose value is non-nil but doesn't resolve
in the live `AssetRegistry`:

```json
{
  "broken": [
    {"entity": 123, "component": "Sprite", "field": "texture", "asset_id": "3f9c...-...-..."}
  ]
}
```

An empty `broken` array means a clean scan. A field holding the nil
`AssetID` (i.e. `is_nil()` is true — no reference set) is normal, not
broken, and never appears in the output.

## Walking components: `components_on` + `inspect`, reusing existing patterns

For each entity (`world.for_each_entity`), get its actual component list
via `types.components_on(world, e)` — the same call `component_names`
already uses — then, for each component name, drive
`types.inspect(world, e, name, visitor)` with a new local visitor,
`AssetRefCollector`.

`AssetRefCollector : FieldVisitor` implements every pure-virtual callback
(the interface requires it) but only `field_asset` does real work:

```cpp
class AssetRefCollector final : public mosaic::engine::reflection::FieldVisitor
{
public:
    AssetRefCollector(const AssetRegistry& registry, nlohmann::json& out,
                      EntityID entity, const std::string& component)
        : registry_(registry), out_(out), entity_(entity), component_(component)
    {
    }

    void field_asset(std::string_view name, AssetID& value) override
    {
        if (value.is_nil()) { return; }
        if (registry_.contains(value)) { return; }
        out_.push_back({{"entity", entity_.value()},
                        {"component", component_},
                        {"field", path_with(name)},
                        {"asset_id", value.to_string()}});
    }

    void field_float(std::string_view, float&) override {}
    void field_readonly(std::string_view, const std::string&) override {}
    void begin_group(std::string_view name) override { path_.push_back(std::string(name)); }
    void end_group() override { path_.pop_back(); }

private:
    std::string path_with(std::string_view leaf) const
    {
        std::string result;
        for (const std::string& seg : path_) { result += seg + "."; }
        result += leaf;
        return result;
    }

    const AssetRegistry& registry_;
    nlohmann::json& out_;
    EntityID entity_;
    std::string component_;
    std::vector<std::string> path_;
};
```

No built-in component today nests an `AssetID` inside a `begin_group`, so
`path_with` degrades to the plain field name in practice — the group-path
tracking exists only because the `FieldVisitor` contract requires
`begin_group`/`end_group` overrides regardless, and getting the dotted
path right costs nothing extra. This mirrors the read side already
established for nested paths in `entities_where`.

## Wiring: `AssetRegistry` threaded into `handle_tool_call`, a 4th special-cased tool

`app/run/mcp.cpp` already constructs an `AssetRegistry asset_registry`
local at startup (populated via `load_project_assets`, used to build
`setup_runtime`), alive for the whole `run_mcp` call. Today it's used only
locally — not captured by the `dispatch` lambda, not passed into
`handle_tool_call`.

This slice adds a new parameter to `handle_tool_call`:

```cpp
nlohmann::json handle_tool_call(World& world, const ComponentReflection& types,
                                CommandStack& stack, const std::string& scene_path,
                                const std::string& tool_name, const nlohmann::json& arguments,
                                const ScriptEngineFactory& script_factory = ScriptEngineFactory{},
                                const std::string& project_dir = std::string{},
                                const AssetRegistry& asset_registry = AssetRegistry{});
```

and a 4th `if` branch alongside `set_script`/`import_asset`/`build_project`:

```cpp
if (tool_name == "broken_references") { return tool_broken_references(world, types, asset_registry); }
```

`app/run/mcp.cpp`'s `dispatch` lambda gains `asset_registry` to its
capture list and passes it through, the same way `project_dir` already
is. This follows the exact precedent those three tools set — a tool
needing a dependency no plain `ToolHandler` carries gets special-cased in
`handle_tool_call` rather than changing the `ToolHandler` shape for
everyone.

## Accepted limitation: the `AssetRegistry` snapshot is stale after `import_asset`

`app/run/mcp.cpp` populates `asset_registry` once, at `mosaic mcp`
startup, via `load_project_assets`. It is never refreshed. This is the
existing, already-tracked "hot-reload imported assets into the live
`AssetRegistry` mid-session" gap in `docs/project/deferred-work-backlog.md`
(sourced from the `import_asset` design spec) — not a new bug introduced
here, but `broken_references` is the first tool that makes the gap
user-visible as an actual false positive: an asset imported via
`import_asset` earlier in the same `mosaic mcp` session will show up as a
broken reference (its `AssetID` genuinely won't be in `asset_registry`)
until the process restarts.

This is called out explicitly in the tool's schema description (so an
agent reading `tools/list` sees the caveat before acting on a false
positive) rather than fixed here — fixing it means building the
hot-reload mechanism, which is its own, separately-scoped backlog item.

## Testing

- **`tests/unit/app/mcp_tools_test.cpp`**: needs the `AssetRegistry`
  wired up (the `RuntimeFixture` already extends the base `Fixture` with
  one for `script_profile`'s tests — reused here, not duplicated). Cases:
  an entity with a nil `AssetID` field is not reported; an entity with an
  `AssetID` field that resolves via `registry.contains` is not reported;
  an entity with an `AssetID` field that does NOT resolve is reported,
  with the correct `entity`/`component`/`field`/`asset_id`; multiple
  broken refs across different entities/components all appear; an empty
  world (or a world with no `AssetID`-bearing components in use) returns
  an empty `broken` array; `handle_tool_call`'s new default-constructed
  `AssetRegistry{}` parameter is exercised implicitly by every other
  existing test in the file that doesn't pass one (confirms the default
  doesn't break unrelated tools).
- **`tests/unit/app/mcp_protocol_test.cpp`**: the `tools/list` count
  assertion needs updating (22 → 23).

## Files

- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 22 → 23; new
  `AssetRegistry` forward-declare or include; new default parameter on
  `handle_tool_call`.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "engine/assets/asset_registry.hpp"`,
  new `using mosaic::foundation::serialization::AssetID;` and
  `using mosaic::engine::assets::AssetRegistry;` aliases (matching the
  file's existing `using` block), new `AssetRefCollector` visitor class,
  new `tool_broken_references` function (special-cased in
  `handle_tool_call`, not in `tool_handlers()`), new `tools_list_schema()`
  entry with the staleness caveat in its description, new
  `schema_broken_references()` helper.
- Modify: `app/run/mcp.cpp` — capture `asset_registry` in the `dispatch`
  lambda, pass it through to `handle_tool_call`.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases using
  `RuntimeFixture`, the existing `tools_list_schema` count assertion
  (22 → 23).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (22 → 23).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  sub-project 2 of 4, with orphaned-asset detection named as what's next.
- Modify: `docs/project/deferred-work-backlog.md` — update the existing
  "(in progress: sub-project 1/4 shipped...)" annotations (both the Agent
  API section and the Effort vs. impact table) to "sub-project 2/4
  shipped", pointing at this spec.

## Non-goals (explicit future expansion, within this sub-project's own scope)

- `EntityID` reference checking — needs a `FieldVisitor` extension to
  distinguish `EntityID` fields from generic readonly scalars; see Scope
  above.
- Fixing the `AssetRegistry` staleness itself (hot-reloading imported
  assets mid-session) — separately tracked, not this slice; see Accepted
  limitation above.
- An optional component-name filter to narrow the scan — no evidence a
  full scan is slow; YAGNI for v1.
- Everything in sub-projects 3-4 (orphaned assets, bulk editing) — each
  gets its own spec.
