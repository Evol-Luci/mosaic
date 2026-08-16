# Reflection System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the reflection system (#18) — a foundation-layer type registry (name/`TypeID`/field metadata) plus an engine-layer bridge that constructs a component from `(type-name, Value)` and attaches it to a `World` with no concrete type at the call site.

**Architecture:** Move the stable within-run `TypeID` generator down into `foundation/reflection` and re-export it from `engine/ecs/component_type_id.hpp` (subsuming the #9 shim). Add a header-only `TypeRegistry` in `foundation/reflection` that registers reflected types by name and enumerates their fields via the existing `reflect()` visitor. Add a header-only `ComponentReflection` in `engine/reflection` that stores per-type attach thunks (which know `T`) and deserializes a `Value` into a fresh component via the existing templated `from_value`, then `world.add_component<T>`.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-reflection-system-design.md](../specs/2026-08-16-reflection-system-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `foundation/` knows nothing above it; `engine/` depends only on `foundation/` and other `engine/` code — never upward. (See [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md).) `foundation/reflection` must never mention `World`.
- **Namespaces:** foundation reflection is `mosaic::foundation::reflection`; engine reflection is `mosaic::engine::reflection`; ECS code is `mosaic::engine::ecs`; the serialization bridge is `mosaic::foundation::serialization`.
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` methods/functions, `PascalCase` types, trailing-underscore private members (`by_id_`).
- **Identity rule:** the persisted identity of a type is its **name** (string); `TypeID` is a runtime-only handle. Never persist a `TypeID`.
- **Testing:** Catch2, unit tests under `tests/unit/reflection/`, tag `[reflection]`; register each new `.cpp` in `tests/CMakeLists.txt`. TDD — write the failing test first and watch it fail before implementing.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[reflection]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/practical-williams-bde237`).

## File Structure

| File | Responsibility |
|------|----------------|
| `foundation/reflection/type_id.hpp` (create) | `TypeID` + `type_id<T>()` — stable within-run handle. |
| `engine/ecs/component_type_id.hpp` (replace) | Re-export `foundation::reflection::TypeID` / `type_id<T>()` as `ComponentTypeID` / `component_type_id<T>()`. |
| `foundation/reflection/type_registry.hpp` (create) | `FieldInfo`, `TypeInfo`, `field_type_name<M>()`, `TypeRegistry`. |
| `engine/reflection/component_reflection.hpp` (create) | `ComponentReflection`: `register_component<T>` + `construct(...)`. |
| `tests/unit/reflection/type_id_test.cpp` (create) | Task 1 test. |
| `tests/unit/reflection/type_registry_test.cpp` (create) | Task 2 test. |
| `tests/unit/reflection/component_reflection_test.cpp` (create) | Tasks 3 & 4 tests. |
| `tests/CMakeLists.txt` (modify) | Register the three new test files. |

No `CMakeLists.txt` *library* changes: both new headers are header-only and reachable via the existing `${CMAKE_SOURCE_DIR}` include root; `mosaic_engine` already links `mosaic_foundation`, and the tests already link `mosaic_serialization`.

---

### Task 1: `TypeID` in foundation + reconcile the `ComponentTypeID` shim

**Files:**
- Create: `foundation/reflection/type_id.hpp`
- Replace: `engine/ecs/component_type_id.hpp`
- Test: `tests/unit/reflection/type_id_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `mosaic::foundation::reflection::TypeID` (`= std::uint32_t`) and `template<class T> TypeID type_id() noexcept` — a static-local id, stable within a run, distinct per type.
  - `mosaic::engine::ecs::ComponentTypeID` (now an alias of `TypeID`) and `template<class T> ComponentTypeID component_type_id() noexcept` (forwards to `type_id<T>()`), so existing `ComponentRegistry`/`System` call sites are unchanged.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/type_id_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_type_id.hpp"
#include "foundation/reflection/type_id.hpp"

using mosaic::foundation::reflection::type_id;
using mosaic::foundation::reflection::TypeID;

namespace
{
struct Alpha {};
struct Beta {};
} // namespace

TEST_CASE("type_id is stable within a run and distinct across types", "[reflection]")
{
    const TypeID a1 = type_id<Alpha>();
    const TypeID a2 = type_id<Alpha>();
    const TypeID b = type_id<Beta>();

    REQUIRE(a1 == a2);
    REQUIRE(a1 != b);
}

TEST_CASE("engine component_type_id re-exports foundation type_id", "[reflection]")
{
    REQUIRE(mosaic::engine::ecs::component_type_id<Alpha>() == type_id<Alpha>());
    REQUIRE(mosaic::engine::ecs::component_type_id<Beta>() == type_id<Beta>());
    static_assert(std::is_same_v<mosaic::engine::ecs::ComponentTypeID, TypeID>,
                  "ComponentTypeID must alias foundation TypeID");
}
```

Add `#include <type_traits>` at the top of that file (for the `static_assert`).

Register it in `tests/CMakeLists.txt` — add this line inside the `add_executable(mosaic_unit_tests ...)` source list, immediately after `unit/commands/command_event_integration_test.cpp`:

```cmake
  unit/reflection/type_id_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: foundation/reflection/type_id.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/reflection/type_id.hpp`:

```cpp
#pragma once

#include <cstdint>

namespace mosaic::foundation::reflection
{

// A stable *within-run* type handle. Assignment order is unspecified across runs;
// the durable identity of a type is its registered NAME, not this integer. This
// subsumes the minimal ComponentTypeID shim the ECS core (#9) shipped: there is
// now one id source, re-exported by engine/ecs/component_type_id.hpp.
using TypeID = std::uint32_t;

namespace detail
{
// Not thread-safe; assumes single-threaded type registration.
inline TypeID next_type_id() noexcept
{
    static TypeID counter = 0;
    return counter++;
}
} // namespace detail

template <typename T>
TypeID type_id() noexcept
{
    static const TypeID id = detail::next_type_id();
    return id;
}

} // namespace mosaic::foundation::reflection
```

Replace the entire contents of `engine/ecs/component_type_id.hpp` with:

```cpp
#pragma once

#include "foundation/reflection/type_id.hpp"

namespace mosaic::engine::ecs
{

// The ECS keys component storage by a per-type integer id. That identity now
// comes from the reflection system's single source (foundation/reflection):
// ComponentTypeID is an alias of reflection::TypeID and component_type_id<T>()
// forwards to type_id<T>(), so a component's storage slot and its reflected
// TypeInfo::id are the same integer within a run.
using ComponentTypeID = mosaic::foundation::reflection::TypeID;

template <typename T>
ComponentTypeID component_type_id() noexcept
{
    return mosaic::foundation::reflection::type_id<T>();
}

} // namespace mosaic::engine::ecs
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS. Then run the full suite to confirm the shim re-export regresses nothing:
Run: `ctest --preset debug`
Expected: PASS — all pre-existing `[components]`, `[world]`, `[system]`, serialization tests still green.

- [ ] **Step 5: Commit**

```bash
git add foundation/reflection/type_id.hpp engine/ecs/component_type_id.hpp tests/unit/reflection/type_id_test.cpp tests/CMakeLists.txt
git commit -m "Add foundation TypeID; reconcile ComponentTypeID shim (#18)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `TypeRegistry` — register, look up, enumerate fields

**Files:**
- Create: `foundation/reflection/type_registry.hpp`
- Test: `tests/unit/reflection/type_registry_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `type_id<T>()` (Task 1), the existing `reflect(visitor, value)` ADL contract (`foundation/reflection/reflect.hpp`), and the serialization type-category traits (`is_std_vector_v`, `is_handle_v`) plus `AssetID`/`Handle`/`EntityID`/`Value` types.
- Produces:
  - `struct FieldInfo { std::string name; std::string type; };`
  - `struct TypeInfo { TypeID id; std::string name; std::vector<FieldInfo> fields; };`
  - `template<class M> std::string field_type_name();`
  - `class TypeRegistry` with:
    - `template<class T> const TypeInfo& register_type(std::string name);`
    - `const TypeInfo* find(std::string_view name) const;`
    - `const TypeInfo* find(TypeID id) const;`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/type_registry_test.cpp`:

```cpp
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/reflection/type_id.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::foundation::reflection::TypeID;
using mosaic::foundation::reflection::TypeInfo;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::reflection::type_id;

namespace
{
struct Health
{
    int current;
    int max;
};
template <class V> void reflect(V& visitor, Health& h)
{
    visitor.member("current", h.current);
    visitor.member("max", h.max);
}

struct Other
{
    float x;
};
template <class V> void reflect(V& visitor, Other& o)
{
    visitor.member("x", o.x);
}

struct Bag
{
    std::vector<int> items;
    Health nested;
};
template <class V> void reflect(V& visitor, Bag& b)
{
    visitor.member("items", b.items);
    visitor.member("nested", b.nested);
}
} // namespace

TEST_CASE("register_type records id and name, findable by name and id", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Health>("Health");

    REQUIRE(info.name == "Health");
    REQUIRE(info.id == type_id<Health>());
    REQUIRE(registry.find("Health") == &info);
    REQUIRE(registry.find(info.id) == &info);
}

TEST_CASE("register_type enumerates fields in declaration order with type labels", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Health>("Health");

    REQUIRE(info.fields.size() == 2);
    REQUIRE(info.fields[0].name == "current");
    REQUIRE(info.fields[0].type == "i32");
    REQUIRE(info.fields[1].name == "max");
    REQUIRE(info.fields[1].type == "i32");
}

TEST_CASE("field_type_name labels vectors and nested reflected types", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& info = registry.register_type<Bag>("Bag");

    REQUIRE(info.fields[0].type == "array<i32>");
    REQUIRE(info.fields[1].type == "object");
}

TEST_CASE("find returns nullptr for unknown name and id", "[reflection]")
{
    TypeRegistry registry;
    REQUIRE(registry.find("Nope") == nullptr);
    REQUIRE(registry.find(TypeID{99999}) == nullptr);
}

TEST_CASE("re-registering the same type and name is idempotent", "[reflection]")
{
    TypeRegistry registry;
    const TypeInfo& a = registry.register_type<Health>("Health");
    const TypeInfo& b = registry.register_type<Health>("Health");
    REQUIRE(&a == &b);
}

TEST_CASE("a name collision with a different type throws", "[reflection]")
{
    TypeRegistry registry;
    registry.register_type<Health>("Dup");
    REQUIRE_THROWS_AS(registry.register_type<Other>("Dup"), std::logic_error);
}
```

Register it in `tests/CMakeLists.txt`, after the Task 1 line:

```cmake
  unit/reflection/type_registry_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: foundation/reflection/type_registry.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `foundation/reflection/type_registry.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "foundation/core/entity_id.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/reflection/type_id.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::reflection
{

// A reflected member: its name and a canonical, human-readable type label. The
// label mirrors the serialization encode() categories; it is descriptive (drives
// an inspector column / documentation), NOT a re-parseable type expression.
struct FieldInfo
{
    std::string name;
    std::string type;
};

struct TypeInfo
{
    TypeID id;                       // runtime handle (see type_id.hpp)
    std::string name;                // stable, persisted identity
    std::vector<FieldInfo> fields;   // in reflect() declaration order
};

// Canonical field-type label. Mirrors serialization::encode() so the label set
// stays in step with what actually round-trips.
template <class M>
std::string field_type_name()
{
    namespace ser = mosaic::foundation::serialization;
    using U = std::remove_cv_t<M>;
    if constexpr (std::is_same_v<U, ser::Value>)
    {
        return "value";
    }
    else if constexpr (std::is_same_v<U, bool>)
    {
        return "bool";
    }
    else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
    {
        return "EntityID";
    }
    else if constexpr (std::is_same_v<U, ser::AssetID>)
    {
        return "AssetID";
    }
    else if constexpr (ser::is_handle_v<U>)
    {
        return "Handle";
    }
    else if constexpr (std::is_integral_v<U> && std::is_signed_v<U>)
    {
        return "i" + std::to_string(sizeof(U) * 8);
    }
    else if constexpr (std::is_integral_v<U> && std::is_unsigned_v<U>)
    {
        return "u" + std::to_string(sizeof(U) * 8);
    }
    else if constexpr (std::is_floating_point_v<U>)
    {
        return sizeof(U) == 4 ? "f32" : "f64";
    }
    else if constexpr (std::is_same_v<U, std::string>)
    {
        return "string";
    }
    else if constexpr (ser::is_std_vector_v<U>)
    {
        return "array<" + field_type_name<typename U::value_type>() + ">";
    }
    else if constexpr (Reflectable<U>)
    {
        return "object";
    }
    else
    {
        return "unknown";
    }
}

// A runtime registry of reflected types, keyed by stable name and by TypeID. Owned
// instance, no global. Register once per type; the returned TypeInfo has a stable
// address for the registry's lifetime.
class TypeRegistry
{
public:
    // Records TypeInfo{ type_id<T>(), name, fields-of-T }. Fields are enumerated by
    // running reflect() over a default-constructed T (T must be default-
    // constructible and reflected). Idempotent for the same T already registered
    // under `name`; throws std::logic_error on a re-register under a different name
    // or a name collision with a different type.
    template <class T>
    const TypeInfo& register_type(std::string name)
    {
        const TypeID id = type_id<T>();

        if (auto it = by_id_.find(id); it != by_id_.end())
        {
            const TypeInfo& existing = *it->second;
            if (existing.name != name)
            {
                throw std::logic_error(
                    "TypeRegistry: type already registered under a different name");
            }
            return existing;
        }
        if (by_name_.find(name) != by_name_.end())
        {
            throw std::logic_error(
                "TypeRegistry: name '" + name + "' already registered for a different type");
        }

        auto info = std::make_unique<TypeInfo>();
        info->id = id;
        info->name = name;

        FieldEnumerator enumerator{info->fields};
        T probe{};
        reflect(enumerator, probe);

        const TypeInfo& ref = *info;
        by_name_.emplace(info->name, info.get());
        by_id_.emplace(id, std::move(info));
        return ref;
    }

    const TypeInfo* find(std::string_view name) const
    {
        auto it = by_name_.find(std::string(name));
        return it == by_name_.end() ? nullptr : it->second;
    }

    const TypeInfo* find(TypeID id) const
    {
        auto it = by_id_.find(id);
        return it == by_id_.end() ? nullptr : it->second.get();
    }

private:
    // Visitor that records each member's name + type label into a field list.
    struct FieldEnumerator
    {
        std::vector<FieldInfo>& fields;
        template <class M> void member(std::string_view name, M&)
        {
            fields.push_back(FieldInfo{std::string(name), field_type_name<M>()});
        }
    };

    // by_id_ owns the TypeInfo (stable heap address); by_name_ points into it.
    std::unordered_map<std::string, TypeInfo*> by_name_;
    std::unordered_map<TypeID, std::unique_ptr<TypeInfo>> by_id_;
};

} // namespace mosaic::foundation::reflection
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS (Task 1 + Task 2 `[reflection]` cases).

- [ ] **Step 5: Commit**

```bash
git add foundation/reflection/type_registry.hpp tests/unit/reflection/type_registry_test.cpp tests/CMakeLists.txt
git commit -m "Add TypeRegistry: register, look up, enumerate reflected fields (#18)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `ComponentReflection` — generic construction into a `World`

**Files:**
- Create: `engine/reflection/component_reflection.hpp`
- Test: `tests/unit/reflection/component_reflection_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `TypeRegistry` (Task 2), `mosaic::engine::World` (`create_entity`, `destroy_entity`, `tick`, `add_component<T>`, `get_component<T>`, `EntityID`), and serialization `to_value`/`from_value`/`Value`/`ReferenceResolver`/`default_resolver`.
- Produces:
  - `class mosaic::engine::reflection::ComponentReflection` with:
    - `template<class T> void register_component(foundation::reflection::TypeRegistry&, std::string name);`
    - `bool construct(World&, World::EntityID, std::string_view name, const Value&, const ReferenceResolver& = default_resolver()) const;`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/component_reflection_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Value;
using mosaic::foundation::serialization::to_value;
using EntityID = World::EntityID;

namespace
{
struct Position
{
    float x;
    float y;
};
template <class V> void reflect(V& visitor, Position& p)
{
    visitor.member("x", p.x);
    visitor.member("y", p.y);
}
} // namespace

TEST_CASE("construct attaches a component with no concrete type at the call site", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();

    const Value value = to_value(Position{3.0f, 4.0f});
    REQUIRE(reflector.construct(world, e, "Position", value));

    Position* p = world.get_component<Position>(e);
    REQUIRE(p != nullptr);
    REQUIRE(p->x == 3.0f);
    REQUIRE(p->y == 4.0f);
}

TEST_CASE("construct with an unregistered name returns false and attaches nothing", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();
    const Value value = to_value(Position{1.0f, 1.0f});

    REQUIRE_FALSE(reflector.construct(world, e, "Unknown", value));
    REQUIRE(world.get_component<Position>(e) == nullptr);
}

TEST_CASE("construct onto an invalid entity returns false", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID e = world.create_entity();
    world.destroy_entity(e);
    world.tick(0.0f); // commit destruction -> e is now invalid

    const Value value = to_value(Position{1.0f, 1.0f});
    REQUIRE_FALSE(reflector.construct(world, e, "Position", value));
}
```

Register it in `tests/CMakeLists.txt`, after the Task 2 line:

```cmake
  unit/reflection/component_reflection_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: engine/reflection/component_reflection.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/reflection/component_reflection.hpp`:

```cpp
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::reflection
{

// Bridges a foundation TypeRegistry to generic component construction into a
// World. Holds one attach thunk per registered component type, keyed by name.
// The thunk is captured where T is known (register_component); construct() callers
// (e.g. Scene load) name only a string, never a concrete C++ type.
class ComponentReflection
{
public:
    using Value = mosaic::foundation::serialization::Value;
    using ReferenceResolver = mosaic::foundation::serialization::ReferenceResolver;
    using EntityID = World::EntityID;

    // Registers T's metadata into `registry` AND an attach thunk here, sharing
    // type_id<T>() and `name`.
    template <class T>
    void register_component(mosaic::foundation::reflection::TypeRegistry& registry, std::string name)
    {
        registry.register_type<T>(name);
        attachers_[std::move(name)] =
            [](World& world, EntityID e, const Value& value, const ReferenceResolver& resolver)
        {
            T component{};
            mosaic::foundation::serialization::from_value(value, component, resolver);
            return world.add_component<T>(e, std::move(component));
        };
    }

    // Look up by name, deserialize `value` into a fresh component via the templated
    // from_value, and world.add_component<T>(e, ...). Returns false if `name` is not
    // registered, or whatever add_component returns (false for an invalid entity).
    bool construct(World& world, EntityID e, std::string_view name, const Value& value,
                   const ReferenceResolver& resolver =
                       mosaic::foundation::serialization::default_resolver()) const
    {
        auto it = attachers_.find(std::string(name));
        if (it == attachers_.end())
        {
            return false;
        }
        return it->second(world, e, value, resolver);
    }

private:
    using Attacher = std::function<bool(World&, EntityID, const Value&, const ReferenceResolver&)>;
    std::unordered_map<std::string, Attacher> attachers_;
};

} // namespace mosaic::engine::reflection
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_reflection_test.cpp tests/CMakeLists.txt
git commit -m "Add ComponentReflection: generic component construction into World (#18)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Round-trip acceptance — reflect → to_value → construct

**Files:**
- Modify: `tests/unit/reflection/component_reflection_test.cpp`

**Interfaces:**
- Consumes: the full `ComponentReflection` + `TypeRegistry` API (Tasks 2–3). No new production code — this task codifies acceptance criterion 5 (round-trip) and exercises the recursive `from_value` path through the thunk (nested reflected field + `std::vector` field).

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/reflection/component_reflection_test.cpp`. First add these includes at the top of the file (after the existing includes):

```cpp
#include <string>
#include <vector>
```

Then append these definitions and test at the end of the file. Add the two structs inside the existing anonymous namespace (i.e. reopen it), and the `TEST_CASE` after it:

```cpp
namespace
{
struct Inner
{
    int a;
};
template <class V> void reflect(V& visitor, Inner& i)
{
    visitor.member("a", i.a);
}

struct Complex
{
    Inner inner;
    std::vector<int> values;
    std::string label;
};
template <class V> void reflect(V& visitor, Complex& c)
{
    visitor.member("inner", c.inner);
    visitor.member("values", c.values);
    visitor.member("label", c.label);
}
} // namespace

TEST_CASE("round-trip: reflect -> to_value -> construct reproduces the component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Complex>(registry, "Complex");

    World world;
    const EntityID e = world.create_entity();

    const Complex original{Inner{7}, {1, 2, 3}, "hp"};
    const Value value = to_value(original);
    REQUIRE(reflector.construct(world, e, "Complex", value));

    Complex* got = world.get_component<Complex>(e);
    REQUIRE(got != nullptr);
    REQUIRE(got->inner.a == 7);
    REQUIRE(got->values == std::vector<int>{1, 2, 3});
    REQUIRE(got->label == "hp");
}
```

Note: `to_value(original)` binds `original` (a `const Complex&`) — `to_value` takes `T&`/`T&&` and reflects; the reflected members are read, not written, so `const` is fine via the forwarding overload. If the compiler rejects the `const` binding, drop `const` from `original`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: This task adds only a test over already-built production code, so it may compile and PASS immediately. To confirm the test is actually exercised, first run with a deliberately wrong expectation is unnecessary — instead verify the case name appears and passes:
Run: `./build/debug/tests/mosaic_unit_tests "round-trip: reflect -> to_value -> construct reproduces the component"`
Expected: the case runs and PASSES (1 assertion section, all `REQUIRE`s pass). If it does not compile, fix per the `const` note above.

- [ ] **Step 3: (no new production code)**

This task only adds a test — the round-trip is already supported by Tasks 2–3. Skip to verification.

- [ ] **Step 4: Run the full suite**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]" && ctest --preset debug`
Expected: PASS — the `[reflection]` subset and the full suite are green.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/reflection/component_reflection_test.cpp
git commit -m "Add reflection round-trip acceptance test (nested + vector fields) (#18)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`:
- Add a "built and merged" paragraph for the reflection system (#18) under *Where we are*, mirroring the style of the existing entries: note `foundation/reflection/` (`type_id.hpp`, `type_registry.hpp`) and `engine/reflection/component_reflection.hpp`, that the #9 `ComponentTypeID` shim is now a re-export of `foundation::reflection::TypeID`, and that generic construction into a `World` from `(type-name, Value)` is available — the gap the World left open, now closed.
- In *What to build next*, change the suggested first target from the reflection system to **Scene load/save (#19)**, now unblocked by #18.
- Update the *Suggested sequence* diagram to mark `#18 reflection` done (✓).
- Bump **Last updated** to the implementation date.
- Note the durability rule for future workers: the persisted type identity is the **name**; `TypeID` is a runtime-only handle.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: reflection system built (#18)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Foundation type registry (register, look up by name & id) → Task 2.
- Field enumeration (name + type) → Task 2 (`field_type_name`, `FieldEnumerator`).
- Reconcile/subsume the #9 `ComponentTypeID` shim → Task 1 (re-export).
- Engine generic construction bridge (attach to World, no concrete type at call site) → Task 3.
- Round-trip reflect → to_value → from_value → generic-construct → Task 4.
- One identity source; name is durable, `TypeID` runtime-only → Task 1 (impl) + Task 5 (documented rule).
- Tier contract (foundation never mentions World) → foundation/reflection headers include no engine header (Tasks 1–2); the World-attach thunk lives in engine/reflection (Task 3).
- Error handling: unknown name → `construct` returns false (Task 3 test); name collision → `register_type` throws (Task 2 test); malformed Value → `SerializationError` propagates from `from_value` (existing behavior, unchanged).
- Unit tests under `tests/unit/reflection/` → Tasks 1–4.
- Out-of-scope (foundation-only value round-trip, macros, set-by-name, Scene) → not built; recorded in spec.

**Placeholder scan:** No TBD/TODO in production or test steps. Task 4 Step 3 is an explicit "no production code" note; Task 5 is prose documentation. Both intentional.

**Type consistency:** `type_id<T>()`/`TypeID` (Task 1) are used identically in Tasks 2–3 and every test. `register_type`, `find`, `TypeInfo`, `FieldInfo`, `field_type_name` (Task 2) match their uses. `register_component`, `construct`, `ComponentReflection` (Task 3) match Task 4's calls. `ComponentTypeID`/`component_type_id<T>()` re-export (Task 1) matches the unchanged `ComponentRegistry`/`System` call sites and the `system_test.cpp`/`component_registry_test.cpp` usages verified in the codebase.
</content>
