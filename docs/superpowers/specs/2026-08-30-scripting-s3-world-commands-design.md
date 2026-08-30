# Scripting S3 — world + commands from a script (design)

**Decomposition:** [scripting-decomposition](2026-08-30-scripting-decomposition.md) (slice S3).
**Builds on:** S1 (`ScriptEngine`/pybind11), S2 (`bind_world`/`set_global` + `Value`↔Python +
`get_component`/`set_component`), #11 commands (`Command`/`CommandStack`), the runtime `World`.
**Date:** 2026-08-30.

> Built semi-autonomously (user approved the scripting cadence at PR #77: build → PR each slice).

## Summary

Let a Python script **query and change the live world's entity population** and route its mutations
through the **#11 command surface** so they are undoable. A script does `e = create_entity()`,
`destroy_entity(e)`, reads `entity_count()` / `entities()`, and — when a `CommandStack` is bound —
its `set_component` calls become recorded, undoable `SetComponentCommand`s that `undo()` / `redo()`
reverse. This is the first place the command system (built in #11, until now unused by the runtime)
drives a real workflow, honoring the manifesto's "a command is the only way state is mutated."

## Scope

**In:**
- **Entity lifecycle + query from a script** (direct on `World`): `create_entity() -> int`,
  `destroy_entity(eid) -> bool`, `entity_count() -> int`, `entities() -> list[int]`,
  `is_valid(eid) -> bool`.
- **Reflection gains generic remove-by-name:** `ComponentReflection::remove_value(World&, EntityID,
  name) -> bool` (one thunk, sibling to S2's `component_value`/`set_value`), so a command can undo
  an *add* by exactly removing the component.
- **Command surface:** the neutral `ScriptEngine` gains `bind_commands(CommandStack&)`. A new
  engine-tier `SetComponentCommand` (reflection-derived, reusing S2's `component_value`/`set_value`
  + `remove_value`) captures the prior component value (or its absence), applies the new one, and on
  `undo` restores the prior value — or removes the component if it was absent before. When a stack is
  bound, the script's `set_component` runs through it (undoable); when none is bound, it falls back
  to S2's direct `set_value` (unchanged behavior). Script builtins `undo() -> bool`, `redo() ->
  bool`, `undo_depth() -> int` drive the bound stack.

**Deferred:**
- **Toggling Play** — `PlayState` is a **studio-tier** concept (`studio/play_state.hpp`); a headless
  S3 has no studio. Script control of Play belongs to **S5** (the Studio console), where a real
  `PlayState` exists. Recorded here so the decomposition's "invokes a command / **toggles Play**"
  is honored in the right tier.
- **Command-backed entity create/destroy** — entity ids are generational, so undoing a create then
  redoing it yields a *different* id, which would surprise scripts holding the handle. v0.1 keeps
  entity lifecycle direct (not undoable); undoable structural edits are a post-v0.1 refinement.
- A general **id-keyed command registry** ("route a human button, an agent, and a script through the
  same command id") — no registry exists yet; building one is its own work. S3 seeds the command
  surface with the one command a script actually needs (component mutation).

## Architecture

### 1. `ComponentReflection::remove_value` (`engine/reflection/component_reflection.hpp`)

One typed thunk, registered beside S2's `valuers_`/`setters_` in `register_component<T>` (before the
`savers_[std::move(name)]` line, while `name` is still valid):

```cpp
// Remove component `name` from `e`. False if unregistered or the entity had no such component.
bool remove_value(World& world, EntityID e, std::string_view name) const;
// thunk: return world.remove_component<T>(e);
```

### 2. `SetComponentCommand` (`engine/commands/set_component_command.hpp`) — new, engine tier

An undoable component mutation, built generically on S2's reflection get/set + `remove_value`.
Engine-tier because it holds `World&` + `ComponentReflection&`; depends on `foundation/commands`
(neutral) — no Python.

```cpp
namespace mosaic::engine::commands
{
// Undoable "set component `name` on entity `e` to `next`". Captures the prior value (or its
// absence) at construction so undo restores exactly what was there. Reflection-derived: works for
// any registered component via S2's component_value/set_value + remove_value.
class SetComponentCommand : public mosaic::foundation::commands::Command
{
public:
    SetComponentCommand(World& world, reflection::ComponentReflection& types, EntityID e,
                        std::string name, Value next);
    std::string id() const override;                 // "world.set_component"
    bool valid() const { return applies_; }          // false if the entity/name/value was rejected
    void execute() override;                          // set_value(e, name, next_)
    void undo() override;                             // restore prior_ or remove_value(name)
    foundation::serialization::Value parameters() const override; // { entity, component, value }
private:
    World& world_; reflection::ComponentReflection& types_;
    EntityID e_; std::string name_; Value next_;
    std::optional<Value> prior_;                     // component_value at construction (nullopt=absent)
    bool had_component_ = false;                      // whether the component existed before execute
    bool applies_ = false;                            // set_value(next_) succeeded on construction probe
};
```

The constructor captures `prior_ = types_.component_value(world_, e_, name_)` and
`had_component_ = prior_.has_value()`. `execute()` calls `types_.set_value(world_, e_, name_,
next_)` and records success in `applies_`. `undo()`: if `had_component_`,
`types_.set_value(world_, e_, name_, *prior_)`; else `types_.remove_value(world_, e_, name_)` —
exact restoration either way. A command whose `set_value(next_)` would fail (bad entity/name/value)
is detected by the caller and **not recorded** (see §5), so `undo`/`redo` only ever see valid
commands.

### 3. `CommandStack::push_executed` (`foundation/commands/command_stack.hpp`)

The existing `execute(cmd)` *runs* `execute()` then records. S3 needs to record a command whose
effect has **already** been applied (the backend applies first so it can check validity and skip
recording a failed set). One small method:

```cpp
// Record an already-executed command WITHOUT calling execute() again. Clears redo history, exactly
// like execute(). redo() will re-run execute() normally; undo() inverts it.
void push_executed(std::unique_ptr<Command> command)
{
    undone_.clear();
    applied_.push_back(std::move(command));
}
```

This is a clean, general #11 addition (commands constructed post-hoc), unit-tested on its own.

### 4. Neutral `ScriptEngine::bind_commands` (`engine/script/script_engine.hpp`) — still Python-free

```cpp
// Bind an undoable command stack. Once bound, script set_component runs through a
// SetComponentCommand (undoable via undo()/redo()); entity lifecycle stays direct. Non-owning; the
// caller keeps the stack alive across run_string calls. Binding is optional (S2 behavior without it).
virtual void bind_commands(mosaic::foundation::commands::CommandStack& stack) = 0;
```

`script_engine.hpp` gains `#include "foundation/commands/command_stack.hpp"` — still no pybind11.

### 5. Python backend (`modules/script_python`)

At `bind_world` time (world available), install the **entity builtins**, closing over `world_`:
- `create_entity() -> int` → `world_->create_entity().value()` (immediate).
- `destroy_entity(eid) -> bool` → if `world_->is_valid(EntityID(eid))` then `world_->destroy_entity`,
  return `true`; else `false`. **`World::destroy_entity` is deferred** — it queues the entity and the
  slot is reclaimed at the next `World::tick`, so `entity_count()`/`is_valid()` still report the
  entity as alive until a tick runs. The script builtin mirrors this exactly (it must not bypass
  deferred destruction); `true` means "queued," not "gone."
- `entity_count() -> int` → `world_->entity_count()` (alive count; excludes queued-but-not-committed
  destructions).
- `entities() -> list[int]` → `for_each_entity` collecting `.value()` into a `py::list`.
- `is_valid(eid) -> bool` → `world_->is_valid(EntityID(eid))`.

`bind_commands(stack)` stores `stack_` (non-owning) and installs `undo`/`redo`/`undo_depth` closing
over it. It also **re-installs `set_component`** to prefer the stack: if `stack_` is set, build a
`SetComponentCommand`, call `execute()` on it, and — only if `cmd->valid()` — record it with
`stack_->push_executed(std::move(cmd))`, returning `true`; a failed set (`!valid()`) is discarded
unrecorded, returning `false` (S2's `False` contract, `undo_depth` unchanged). If no stack is bound,
fall back to the S2 direct `set_value`. `get_component` is unchanged.

- `undo() -> bool` → `stack_ ? stack_->undo() : false`.
- `redo() -> bool` → `stack_ ? stack_->redo() : false`.
- `undo_depth() -> int` → `stack_ ? stack_->undo_depth() : 0`.

EntityID crosses as its `uint64 .value()` (opaque handle), reconstructed via `explicit
EntityID(uint64)` — same convention as S2.

## Data flow

```
engine->bind_world(world, types);
engine->bind_commands(stack);
run_string(
  "a = create_entity(); b = create_entity()\n"   // world grows by 2
  "set_component(a, 'Transform', {'position': {'x': 5.0, 'y':0,'z':0}, ...})\n" // -> SetComponentCommand
  "undo()\n");                                    // Transform reverts to prior
// C++: entity_count()==2 (create is direct, not undone); the Transform set was undone.
```

## Error handling

- `destroy_entity`/`is_valid` on an unknown or stale id → `false` (never throws; World guards with
  `is_valid`). `destroy_entity` on a valid id → `true` (queued); the entity is actually removed at
  the next `World::tick` (deferred), so a script that destroys then immediately reads
  `entity_count()` still counts it — matching the C++ API.
- `set_component` through a bound stack with an unknown component/bad value → the command's
  `execute` (via `set_value`) is a no-op returning `false`; **it is not recorded** on the stack (so
  `undo_depth` doesn't grow for a failed set). Matches S2's None/False contract.
- `undo`/`redo` with nothing to undo/redo, or with no stack bound → `false`.
- Entity builtins and `undo`/`redo`/`undo_depth` are installed at `bind_world`; calling any of them
  (or `get_component`/`set_component`) before `bind_world` → the function isn't installed →
  `NameError`, captured by S1's `run_string` (never a crash). After `bind_world` but with no stack
  bound, `undo`/`redo` return `False` and `undo_depth` returns `0` (not a `NameError`).

## Testability

Headless (`tests/unit/`):

- **`ComponentReflection::remove_value` unit** (added to
  `tests/unit/reflection/component_value_test.cpp`): remove an existing component → `true` and
  `has_component` false; remove when absent or name unregistered → `false`.
- **`CommandStack::push_executed` unit** (added to `tests/unit/commands/command_stack_test.cpp`):
  `push_executed` grows `undo_depth` without calling `execute`; clears redo; `undo`/`redo` then
  behave normally.
- **`SetComponentCommand` unit** (`tests/unit/commands/set_component_command_test.cpp`, no Python):
  construct over a `World`+`Transform`, `execute` changes the value, `undo` restores the prior;
  `valid()` false for an unregistered name / invalid entity (execute is a no-op); the
  add-then-undo path removes the component (exact, via `remove_value`).
- **Script entity lifecycle** (`tests/unit/script/script_entities_test.cpp`): a script
  `create_entity()`s N (immediate: C++ sees `entity_count()`/`entities()` grow); `is_valid` tracks
  liveness; `destroy_entity(valid)` → `True` but is **deferred** (count unchanged until a tick);
  `destroy_entity(bad)` → `False`; after `world.tick(0)` in C++, the count drops and the victim is
  invalid.
- **Script commands / undo** (`tests/unit/script/script_commands_test.cpp`): with a bound
  `CommandStack`, a script `set_component`s a `Transform`, C++ observes the change **and**
  `undo_depth()==1`; `undo()` reverts the value in C++; `redo()` re-applies; a failed set doesn't
  grow `undo_depth`; without a bound stack, `set_component` still works (S2 path) and `undo()` →
  `False`.

## Ground rules honored

- **Tier wall:** `engine/script` + `engine/commands` + `engine/reflection` stay Python-free; all
  pybind11 in `modules/script_python`. `SetComponentCommand` is an ordinary engine command.
- **Reflection-derived (#18):** the command captures/applies via S2's `component_value`/`set_value`
  — no per-component code; every reflected component is undoably scriptable for free.
- **Command surface is #11:** mutations become real `Command`s on a `CommandStack`, reusing the
  existing execute/undo/redo machinery unchanged. This is its first runtime consumer.
- **Serialization is #10:** values still cross as the `Value` DOM (S2 marshaling reused). The
  command's `parameters()` projects to a `Value` for the (future) transaction log / agent API.
- No new third-party deps. Catch2 names avoid a leading `--`. EntityID crosses as `uint64`.

## Acceptance

- [ ] Script entity API: `create_entity`/`destroy_entity`/`entity_count`/`entities`/`is_valid`, with
      C++ observing each; unit-tested headlessly.
- [ ] `ScriptEngine::bind_commands`; a bound stack makes `set_component` an undoable
      `SetComponentCommand`; `undo`/`redo`/`undo_depth` drive it.
- [ ] `SetComponentCommand` restores the prior value on undo (direct unit test), reflection-derived.
- [ ] A script creates N entities and fires a command (undoable set); C++ observes both; error paths
      return `False`/no-op, never throw. Full suite green.

## Depends on / relates

- **Depends on:** S2 (`bind_world`/`set_global`, `component_value`/`set_value`, marshaling), S1
  (`ScriptEngine`), #11 (`Command`/`CommandStack`), the runtime `World`, #10 (`Value`).
  **Blocks:** S4 (`ScriptComponent` — scripted `on_update` mutates its entity through this surface),
  S5 (Studio console — binds the editor's world + stack + `PlayState`, adding Play toggle there).
- **Relates:** the deferred automation/agent API drives the same `Command` surface by id; command-
  backed entity lifecycle and a command registry are post-v0.1 refinements seeded here.
