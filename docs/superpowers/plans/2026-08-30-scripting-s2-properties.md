# Scripting S2 — reflection → Python property bindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python script reads/sets a reflected component's properties generically — `get_component(eid,'Transform')` → dict, edit, `set_component(...)` → C++ sees it — via the #10 `Value` DOM + #18 reflection.

**Architecture:** `ComponentReflection` gains generic `component_value`/`set_value` (engine, no Python). The neutral `ScriptEngine` gains `bind_world`/`set_global`. The Python backend marshals `Value`↔Python and installs `get_component`/`set_component` closing over the bound world.

**Tech Stack:** C++20, CMake, Catch2, pybind11 (from S1), the #10 `Value`/`to_value`/`from_value`.

**Spec:** `docs/superpowers/specs/2026-08-30-scripting-s2-properties-design.md`

## Global Constraints

- **Tier wall:** `engine/reflection` + `engine/script` stay Python-free; ALL pybind11/marshaling in `modules/script_python`. `component_value`/`set_value` are ordinary reflection methods.
- **Reflection-derived (#18):** get/set go through the reflected `to_value`/`from_value` thunks; no per-component code.
- **`Value` DOM (#10):** `variant<monostate,bool,int64,uint64,double,string,Array,Table>`; `Value::is<T>()`/`as<T>()`; `Table` iterates `pair<string,Value>` via `begin/end`. `EntityID` crosses as `uint64 .value()` ↔ `explicit EntityID(uint64)`.
- **Never throw into Python / into C++ callers:** `set_value` catches `SerializationError`→false; the script API returns `None`/`False`/captured-error.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::reflection`, `mosaic::engine::script`, `mosaic::script::python`.

---

### Task 1: `ComponentReflection` generic get/set

**Files:**
- Modify: `engine/reflection/component_reflection.hpp`
- Test: `tests/unit/reflection/component_value_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<Value> ComponentReflection::component_value(World&, EntityID, std::string_view) const;` and `bool ComponentReflection::set_value(World&, EntityID, std::string_view, const Value&, const ReferenceResolver& = default_resolver()) const;`

- [ ] **Step 1: Write the failing test `tests/unit/reflection/component_value_test.cpp`**

```cpp
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::to_value;
using mosaic::foundation::serialization::Value;

namespace
{
ComponentReflection make_types(TypeRegistry& r) { ComponentReflection t; register_builtin_components(r, t); return t; }
} // namespace

TEST_CASE("component_value returns the live component as a Value", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    auto v = types.component_value(world, e, "Transform");
    REQUIRE(v.has_value());
    REQUIRE(v->is<mosaic::foundation::serialization::Table>()); // a component serializes to a table
    // Unknown / absent -> nullopt.
    REQUIRE_FALSE(types.component_value(world, e, "Nope").has_value());
    auto e2 = world.create_entity();
    REQUIRE_FALSE(types.component_value(world, e2, "Transform").has_value()); // e2 has no Transform
}

TEST_CASE("set_value updates an existing component in place", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});

    Transform desired{}; desired.position = Vec3{7, 8, 9};
    REQUIRE(types.set_value(world, e, "Transform", to_value(desired)));
    REQUIRE(world.get_component<Transform>(e)->position.x == 7.0f);
    REQUIRE(world.get_component<Transform>(e)->position.z == 9.0f);
}

TEST_CASE("set_value adds the component when absent, and rejects unknown/bad input", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity(); // no Transform yet

    Transform desired{}; desired.position = Vec3{4, 0, 0};
    REQUIRE(types.set_value(world, e, "Transform", to_value(desired))); // adds it
    REQUIRE(world.has_component<Transform>(e));
    REQUIRE(world.get_component<Transform>(e)->position.x == 4.0f);

    REQUIRE_FALSE(types.set_value(world, e, "NotAType", to_value(desired))); // unregistered -> false
}
```

- [ ] **Step 2: Register the test + run to fail**

Add to `tests/CMakeLists.txt` after the last `unit/reflection/...` entry:
```cmake
  unit/reflection/component_value_test.cpp
```
Build → FAIL (`component_value`/`set_value` undefined).

- [ ] **Step 3: Add the thunks + methods to `engine/reflection/component_reflection.hpp`**

Add `#include <optional>` at the top. Inside `register_component<T>(...)`, register `valuers_`/`setters_` immediately BEFORE the existing `savers_[std::move(name)] = ...` line (which moves `name`, so these two must come first while `name` is still valid):
```cpp
        valuers_[name] = [](World& world, EntityID e) -> std::optional<Value>
        {
            if (T* c = world.get_component<T>(e))
            {
                return mosaic::foundation::serialization::to_value(*c);
            }
            return std::nullopt;
        };
        setters_[name] = [](World& world, EntityID e, const Value& value,
                            const ReferenceResolver& resolver) -> bool
        {
            T c{};
            try
            {
                mosaic::foundation::serialization::from_value(value, c, resolver);
            }
            catch (const mosaic::foundation::serialization::SerializationError&)
            {
                return false;
            }
            if (T* existing = world.get_component<T>(e))
            {
                *existing = std::move(c);
                return true;
            }
            return world.add_component<T>(e, std::move(c));
        };
```
Add the public methods (near `inspect`):
```cpp
    // Live component `name` on `e` as a Value (to_value), or nullopt if unregistered/absent.
    std::optional<Value> component_value(World& world, EntityID e, std::string_view name) const
    {
        auto it = valuers_.find(std::string(name));
        return it == valuers_.end() ? std::nullopt : it->second(world, e);
    }

    // Deserialize `value` and apply to `e`'s component `name` (update in place if present, else
    // add). False if unregistered, invalid entity, or deserialization fails. Never throws.
    bool set_value(World& world, EntityID e, std::string_view name, const Value& value,
                   const ReferenceResolver& resolver =
                       mosaic::foundation::serialization::default_resolver()) const
    {
        auto it = setters_.find(std::string(name));
        return it == setters_.end() ? false : it->second(world, e, value, resolver);
    }
```
Add the member maps (in `private:`):
```cpp
    using Valuer = std::function<std::optional<Value>(World&, EntityID)>;
    std::unordered_map<std::string, Valuer> valuers_;
    using Setter = std::function<bool(World&, EntityID, const Value&, const ReferenceResolver&)>;
    std::unordered_map<std::string, Setter> setters_;
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "component_value|set_value" --output-on-failure
```
Expected: the three cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_value_test.cpp tests/CMakeLists.txt
git commit -m "feat(reflection): generic component_value/set_value (Value get/set by name)"
```

---

### Task 2: `bind_world`/`set_global` + Python marshaling + script API

Adds the neutral interface methods AND the backend implementation together (the interface methods are pure-virtual, so the backend must implement them to compile). Marshaling is module-internal and exercised through the script round-trip.

**Files:**
- Modify: `engine/script/script_engine.hpp` (add `bind_world`/`set_global`)
- Create: `modules/script_python/value_marshal.hpp` (module-internal marshaling)
- Modify: `modules/script_python/python_script_engine.cpp` (implement + install the API)
- Test: `tests/unit/script/script_world_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ComponentReflection::component_value`/`set_value` (Task 1); S1's `PythonScriptEngine`.
- Produces: `ScriptEngine::bind_world(World&, ComponentReflection&)`, `ScriptEngine::set_global(const std::string&, const Value&)`; Python builtins `get_component(eid,name)`/`set_component(eid,name,value)`.

- [ ] **Step 1: Write the failing integration test `tests/unit/script/script_world_test.cpp`**

```cpp
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("a script mutates a component property and C++ observes it", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{1, 2, 3};
    world.add_component<Transform>(e, t);

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));

    const auto r = engine->run_string(
        "t = get_component(eid, 'Transform')\n"
        "t['position']['x'] = 9.0\n"
        "set_component(eid, 'Transform', t)\n");
    REQUIRE(r.ok);
    REQUIRE(world.get_component<Transform>(e)->position.x == 9.0f);
    REQUIRE(world.get_component<Transform>(e)->position.y == 2.0f); // untouched
}

TEST_CASE("get_component reads the current C++ values into the script", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    Transform t{}; t.position = Vec3{5, 0, 0};
    world.add_component<Transform>(e, t);

    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));
    const auto r = engine->run_string("print(get_component(eid, 'Transform')['position']['x'])");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("5.0") != std::string::npos);
}

TEST_CASE("script property errors return None/False, not a crash", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    auto engine = create_python_script_engine();
    engine->bind_world(world, types);
    engine->set_global("eid", Value(static_cast<std::int64_t>(e.value())));

    REQUIRE(engine->run_string("assert get_component(eid, 'Nope') is None").ok);      // unknown -> None
    REQUIRE(engine->run_string("assert set_component(eid, 'Nope', {}) == False").ok); // unknown -> False
}

TEST_CASE("set_global marshals scalars and lists into the script", "[script]")
{
    auto engine = create_python_script_engine();
    engine->set_global("n", Value(static_cast<std::int64_t>(42)));
    engine->set_global("s", Value(std::string("hi")));
    mosaic::foundation::serialization::Array arr;
    arr.push_back(Value(static_cast<std::int64_t>(1)));
    arr.push_back(Value(static_cast<std::int64_t>(2)));
    engine->set_global("xs", Value(std::move(arr)));
    const auto r = engine->run_string("print(n, s, xs[0] + xs[1])");
    REQUIRE(r.ok);
    REQUIRE(r.output.find("42 hi 3") != std::string::npos);
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/script/script_engine_test.cpp`:
```cmake
  unit/script/script_world_test.cpp
```
Build → FAIL (`bind_world`/`set_global` not declared).

- [ ] **Step 3: Add `bind_world`/`set_global` to the neutral interface `engine/script/script_engine.hpp`**

Add includes (still no pybind11):
```cpp
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/value.hpp"
```
Add to the `ScriptEngine` class (after `run_string`):
```cpp
    // Make get_component/set_component available to scripts, bound to this world + component set.
    // The engine holds non-owning pointers; the caller keeps them alive across run_string calls.
    virtual void bind_world(mosaic::engine::World& world,
                            mosaic::engine::reflection::ComponentReflection& types) = 0;

    // Inject a value into the script's global namespace (e.g. an entity handle), marshaled from
    // the #10 Value DOM.
    virtual void set_global(const std::string& name,
                            const mosaic::foundation::serialization::Value& value) = 0;
```

- [ ] **Step 4: Write `modules/script_python/value_marshal.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pybind11/pybind11.h>

#include "foundation/serialization/value.hpp"

namespace mosaic::script::python
{
namespace py = pybind11;

// #10 Value DOM -> Python object. Requires a running interpreter + the GIL (held by the caller).
inline py::object value_to_py(const mosaic::foundation::serialization::Value& v)
{
    namespace ser = mosaic::foundation::serialization;
    if (v.is<std::monostate>()) { return py::none(); }
    if (v.is<bool>()) { return py::bool_(v.as<bool>()); }
    if (v.is<std::int64_t>()) { return py::int_(v.as<std::int64_t>()); }
    if (v.is<std::uint64_t>()) { return py::int_(v.as<std::uint64_t>()); }
    if (v.is<double>()) { return py::float_(v.as<double>()); }
    if (v.is<std::string>()) { return py::str(v.as<std::string>()); }
    if (v.is<ser::Array>())
    {
        py::list list;
        for (const auto& e : v.as<ser::Array>()) { list.append(value_to_py(e)); }
        return list;
    }
    if (v.is<ser::Table>())
    {
        py::dict dict;
        for (const auto& [key, val] : v.as<ser::Table>()) { dict[py::str(key)] = value_to_py(val); }
        return dict;
    }
    return py::none();
}

// Python object -> #10 Value DOM. Throws std::runtime_error on an unsupported type (surfaces to
// the script as a normal exception).
inline mosaic::foundation::serialization::Value py_to_value(const py::handle& o)
{
    using mosaic::foundation::serialization::Array;
    using mosaic::foundation::serialization::Table;
    using mosaic::foundation::serialization::Value;
    if (o.is_none()) { return Value{}; }
    if (py::isinstance<py::bool_>(o)) { return Value(o.cast<bool>()); }
    if (py::isinstance<py::int_>(o)) { return Value(o.cast<std::int64_t>()); }
    if (py::isinstance<py::float_>(o)) { return Value(o.cast<double>()); }
    if (py::isinstance<py::str>(o)) { return Value(o.cast<std::string>()); }
    if (py::isinstance<py::list>(o) || py::isinstance<py::tuple>(o))
    {
        Array a;
        for (const auto& e : o) { a.push_back(py_to_value(e)); }
        return Value(std::move(a));
    }
    if (py::isinstance<py::dict>(o))
    {
        Table t;
        for (const auto& item : py::reinterpret_borrow<py::dict>(o))
        {
            t[item.first.cast<std::string>()] = py_to_value(item.second);
        }
        return Value(std::move(t));
    }
    throw std::runtime_error("unsupported Python type for a Mosaic Value");
}
} // namespace mosaic::script::python
```

- [ ] **Step 5: Implement `bind_world`/`set_global` + install the API in `modules/script_python/python_script_engine.cpp`**

Read the current file first. Add `#include "modules/script_python/value_marshal.hpp"`, `#include "engine/reflection/component_reflection.hpp"`, `#include "engine/world/world.hpp"`, `#include <pybind11/functional.h>`. In `PythonScriptEngine`, add members and the two overrides:
```cpp
    void bind_world(mosaic::engine::World& world,
                    mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        globals_["get_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> py::object
            {
                auto v = types_->component_value(*world_, mosaic::foundation::EntityID(eid), name);
                return v ? value_to_py(*v) : py::none();
            });
        globals_["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                return types_->set_value(*world_, mosaic::foundation::EntityID(eid), name,
                                         py_to_value(value));
            });
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        globals_[py::str(name)] = value_to_py(value);
    }
```
And the members (in `private:`):
```cpp
    mosaic::engine::World* world_ = nullptr;
    mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
```
(The GIL is held by the main thread post-init; these run on it. `py::cpp_function` with a capturing lambda is standard. `set_component`'s `py_to_value` may throw `std::runtime_error` on a bad type — pybind11 converts it to a Python exception, which the script's `run_string` captures; that is the intended behavior.)

- [ ] **Step 6: Build + run the script-world tests, then the full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "mutates a component|reads the current|property errors|set_global marshals" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: all four `[script]` cases PASS; full suite green.

- [ ] **Step 7: Commit**

```bash
git add engine/script/script_engine.hpp modules/script_python/value_marshal.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_world_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): bind_world + set_global + Value<->Python marshaling (get/set_component)"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, note S2 complete + point to S3 (world/commands from a script). Commit on this branch (rides in the S2 PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- The GIL/single-thread assumption from S1 continues to hold (all script calls on the main thread).
