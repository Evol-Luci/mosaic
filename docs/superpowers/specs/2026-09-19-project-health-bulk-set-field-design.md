# Project Health & Bulk Editing — `bulk_set_field` — Design

## Goal (sub-project 4 of 4, the last one: bulk editing)

Sub-projects 1-3 shipped the read-only tooling this arc is built on:
`entities_where` (query/filter primitive), `broken_references`, and
`orphaned_assets` — all merged to `main` as MCP tools 22-24. This is
sub-project 4, the last one: **bulk editing** — mutating many entities in
one call. Once this ships, "bulk refactoring / project health checks" in
`docs/project/deferred-work-backlog.md` is fully done.

Sub-project 1's original scoping named "mass rename/set-field/find-replace
across matched entities." Re-reading that framing during this spec's own
brainstorming: "mass rename" isn't a separate operation from "set-field"
— bulk-renaming a set of entities amounts to bulk-setting their `Name`
component's `value` field, the flagship example of a generic set-field
operation, not a distinct primitive. `find-replace` (substring pattern
matching within a string field) is a genuinely distinct operation with
its own unresolved design questions (case sensitivity, substring vs.
whole-value matching, plain string vs. something richer) — deferred,
matching every prior sub-project's tight-v1 pattern. **v1 scope: a single
operation, `bulk_set_field`.**

## `bulk_set_field`: select via `entities_where`'s query shape, then set one field on every match

```json
{
  "component": "Name",
  "where": {"field": "value", "op": "eq", "value": "OldName"},
  "set": {"field": "value", "value": "NewName"}
}
```

- `component` (required string): every matched entity must have this
  component.
- `where` (optional object, `{field, op, value}`): reuses `entities_where`'s
  exact filter shape and its existing operators (`eq`/`ne`/`gt`/`gte`/
  `lt`/`lte`) and existing `compare()` helper verbatim — zero new
  comparison logic. Omitted entirely means "every entity that has
  `component`," the bulk-editing equivalent of `entities_with`.
- `set` (required object, `{field, value}`): the field to write (a dotted
  path into the component, same convention as `where.field`) and its new
  literal value, applied identically to every matched entity.

Response: `{"updated": 3, "entities": [123, 456, 789]}` — the count and
ids of entities actually mutated, so an agent doesn't need a follow-up
`entities_where` call to see what it changed.

## Write-side field patching: `json_set_at`, the counterpart to `entities_where`'s `json_at_path`

`entities_where` already has a read-side dotted-path walker,
`json_at_path`, that returns `std::nullopt` if any segment is missing.
This slice adds the write-side counterpart:

```cpp
bool json_set_at(nlohmann::json& root, const std::vector<std::string>& path,
                 const nlohmann::json& value)
{
    nlohmann::json* cur = &root;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        if (!cur->is_object() || !cur->contains(path[i])) { return false; }
        cur = &cur->at(path[i]);
    }
    if (path.empty() || !cur->is_object() || !cur->contains(path.back())) { return false; }
    (*cur)[path.back()] = value;
    return true;
}
```

Deliberately **requires every segment (including the leaf) to already
exist** — it never creates new structure. A reflected component's fields
are fixed by its C++ struct (e.g. every `Material` always has `base_color`
and `texture`, unconditionally), so a `set.field` that doesn't resolve is
always a malformed request (an unknown/misspelled field name), never a
legitimate per-entity condition — consistent with `entities_where`'s own
rule that a missing path is a request-shape question, not a per-entity
runtime fact, for the field a caller is actively trying to target
(distinct from `entities_where`'s *read*-side graceful-non-match, which
is about entities that legitimately don't have the queried data).

## Two-phase execution: build every patch first, mutate nothing until all of them succeed

1. **Select.** Walk every entity; keep those that have `component` and
   (if `where` is given) whose field matches `where`'s condition — the
   exact same walk `entities_where` already does.
2. **Build.** For each matched entity, read its current component value
   (`component_value`), convert to JSON (`value_to_json`), patch the
   target field via `json_set_at`, convert back to a `Value`
   (`json_to_value`), and construct (but do not yet execute) a
   `SetComponentCommand` for it. If `json_set_at` fails for **any**
   matched entity, throw `McpToolError` immediately — nothing has been
   mutated yet, since no command's `execute()` has run.
3. **Apply.** If there's nothing to do (zero matches), return
   `{"updated": 0, "entities": []}` without touching `CommandStack` at
   all — no no-op command polluting the undo history. Otherwise, wrap
   every constructed `SetComponentCommand` into one `CompositeCommand`
   (see below) and call `stack.execute(std::move(composite))` — a single
   call that runs every sub-command's `execute()` in order and records
   one atomic entry on the undo stack.

This ordering is what makes a malformed `set.field` request fail cleanly:
by the time any mutation happens, every patch has already been validated
to apply.

## `CompositeCommand`: a new, generic `foundation/commands/` primitive

`CommandStack` has no batch/composite command type today — `execute()`/
`push_executed()` each take exactly one `Command`, and `undo()`/`redo()`
operate one command at a time. For a tool explicitly framed as "highest
risk, mutates many things at once," N separate per-entity commands would
mean a single `undo` call only reverts the last entity touched — a real
correctness trap for an agent that calls `undo` once and assumes the
whole bulk edit is clean.

`CompositeCommand` (`foundation/commands/composite_command.hpp`) closes
this gap generically, not as MCP-specific glue:

```cpp
class CompositeCommand final : public Command
{
public:
    CompositeCommand(std::string id, std::vector<std::unique_ptr<Command>> commands)
        : id_(std::move(id)), commands_(std::move(commands))
    {
    }

    std::string id() const override { return id_; }

    void execute() override
    {
        for (auto& c : commands_) { c->execute(); }
    }

    void undo() override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) { (*it)->undo(); }
    }

    serialization::Value parameters() const override
    {
        serialization::Array arr;
        for (const auto& c : commands_) { arr.push_back(c->serialize()); }
        return serialization::Value{std::move(arr)};
    }

private:
    std::string id_;
    std::vector<std::unique_ptr<Command>> commands_;
};
```

`execute()` runs sub-commands in order; `undo()` reverses in reverse
order (undoing the last-applied change first, mirroring how a stack of
individual commands would unwind). `parameters()` projects each
sub-command's own `serialize()` into an array — consistent with
`Command`'s existing "the log is the source of truth" design, an
agent/human inspecting the transaction log sees the full batch, not an
opaque blob. This is a foundation-tier addition (alongside `Command`
itself and `SetComponentCommand`), not an MCP-specific type — any future
caller needing an atomic multi-command batch (a future Studio bulk-edit
UI, for instance) can reuse it directly.

## Wiring: fits the uniform `ToolHandler` shape, no special-casing

`bulk_set_field` needs only `World&`, `ComponentReflection&`, and
`CommandStack&` — all three already parameters on every `ToolHandler`.
Added directly to the existing `tool_handlers()` map, the same
"simplest of the thin-wrapper shapes" pattern `script_profile` and
`entities_where` already established — no `handle_tool_call` signature
change, unlike `broken_references`/`orphaned_assets` (which needed
`AssetRegistry`, a dependency no plain `ToolHandler` carries).

## Error handling

- Missing `component`, missing `set`/`set.field`/`set.value`, an
  unrecognized `where.op` string, or a `set.field` that doesn't resolve
  on a matched entity — all `McpToolError`, consistent with malformed
  request handling everywhere else in this tool surface.
- An unregistered/misspelled `component` name, or a `where` condition
  that legitimately matches nothing — both gracefully return
  `{"updated": 0, "entities": []}`, never an error, matching
  `entities_where`'s existing convention for the read side.

## Testing

- **`tests/unit/commands/composite_command_test.cpp`** (new): `execute()`
  runs sub-commands in order; `undo()` reverses in reverse order;
  `parameters()` reflects every sub-command's own `serialize()`.
- **`tests/unit/app/mcp_tools_test.cpp`** (extended): `bulk_set_field`
  with a `where` filter updates only the matching entities, returning
  the correct `updated` count and entity list; omitting `where` updates
  every entity with `component`; zero matches returns
  `{"updated": 0, "entities": []}` and leaves `undo_depth` unchanged (no
  command pushed); a `set.field` that doesn't resolve throws
  `McpToolError` **and** leaves every entity's component value
  unchanged (confirms the two-phase build-then-apply ordering); a
  single `undo` call after a multi-entity bulk edit reverts every
  touched entity, not just the last one (the core atomic-undo test);
  `redo` re-applies the whole batch; missing `component`/`set`/
  `set.field`/`set.value` and an unrecognized `where.op` each throw
  `McpToolError`.
- **`tests/unit/app/mcp_tools_test.cpp`**: the existing `tools_list_schema`
  count assertion needs updating (24 → 25).
- **`tests/unit/app/mcp_protocol_test.cpp`**: the existing `tools/list`
  count assertion needs updating (24 → 25).

## Files

- Create: `foundation/commands/composite_command.hpp` — `CompositeCommand`.
- Create: `tests/unit/commands/composite_command_test.cpp`.
- Modify: `app/mcp/mcp_tools.hpp` — doc-comment tool count 24 → 25. No
  signature changes.
- Modify: `app/mcp/mcp_tools.cpp` — `#include "foundation/commands/composite_command.hpp"`,
  a new `json_set_at` helper, a new `tool_bulk_set_field` function (added
  to `tool_handlers()`, no special-casing), a new `tools_list_schema()`
  entry, a new `schema_bulk_set_field()` helper.
- Modify: `tests/unit/app/mcp_tools_test.cpp` — new test cases, the
  existing `tools_list_schema` count assertion (24 → 25).
- Modify: `tests/unit/app/mcp_protocol_test.cpp` — existing `tools/list`
  count assertion (24 → 25).
- Modify: `docs/project/next-steps.md` — new dated entry naming this as
  sub-project 4 of 4, the last one, closing out the arc.
- Modify: `docs/project/deferred-work-backlog.md` — update the "(in
  progress: sub-project 3/4 shipped...)" annotations (both the Agent API
  section and the Effort vs. impact table) to reflect the arc's
  completion — all 4 sub-projects shipped — pointing at this spec.

## Non-goals (explicit future expansion, within this sub-project's own scope)

- `find-replace` (substring pattern matching within a string field
  across matched entities) — see Goal above; its own design questions
  (case sensitivity, substring vs. whole-value, plain string vs.
  something richer) are real future work, not designed further here.
- Multi-field bulk sets (setting several fields at once per matched
  entity) — v1 is single-field, matching `entities_where`'s existing
  single-condition simplicity.
- Bulk operations beyond set-field (bulk `remove_component`, bulk
  `destroy_entity`, etc.) — each would need its own scoping; not
  attempted here.
- Using `CompositeCommand` to retrofit atomic undo onto any other
  existing multi-step operation — this spec introduces it for
  `bulk_set_field` only; other call sites are out of scope.
