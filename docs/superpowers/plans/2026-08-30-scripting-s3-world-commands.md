# Scripting S3 — world + commands from a script Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python script queries/creates/destroys live entities and drives undoable component mutation through the #11 command surface — `create_entity()`, `destroy_entity()`, `entity_count()`, and (with a bound `CommandStack`) an undoable `set_component` + `undo()`/`redo()`.

**Architecture:** Reflection gains `remove_value` (generic remove-by-name). `CommandStack` gains `push_executed` (record an already-applied command). A new engine-tier `SetComponentCommand` captures/restores a component value via S2's reflected get/set. The neutral `ScriptEngine` gains `bind_commands`; the Python backend installs entity builtins + routes `set_component` through the stack when bound.

**Tech Stack:** C++20, CMake, Catch2, pybind11 (from S1). Reuses S2 (`component_value`/`set_value`, `Value`↔Python marshaling), #11 (`Command`/`CommandStack`), the runtime `World`.

**Spec:** `docs/superpowers/specs/2026-08-30-scripting-s3-world-commands-design.md`

## Global Constraints

- **Tier wall:** `engine/reflection` + `engine/commands` + `engine/script` stay Python-free; ALL pybind11 lives in `modules/script_python`. `SetComponentCommand` is an ordinary engine command; `remove_value`/`push_executed` are ordinary methods.
- **Reflection-derived (#18):** get/set/remove go through the reflected thunks; no per-component code.
- **Command surface is #11:** script mutations become real `Command`s on a `CommandStack` (execute/undo/redo unchanged). `push_executed` records an already-applied command without re-running `execute`.
- **`Value` DOM (#10):** values cross as the variant; EntityID crosses as `uint64 .value()` ↔ `explicit EntityID(uint64)` (same as S2). Reuse S2 marshaling.
- **Never throw into Python / into C++ callers:** `destroy_entity`/`is_valid` on a bad id → false; a failed `set_component` → `False`, not recorded; `undo`/`redo` with nothing/no-stack → `false`.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::reflection`, `mosaic::engine::commands`, `mosaic::engine::script`, `mosaic::foundation::commands`, `mosaic::script::python`, `mosaic::foundation`.
- **Backward compatibility:** with NO stack bound, `set_component` keeps S2's direct behavior; S2 tests must stay green.

---

### Task 1: prerequisite primitives — `ComponentReflection::remove_value` + `CommandStack::push_executed`

Two small, independent leaf additions in different subsystems, each with its own test. Batched into one task (both are tiny, no interdependency).

**Files:**
- Modify: `engine/reflection/component_reflection.hpp`
- Modify: `tests/unit/reflection/component_value_test.cpp`
- Modify: `foundation/commands/command_stack.hpp`
- Modify: `tests/unit/commands/command_stack_test.cpp`

**Interfaces:**
- Produces: `bool ComponentReflection::remove_value(World&, EntityID, std::string_view) const;` and `void CommandStack::push_executed(std::unique_ptr<Command>);`

- [ ] **Step 1: Write the failing `remove_value` test** — add to `tests/unit/reflection/component_value_test.cpp` (after the last TEST_CASE):

```cpp
TEST_CASE("remove_value removes a component; false when absent or unregistered", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});

    REQUIRE(types.remove_value(world, e, "Transform"));
    REQUIRE_FALSE(world.has_component<Transform>(e));
    REQUIRE_FALSE(types.remove_value(world, e, "Transform")); // already gone
    REQUIRE_FALSE(types.remove_value(world, e, "Nope"));      // unregistered
}
```

- [ ] **Step 2: Write the failing `push_executed` test** — add to `tests/unit/commands/command_stack_test.cpp` (reuse the existing `AppendCommand` fake):

```cpp
TEST_CASE("push_executed records an already-applied command without executing", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    trace.push_back("x"); // the effect was already applied externally
    stack.push_executed(std::make_unique<AppendCommand>(trace, "x"));

    REQUIRE(trace == std::vector<std::string>{"x"}); // execute() was NOT called again
    REQUIRE(stack.undo_depth() == 1);
    REQUIRE(stack.redo_depth() == 0);

    REQUIRE(stack.undo());            // undo() inverts (pop_back)
    REQUIRE(trace.empty());
    REQUIRE(stack.redo());            // redo() re-runs execute() (re-append)
    REQUIRE(trace == std::vector<std::string>{"x"});
}

TEST_CASE("push_executed clears the redo stack", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    stack.execute(std::make_unique<AppendCommand>(trace, "a"));
    stack.undo();
    REQUIRE(stack.can_redo());

    trace.push_back("b");
    stack.push_executed(std::make_unique<AppendCommand>(trace, "b"));
    REQUIRE_FALSE(stack.can_redo());
}
```

- [ ] **Step 3: Build → confirm both fail** (`remove_value`/`push_executed` undefined):

```bash
cmake --build build/debug -j 2>&1 | tail -5
```
Expected: compile errors naming `remove_value` and `push_executed`.

- [ ] **Step 4: Implement `remove_value` in `engine/reflection/component_reflection.hpp`**

Inside `register_component<T>(...)`, register the remover thunk immediately AFTER the `setters_[name] = ...` block and BEFORE `savers_[std::move(name)] = ...` (which moves `name`):
```cpp
        removers_[name] = [](World& world, EntityID e) -> bool
        {
            return world.remove_component<T>(e);
        };
```
Add the public method (next to `set_value`):
```cpp
    // Remove component `name` from `e`. False if unregistered or the entity had no such component.
    bool remove_value(World& world, EntityID e, std::string_view name) const
    {
        auto it = removers_.find(std::string(name));
        return it == removers_.end() ? false : it->second(world, e);
    }
```
Add the member map (in `private:`, next to `setters_`):
```cpp
    using Remover = std::function<bool(World&, EntityID)>;
    std::unordered_map<std::string, Remover> removers_;
```

- [ ] **Step 5: Implement `push_executed` in `foundation/commands/command_stack.hpp`**

Add this method right after `execute(...)`:
```cpp
    // Record an already-executed command WITHOUT calling execute() again. Clears redo history,
    // exactly like execute(). redo() re-runs execute(); undo() inverts it.
    void push_executed(std::unique_ptr<Command> command)
    {
        undone_.clear();
        applied_.push_back(std::move(command));
    }
```

- [ ] **Step 6: Build + run both suites**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "remove_value|push_executed" --output-on-failure
```
Expected: the new cases PASS.

- [ ] **Step 7: Commit**

```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_value_test.cpp foundation/commands/command_stack.hpp tests/unit/commands/command_stack_test.cpp
git commit -m "feat(reflection,commands): remove_value thunk + CommandStack::push_executed"
```

---

### Task 2: `SetComponentCommand` (engine/commands)

**Files:**
- Create: `engine/commands/set_component_command.hpp`
- Test: `tests/unit/commands/set_component_command_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ComponentReflection::component_value`/`set_value` (S2), `remove_value` (Task 1); `Command` base (#11).
- Produces: `mosaic::engine::commands::SetComponentCommand` (ctor `(World&, ComponentReflection&, EntityID, std::string, Value)`; `execute`/`undo`/`id`/`parameters`; `bool valid()`).

- [ ] **Step 1: Write the failing test `tests/unit/commands/set_component_command_test.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/commands/set_component_command.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::commands::SetComponentCommand;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

TEST_CASE("SetComponentCommand executes then undoes to the prior value", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    Transform desired{}; desired.position = Vec3{9, 2, 3};
    SetComponentCommand cmd(world, types, e, "Transform", to_value(desired));
    cmd.execute();
    REQUIRE(cmd.valid());
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    cmd.undo();
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // prior restored
}

TEST_CASE("SetComponentCommand undo of an add exactly removes the component", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity(); // no Transform yet

    SetComponentCommand cmd(world, types, e, "Transform", to_value(Transform{}));
    cmd.execute();
    REQUIRE(cmd.valid());
    REQUIRE(world.has_component<Transform>(e)); // added
    cmd.undo();
    REQUIRE_FALSE(world.has_component<Transform>(e)); // removed, not left at default
}

TEST_CASE("SetComponentCommand is invalid for an unregistered component", "[commands]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();

    SetComponentCommand cmd(world, types, e, "Nope", Value{});
    cmd.execute();
    REQUIRE_FALSE(cmd.valid()); // set_value on an unregistered name -> false
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after the last `unit/commands/...` entry:
```cmake
  unit/commands/set_component_command_test.cpp
```
Build → FAIL (`set_component_command.hpp` missing).

- [ ] **Step 3: Create `engine/commands/set_component_command.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::commands
{
// Undoable "set component `name` on entity `e` to `next`". Captures the prior value (or its
// absence) at construction, so undo restores exactly what was there: the prior value if the
// component existed, or removal if the set added it. Reflection-derived via S2's
// component_value/set_value + Task 1's remove_value — works for any registered component.
class SetComponentCommand final : public mosaic::foundation::commands::Command
{
public:
    using Value = mosaic::foundation::serialization::Value;
    using EntityID = mosaic::foundation::EntityID;
    using ComponentReflection = mosaic::engine::reflection::ComponentReflection;

    SetComponentCommand(World& world, ComponentReflection& types, EntityID e, std::string name,
                        Value next)
        : world_(world), types_(types), e_(e), name_(std::move(name)), next_(std::move(next)),
          prior_(types_.component_value(world_, e_, name_)), had_component_(prior_.has_value())
    {
    }

    std::string id() const override { return "world.set_component"; }

    // True once execute() has applied successfully. A command whose set is rejected (bad
    // entity/name/value) is invalid and should not be recorded.
    bool valid() const { return applies_; }

    void execute() override { applies_ = types_.set_value(world_, e_, name_, next_); }

    void undo() override
    {
        if (had_component_) { types_.set_value(world_, e_, name_, *prior_); }
        else { types_.remove_value(world_, e_, name_); }
    }

    Value parameters() const override
    {
        mosaic::foundation::serialization::Table t;
        t["entity"] = Value{static_cast<std::int64_t>(e_.value())};
        t["component"] = Value{name_};
        t["value"] = next_;
        return Value{std::move(t)};
    }

private:
    World& world_;
    ComponentReflection& types_;
    EntityID e_;
    std::string name_;
    Value next_;
    std::optional<Value> prior_;
    bool had_component_ = false;
    bool applies_ = false;
};
} // namespace mosaic::engine::commands
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "SetComponentCommand" --output-on-failure
```
Expected: the three cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/commands/set_component_command.hpp tests/unit/commands/set_component_command_test.cpp tests/CMakeLists.txt
git commit -m "feat(commands): SetComponentCommand (undoable, reflection-derived)"
```

---

### Task 3: `bind_commands` + entity builtins + command-routed `set_component` + undo/redo

Adds the neutral interface method AND the backend implementation together (the pure-virtual must be implemented to compile), plus two headless integration tests.

**Files:**
- Modify: `engine/script/script_engine.hpp` (add `bind_commands`)
- Modify: `modules/script_python/python_script_engine.cpp` (entity builtins, stack-aware set_component, undo/redo, bind_commands)
- Test: `tests/unit/script/script_entities_test.cpp`
- Test: `tests/unit/script/script_commands_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: S2 backend (`bind_world`/`set_global`/`get_component`/`set_component`, marshaling), `SetComponentCommand` (Task 2), `CommandStack::push_executed` (Task 1).
- Produces: `ScriptEngine::bind_commands(CommandStack&)`; Python builtins `create_entity`/`destroy_entity`/`entity_count`/`entities`/`is_valid`/`undo`/`redo`/`undo_depth`; stack-aware `set_component`.

- [ ] **Step 1: Write the failing entity test `tests/unit/script/script_entities_test.cpp`**

```cpp
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a script creates entities (immediate) and queues destruction (deferred)", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    // Pre-create a victim in C++ so the test holds its id to check after the tick commit.
    const auto victim = world.create_entity();

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("victim", Value(static_cast<std::uint64_t>(victim.value())));

    const auto r = engine->run_string(
        "a = create_entity()\n"
        "b = create_entity()\n"
        "assert entity_count() == 3\n"                 // victim + a + b, creation is immediate
        "assert len(entities()) == 3\n"
        "assert is_valid(a)\n"
        "assert destroy_entity(victim) == True\n"      // queued (deferred to tick)
        "assert destroy_entity(999999) == False\n"     // invalid id
        "assert entity_count() == 3\n"                 // still 3: destruction not committed yet
        "assert is_valid(victim) == True\n");
    REQUIRE(r.ok);
    REQUIRE(world.entity_count() == 3);

    world.tick(0.0f); // commit the deferred destruction
    REQUIRE(world.entity_count() == 2);
    REQUIRE_FALSE(world.is_valid(victim));
}
```

- [ ] **Step 2: Write the failing commands test `tests/unit/script/script_commands_test.cpp`**

```cpp
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a bound stack makes script set_component undoable", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    CommandStack stack;
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->bind_commands(stack);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['x'] = 9.0\n"
        "assert set_component(eid, 'Transform', t)\n"
        "assert undo_depth() == 1\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);

    r = engine->run_string("assert undo()\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f); // reverted

    r = engine->run_string("assert redo()\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f); // re-applied
}

TEST_CASE("a failed set through the stack is not recorded", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    CommandStack stack;
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->bind_commands(stack);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    const auto r = engine->run_string(
        "assert set_component(eid, 'Nope', {}) == False\n"
        "assert undo_depth() == 0\n");
    REQUIRE(r.ok);
}

TEST_CASE("without a bound stack, set_component still works and undo is a no-op", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::uint64_t>(e.value())));

    const auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['y'] = 4.0\n"
        "assert set_component(eid, 'Transform', t)\n"  // S2 direct path
        "assert undo() == False\n");                    // no stack bound
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.y == 4.0f);
}
```

- [ ] **Step 3: Register both tests + confirm failure**

Add to `tests/CMakeLists.txt` after `unit/script/value_marshal_test.cpp`:
```cmake
  unit/script/script_entities_test.cpp
  unit/script/script_commands_test.cpp
```
Build → FAIL (`bind_commands` not declared; builtins missing).

- [ ] **Step 4: Add `bind_commands` to the neutral interface `engine/script/script_engine.hpp`**

Add the include (still no pybind11):
```cpp
#include "foundation/commands/command_stack.hpp"
```
Add to the `ScriptEngine` class (after `set_global`):
```cpp
    // Bind an undoable command stack. Once bound, script set_component runs through a
    // SetComponentCommand (undoable via undo()/redo()); entity lifecycle stays direct. Non-owning;
    // the caller keeps the stack alive across run_string calls. Optional (S2 behavior without it).
    virtual void bind_commands(mosaic::foundation::commands::CommandStack& stack) = 0;
```

- [ ] **Step 5: Implement the backend in `modules/script_python/python_script_engine.cpp`**

Read the current file first. Add includes:
```cpp
#include <memory>

#include "engine/commands/set_component_command.hpp"
#include "foundation/commands/command_stack.hpp"
```
In `bind_world`, KEEP the existing `get_component` install, REPLACE the `set_component` install with the stack-aware version below, and ADD the five entity builtins:
```cpp
        globals_["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                const auto e = mosaic::foundation::EntityID(eid);
                const mosaic::foundation::serialization::Value v = py_to_value(value);
                if (stack_ != nullptr)
                {
                    auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
                        *world_, *types_, e, name, v);
                    cmd->execute();
                    if (!cmd->valid()) { return false; }   // failed set: not recorded
                    stack_->push_executed(std::move(cmd));
                    return true;
                }
                return types_->set_value(*world_, e, name, v);
            });
        globals_["create_entity"] = py::cpp_function(
            [this]() -> std::uint64_t { return world_->create_entity().value(); });
        globals_["destroy_entity"] = py::cpp_function(
            [this](std::uint64_t eid) -> bool
            {
                const auto e = mosaic::foundation::EntityID(eid);
                if (!world_->is_valid(e)) { return false; }
                world_->destroy_entity(e);
                return true;
            });
        globals_["entity_count"] = py::cpp_function(
            [this]() -> std::uint64_t { return static_cast<std::uint64_t>(world_->entity_count()); });
        globals_["entities"] = py::cpp_function(
            [this]() -> py::list
            {
                py::list out;
                world_->for_each_entity([&](mosaic::foundation::EntityID e)
                                        { out.append(py::int_(e.value())); });
                return out;
            });
        globals_["is_valid"] = py::cpp_function(
            [this](std::uint64_t eid) -> bool
            { return world_->is_valid(mosaic::foundation::EntityID(eid)); });
```
Add the `bind_commands` override (next to `bind_world`):
```cpp
    void bind_commands(mosaic::foundation::commands::CommandStack& stack) override
    {
        stack_ = &stack;
        globals_["undo"] =
            py::cpp_function([this]() -> bool { return stack_ != nullptr && stack_->undo(); });
        globals_["redo"] =
            py::cpp_function([this]() -> bool { return stack_ != nullptr && stack_->redo(); });
        globals_["undo_depth"] = py::cpp_function(
            [this]() -> std::uint64_t
            { return stack_ != nullptr ? static_cast<std::uint64_t>(stack_->undo_depth()) : 0; });
    }
```
Add the member (in `private:`, next to `world_`/`types_`):
```cpp
    mosaic::foundation::commands::CommandStack* stack_ = nullptr;
```
(The `set_component` closure reads `this->stack_` at call time, so binding the stack after `bind_world` still routes through it. With no stack bound, it falls back to S2's direct `set_value`, so S2 tests are unaffected.)

- [ ] **Step 6: Build + run the new tests, then the full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "creates, lists|bound stack|failed set|without a bound stack" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the new `[script]` cases PASS; full suite green (S2 script tests still pass).

- [ ] **Step 7: Commit**

```bash
git add engine/script/script_engine.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_entities_test.cpp tests/unit/script/script_commands_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): entity builtins + bind_commands + undoable set_component (undo/redo)"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 3, note S3 complete + point to S4 (`ScriptComponent` — scripted `on_update` per tick). Commit on this branch (rides in the S3 PR). Update the running test count.
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main` (this branch was cut off the S2 branch; if S2 (#78) has not merged when finishing, the PR stacks on S2 — note that in the PR body).
- The GIL/single-thread assumption from S1/S2 continues to hold (all script calls on the main thread).
- **Toggle Play is deliberately NOT in S3** — `PlayState` is studio-tier; it lands in S5.
