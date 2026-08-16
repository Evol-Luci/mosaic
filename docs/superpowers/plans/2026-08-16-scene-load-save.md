# Scene Load/Save Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Serialize a `World`'s entity+component graph to JSON and load it back into a fresh `World`, with intra-scene `EntityID` references remapped on load and generic per-type construction via the reflection system (#18).

**Architecture:** Add live-entity enumeration to the ECS (`EntityRegistry::for_each` → `World::for_each_entity`), give `ComponentReflection` a symmetric save direction (a save thunk per type + `save()`/`has()`), then add a header-only `engine/scene/scene.hpp` that projects a `World` to a `Value` document (rendered to JSON via the #10 backend) and rebuilds it in two passes — create entities + build a remap, then generically construct components through a `SceneReferenceResolver`.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-scene-load-save-design.md](../specs/2026-08-16-scene-load-save-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `foundation/` knows nothing above it; `engine/` depends only on `foundation/` and other `engine/` code — never upward. (See [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md).)
- **Namespaces:** ECS is `mosaic::engine::ecs`; `World` is `mosaic::engine`; reflection bridge is `mosaic::engine::reflection`; scene is `mosaic::engine::scene`; serialization is `mosaic::foundation::serialization`.
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` methods/functions, `PascalCase` types, trailing-underscore private members.
- **In-memory only:** Scene operates on JSON strings / `Value`s; no filesystem access.
- **Testing:** Catch2, unit tests under `tests/unit/scene/`, tag `[scene]`; register each new `.cpp` in `tests/CMakeLists.txt`. TDD — write the failing test first and watch it fail before implementing.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[scene]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/practical-williams-bde237`).

## File Structure

| File | Responsibility |
|------|----------------|
| `engine/ecs/entity_registry.hpp` (modify) | Add `template<class Fn> void for_each(Fn&&) const` — live entities in ascending slot order. |
| `engine/world/world.hpp` (modify) | Add `template<class Fn> void for_each_entity(Fn&&) const` — delegate to the registry. |
| `engine/reflection/component_reflection.hpp` (modify) | Add a per-type save thunk + `save(World&, const SaveSink&)` and `has(std::string_view)`. |
| `engine/scene/scene.hpp` (create) | `to_value`/`save_to_json` (Task 3), `from_value`/`load_from_json`/`SceneReferenceResolver` (Task 4). |
| `tests/unit/ecs/entity_registry_iteration_test.cpp` (create) | Task 1 test. |
| `tests/unit/reflection/component_reflection_test.cpp` (modify) | Task 2 test (append). |
| `tests/unit/scene/scene_save_test.cpp` (create) | Task 3 test. |
| `tests/unit/scene/scene_load_test.cpp` (create) | Task 4 test. |
| `tests/CMakeLists.txt` (modify) | Register the new test sources. |

No `CMakeLists.txt` library changes: `engine/scene` is header-only; the JSON backend it calls is in `mosaic_serialization`, already linked by the tests.

---

### Task 1: Live-entity enumeration

**Files:**
- Modify: `engine/ecs/entity_registry.hpp`
- Modify: `engine/world/world.hpp`
- Test: `tests/unit/ecs/entity_registry_iteration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `template<class Fn> void EntityRegistry::for_each(Fn&& fn) const` — invokes `fn(EntityID)` for each live entity in ascending slot order (skips retired and free-listed slots).
  - `template<class Fn> void World::for_each_entity(Fn&& fn) const` — forwards to the registry.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ecs/entity_registry_iteration_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/entity_registry.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::EntityRegistry;
using mosaic::foundation::EntityID;

TEST_CASE("EntityRegistry::for_each visits every live entity", "[entity_registry]")
{
    EntityRegistry registry;
    const EntityID a = registry.create();
    const EntityID b = registry.create();
    const EntityID c = registry.create();
    registry.destroy(b);

    std::vector<EntityID> seen;
    registry.for_each([&](EntityID e) { seen.push_back(e); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == a);
    REQUIRE(seen[1] == c);
}

TEST_CASE("EntityRegistry::for_each on an empty registry visits nothing", "[entity_registry]")
{
    EntityRegistry registry;
    int calls = 0;
    registry.for_each([&](EntityID) { ++calls; });
    REQUIRE(calls == 0);
}

TEST_CASE("World::for_each_entity visits live entities including component-less ones", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity(); // no components

    std::vector<EntityID> seen;
    world.for_each_entity([&](EntityID e) { seen.push_back(e); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == a);
    REQUIRE(seen[1] == b);
}
```

Register it in `tests/CMakeLists.txt` — add inside the `add_executable(mosaic_unit_tests ...)` source list, after `unit/ecs/component_registry_iteration_test.cpp`:

```cmake
  unit/ecs/entity_registry_iteration_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'for_each' is not a member of 'mosaic::engine::ecs::EntityRegistry'`.

- [ ] **Step 3: Write minimal implementation**

(a) In `engine/ecs/entity_registry.hpp`, add a public `for_each` to `EntityRegistry` (after `alive_count()`, before the `private:` label):

```cpp
    // Visit each live entity in ascending slot order. Skips retired slots and
    // slots currently on the free-list. fn is invoked as fn(EntityID).
    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        std::vector<bool> is_free(generations_.size(), false);
        for (std::uint32_t slot : free_list_)
        {
            is_free[slot] = true;
        }
        for (std::size_t i = 0; i < generations_.size(); ++i)
        {
            if (generations_[i] == kRetiredGeneration || is_free[i])
            {
                continue;
            }
            fn(EntityID{static_cast<std::uint32_t>(i), generations_[i]});
        }
    }
```

(b) In `engine/world/world.hpp`, add `for_each_entity` (in the `public:` section, after `entity_count()`):

```cpp
    template <typename Fn>
    void for_each_entity(Fn&& fn) const { entities_.for_each(std::forward<Fn>(fn)); }
```

(`<utility>` for `std::forward` and `<vector>` are already included in these headers.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[entity_registry],[world]"`
Expected: PASS (the new cases plus all pre-existing `[world]` cases).

- [ ] **Step 5: Commit**

```bash
git add engine/ecs/entity_registry.hpp engine/world/world.hpp tests/unit/ecs/entity_registry_iteration_test.cpp tests/CMakeLists.txt
git commit -m "Add live-entity enumeration (EntityRegistry::for_each, World::for_each_entity) (#19)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Save-direction reflection (`ComponentReflection::save`/`has`)

**Files:**
- Modify: `engine/reflection/component_reflection.hpp`
- Test: `tests/unit/reflection/component_reflection_test.cpp` (append)

**Interfaces:**
- Consumes: `World::for_each<T>`, `serialization::to_value`.
- Produces (on `ComponentReflection`):
  - `using SaveSink = std::function<void(EntityID, const std::string& type_name, const Value&)>;`
  - `void save(World& world, const SaveSink& sink) const;` — for each registered type, emit `(entity, name, to_value(component))` for every entity holding it.
  - `bool has(std::string_view name) const;` — true iff `name` is a registered component type.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/reflection/component_reflection_test.cpp` (at the end of the file). It reuses the `Position` type already defined in that file's first anonymous namespace:

```cpp
#include <map>
#include <string>
#include <tuple>

TEST_CASE("ComponentReflection::save emits (entity, name, value) per component", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 2.0f});
    world.add_component<Position>(b, Position{3.0f, 4.0f});

    std::map<std::uint64_t, std::string> names;
    int count = 0;
    reflector.save(world, [&](EntityID e, const std::string& name, const Value&)
    {
        names[e.value()] = name;
        ++count;
    });

    REQUIRE(count == 2);
    REQUIRE(names[a.value()] == "Position");
    REQUIRE(names[b.value()] == "Position");
}

TEST_CASE("ComponentReflection::has reports registered type names", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection reflector;
    reflector.register_component<Position>(registry, "Position");

    REQUIRE(reflector.has("Position"));
    REQUIRE_FALSE(reflector.has("Nope"));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'class mosaic::engine::reflection::ComponentReflection' has no member named 'save'`.

- [ ] **Step 3: Write minimal implementation**

In `engine/reflection/component_reflection.hpp`:

(a) Add `<map>` to the includes (after `#include <functional>`):

```cpp
#include <map>
```

(b) Replace the `register_component` method body so it records BOTH a load thunk and a save thunk (the current body only records the load thunk):

```cpp
    template <class T>
    void register_component(mosaic::foundation::reflection::TypeRegistry& registry, std::string name)
    {
        registry.register_type<T>(name);
        attachers_[name] =
            [](World& world, EntityID e, const Value& value, const ReferenceResolver& resolver)
        {
            T component{};
            mosaic::foundation::serialization::from_value(value, component, resolver);
            return world.add_component<T>(e, std::move(component));
        };
        savers_[std::move(name)] =
            [](World& world, const std::string& type_name, const SaveSink& sink)
        {
            world.for_each<T>([&](EntityID e, T& component)
            {
                sink(e, type_name, mosaic::foundation::serialization::to_value(component));
            });
        };
    }
```

(c) Add the `SaveSink` alias and the `save`/`has` methods (in the `public:` section, after `construct`):

```cpp
    using SaveSink = std::function<void(EntityID, const std::string& type_name, const Value&)>;

    // For every registered component type, emit (entity, name, to_value(component))
    // for each entity that holds it.
    void save(World& world, const SaveSink& sink) const
    {
        for (const auto& [name, saver] : savers_)
        {
            saver(world, name, sink);
        }
    }

    // True iff `name` is a registered component type.
    bool has(std::string_view name) const
    {
        return attachers_.find(std::string(name)) != attachers_.end();
    }
```

(d) Add the `savers_` member (in the `private:` section, after `attachers_`):

```cpp
    using Saver = std::function<void(World&, const std::string&, const SaveSink&)>;
    // std::map (not unordered): iterating in sorted type-name order gives
    // deterministic per-entity component ordering in saved scenes.
    std::map<std::string, Saver> savers_;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS (all `[reflection]` cases, including the earlier ones).

- [ ] **Step 5: Commit**

```bash
git add engine/reflection/component_reflection.hpp tests/unit/reflection/component_reflection_test.cpp
git commit -m "Add save-direction reflection (ComponentReflection::save/has) (#19)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Scene save (`to_value` + `save_to_json`)

**Files:**
- Create: `engine/scene/scene.hpp`
- Test: `tests/unit/scene/scene_save_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `World::for_each_entity` (Task 1), `ComponentReflection::save` (Task 2), `serialization::to_json`, the `Value`/`Table`/`Array` DOM.
- Produces (namespace `mosaic::engine::scene`):
  - `Value to_value(World&, const reflection::ComponentReflection&);`
  - `std::string save_to_json(World&, const reflection::ComponentReflection&);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/scene/scene_save_test.cpp`:

```cpp
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
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

struct Link
{
    EntityID target;
};
template <class V> void reflect(V& visitor, Link& l)
{
    visitor.member("target", l.target);
}

// Find the entity node with the given "id" string in a scene document's array.
const Table& find_node(const Array& nodes, const std::string& id)
{
    for (const Value& node : nodes)
    {
        const Table& t = node.as<Table>();
        if (t.find("id")->as<std::string>() == id)
        {
            return t;
        }
    }
    throw std::runtime_error("node not found: " + id);
}
} // namespace

TEST_CASE("to_value serializes entities, components, empties, and references", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");
    types.register_component<Link>(registry, "Link");

    World world;
    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 2.0f});
    const EntityID b = world.create_entity(); // component-less
    const EntityID c = world.create_entity();
    world.add_component<Link>(c, Link{a});

    const Value doc = mosaic::engine::scene::to_value(world, types);
    const Table& root = doc.as<Table>();
    REQUIRE(root.find("version")->as<std::int64_t>() == 1);

    const Array& nodes = root.find("entities")->as<Array>();
    REQUIRE(nodes.size() == 3);

    // a: Position{1,2}
    const Table& node_a = find_node(nodes, std::to_string(a.value()));
    const Table& comps_a = node_a.find("components")->as<Table>();
    const Table& pos = comps_a.find("Position")->as<Table>();
    REQUIRE(pos.find("x")->as<double>() == 1.0);
    REQUIRE(pos.find("y")->as<double>() == 2.0);

    // b: empty components table
    const Table& node_b = find_node(nodes, std::to_string(b.value()));
    REQUIRE(node_b.find("components")->as<Table>().size() == 0);

    // c: Link.target encoded as a's id string
    const Table& node_c = find_node(nodes, std::to_string(c.value()));
    const Table& link = node_c.find("components")->as<Table>().find("Link")->as<Table>();
    REQUIRE(link.find("target")->as<std::string>() == std::to_string(a.value()));
}

TEST_CASE("save_to_json produces parseable JSON text", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");

    World world;
    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{5.0f, 6.0f});

    const std::string json = mosaic::engine::scene::save_to_json(world, types);
    REQUIRE(json.find("\"version\"") != std::string::npos);
    REQUIRE(json.find("\"Position\"") != std::string::npos);
}
```

Register in `tests/CMakeLists.txt`, after the reflection test lines (i.e. after `unit/reflection/component_reflection_test.cpp`):

```cmake
  unit/scene/scene_save_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `fatal error: engine/scene/scene.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/scene/scene.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/json_backend.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::scene
{

using Value = mosaic::foundation::serialization::Value;

// Serialize every live entity and its registered components to a scene Value:
// { version: 1, entities: [ { id, components: { TypeName: {fields...} } } ] }.
// Component-less entities appear with an empty components table.
inline Value to_value(World& world, const reflection::ComponentReflection& types)
{
    namespace ser = mosaic::foundation::serialization;

    std::unordered_map<std::uint64_t, ser::Table> components_by_entity;
    types.save(world, [&](World::EntityID e, const std::string& name, const Value& value)
    {
        components_by_entity[e.value()][name] = value;
    });

    ser::Array entities;
    world.for_each_entity([&](World::EntityID e)
    {
        ser::Table node;
        node["id"] = Value(std::to_string(e.value()));
        auto it = components_by_entity.find(e.value());
        node["components"] = Value(it != components_by_entity.end() ? it->second : ser::Table{});
        entities.push_back(Value(std::move(node)));
    });

    ser::Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));
    return Value(std::move(root));
}

// JSON-string convenience over to_value (via the #10 JSON backend).
inline std::string save_to_json(World& world, const reflection::ComponentReflection& types)
{
    return mosaic::foundation::serialization::to_json(to_value(world, types));
}

} // namespace mosaic::engine::scene
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[scene]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/scene/scene.hpp tests/unit/scene/scene_save_test.cpp tests/CMakeLists.txt
git commit -m "Add Scene save: World -> scene Value / JSON (#19)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Scene load (`from_value` + `load_from_json` + remap)

**Files:**
- Modify: `engine/scene/scene.hpp`
- Test: `tests/unit/scene/scene_load_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `to_value` (Task 3), `ComponentReflection::has`/`construct` (#18 + Task 2), `serialization::from_json`, `ReferenceResolver`.
- Produces (namespace `mosaic::engine::scene`):
  - `void from_value(const Value& scene, World& world, const reflection::ComponentReflection& types);`
  - `void load_from_json(const std::string& json, World& world, const reflection::ComponentReflection& types);`
  - Internal `class SceneReferenceResolver : public serialization::ReferenceResolver`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/scene/scene_load_test.cpp`:

```cpp
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::foundation::serialization::Array;
using mosaic::foundation::serialization::SerializationError;
using mosaic::foundation::serialization::Table;
using mosaic::foundation::serialization::Value;
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

struct Link
{
    EntityID target;
};
template <class V> void reflect(V& visitor, Link& l)
{
    visitor.member("target", l.target);
}

ComponentReflection make_types(TypeRegistry& registry)
{
    ComponentReflection types;
    types.register_component<Position>(registry, "Position");
    types.register_component<Link>(registry, "Link");
    return types;
}
} // namespace

TEST_CASE("round-trip preserves the entity/component graph, including empties", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    World src;
    const EntityID a = src.create_entity();
    src.add_component<Position>(a, Position{1.0f, 2.0f});
    src.create_entity(); // component-less
    const EntityID c = src.create_entity();
    src.add_component<Position>(c, Position{3.0f, 4.0f});
    src.add_component<Link>(c, Link{a});

    const Value doc = mosaic::engine::scene::to_value(src, types);

    World dst;
    mosaic::engine::scene::from_value(doc, dst, types);

    REQUIRE(dst.entity_count() == 3);
    int positions = 0;
    dst.for_each<Position>([&](EntityID, Position&) { ++positions; });
    REQUIRE(positions == 2);
    int links = 0;
    dst.for_each<Link>([&](EntityID, Link&) { ++links; });
    REQUIRE(links == 1);
}

TEST_CASE("intra-scene EntityID references remap to the loaded handles", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    // Bump a's generation so its handle differs from a fresh sequential alloc,
    // making the remap non-trivial.
    World src;
    const EntityID scratch = src.create_entity();
    src.destroy_entity(scratch);
    src.tick(0.0f); // slot recycled next
    const EntityID a = src.create_entity(); // slot 0, generation 2
    src.add_component<Position>(a, Position{7.0f, 0.0f});
    const EntityID c = src.create_entity();
    src.add_component<Link>(c, Link{a});
    REQUIRE(a.generation() == 2u);

    const Value doc = mosaic::engine::scene::to_value(src, types);

    World dst;
    mosaic::engine::scene::from_value(doc, dst, types);

    EntityID loaded_target{};
    dst.for_each<Link>([&](EntityID, Link& l) { loaded_target = l.target; });

    REQUIRE(dst.is_valid(loaded_target));   // remapped to a live handle
    REQUIRE(loaded_target != a);            // not the stale source handle
    const Position* p = dst.get_component<Position>(loaded_target);
    REQUIRE(p != nullptr);                  // points at the entity that was `a`
    REQUIRE(p->x == 7.0f);
}

TEST_CASE("a reference to an entity outside the scene resolves to null", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    // Hand-build a scene: one entity "1" with Link.target = "999" (absent).
    Table link;
    link["target"] = Value(std::string("999"));
    Table comps;
    comps["Link"] = Value(std::move(link));
    Table node;
    node["id"] = Value(std::string("1"));
    node["components"] = Value(std::move(comps));
    Array entities;
    entities.push_back(Value(std::move(node)));
    Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));

    World dst;
    mosaic::engine::scene::from_value(Value(std::move(root)), dst, types);

    EntityID loaded_target{EntityID{1u, 1u}}; // sentinel non-null
    dst.for_each<Link>([&](EntityID, Link& l) { loaded_target = l.target; });
    REQUIRE(loaded_target == EntityID(0));
}

TEST_CASE("an unknown component type name aborts the load", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    Table ghost;
    ghost["x"] = Value(1.0);
    Table comps;
    comps["Ghost"] = Value(std::move(ghost));
    Table node;
    node["id"] = Value(std::string("1"));
    node["components"] = Value(std::move(comps));
    Array entities;
    entities.push_back(Value(std::move(node)));
    Table root;
    root["version"] = Value(std::int64_t{1});
    root["entities"] = Value(std::move(entities));

    World dst;
    REQUIRE_THROWS_AS(mosaic::engine::scene::from_value(Value(std::move(root)), dst, types),
                      SerializationError);
}

TEST_CASE("malformed JSON aborts the load", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);
    World dst;
    REQUIRE_THROWS_AS(mosaic::engine::scene::load_from_json("{ not json", dst, types),
                      SerializationError);
}

TEST_CASE("save_to_json / load_from_json round-trips the graph", "[scene]")
{
    TypeRegistry registry;
    ComponentReflection types = make_types(registry);

    World src;
    const EntityID a = src.create_entity();
    src.add_component<Position>(a, Position{1.0f, 2.0f});
    const EntityID c = src.create_entity();
    src.add_component<Link>(c, Link{a});

    const std::string json = mosaic::engine::scene::save_to_json(src, types);

    World dst;
    mosaic::engine::scene::load_from_json(json, dst, types);
    REQUIRE(dst.entity_count() == 2);
    int links = 0;
    dst.for_each<Link>([&](EntityID, Link&) { ++links; });
    REQUIRE(links == 1);
}
```

Register in `tests/CMakeLists.txt`, after the Task 3 line:

```cmake
  unit/scene/scene_load_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'from_value' is not a member of 'mosaic::engine::scene'`.

- [ ] **Step 3: Write minimal implementation**

In `engine/scene/scene.hpp`:

(a) Add these includes (after the existing `#include <unordered_map>`):

```cpp
#include <stdexcept>

#include "foundation/serialization/reference.hpp"
```

(b) Add the resolver and the load functions inside the `mosaic::engine::scene` namespace, after `save_to_json`:

```cpp
// Remaps serialized intra-scene EntityID references to the handles created on
// load. A reference to an entity not in this scene resolves to null EntityID(0).
class SceneReferenceResolver final : public mosaic::foundation::serialization::ReferenceResolver
{
public:
    explicit SceneReferenceResolver(
        const std::unordered_map<std::uint64_t, World::EntityID>& remap)
        : remap_(remap)
    {
    }

    mosaic::foundation::EntityID resolve_entity(
        mosaic::foundation::EntityID serialized) const override
    {
        auto it = remap_.find(serialized.value());
        return it == remap_.end() ? mosaic::foundation::EntityID(0) : it->second;
    }

private:
    const std::unordered_map<std::uint64_t, World::EntityID>& remap_;
};

// Rebuild entities + components into `world` from a scene Value. `world` is
// expected empty for a faithful round-trip. Throws SerializationError on a
// malformed document or an unknown component type name.
inline void from_value(const Value& scene, World& world,
                       const reflection::ComponentReflection& types)
{
    namespace ser = mosaic::foundation::serialization;

    if (!scene.is<ser::Table>())
    {
        throw ser::SerializationError("Scene: root must be a table");
    }
    const ser::Table& root = scene.as<ser::Table>();

    const Value* version = root.find("version");
    if (version == nullptr || !version->is<std::int64_t>() || version->as<std::int64_t>() != 1)
    {
        throw ser::SerializationError("Scene: missing or unsupported version");
    }

    const Value* entities = root.find("entities");
    if (entities == nullptr || !entities->is<ser::Array>())
    {
        throw ser::SerializationError("Scene: 'entities' must be an array");
    }
    const ser::Array& entity_nodes = entities->as<ser::Array>();

    // Pass 1: create entities, build the serialized-id -> new-handle remap.
    std::unordered_map<std::uint64_t, World::EntityID> remap;
    remap.reserve(entity_nodes.size());
    for (const Value& node_value : entity_nodes)
    {
        if (!node_value.is<ser::Table>())
        {
            throw ser::SerializationError("Scene: entity node must be a table");
        }
        const ser::Table& node = node_value.as<ser::Table>();
        const Value* id = node.find("id");
        if (id == nullptr || !id->is<std::string>())
        {
            throw ser::SerializationError("Scene: entity 'id' must be a string");
        }
        std::uint64_t raw = 0;
        try
        {
            raw = std::stoull(id->as<std::string>());
        }
        catch (const std::exception& e)
        {
            throw ser::SerializationError(std::string("Scene: invalid entity id: ") + e.what());
        }
        remap[raw] = world.create_entity();
    }

    const SceneReferenceResolver resolver(remap);

    // Pass 2: construct each entity's components, remapping references.
    for (const Value& node_value : entity_nodes)
    {
        const ser::Table& node = node_value.as<ser::Table>();
        const std::uint64_t raw = std::stoull(node.find("id")->as<std::string>());
        const World::EntityID entity = remap[raw];

        const Value* components = node.find("components");
        if (components == nullptr)
        {
            continue;
        }
        if (!components->is<ser::Table>())
        {
            throw ser::SerializationError("Scene: 'components' must be a table");
        }
        for (const auto& [type_name, field_value] : components->as<ser::Table>())
        {
            if (!types.has(type_name))
            {
                throw ser::SerializationError("Scene: unknown component type '" + type_name + "'");
            }
            types.construct(world, entity, type_name, field_value, resolver);
        }
    }
}

// JSON-string convenience over from_value (via the #10 JSON backend).
inline void load_from_json(const std::string& json, World& world,
                           const reflection::ComponentReflection& types)
{
    from_value(mosaic::foundation::serialization::from_json(json), world, types);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[scene]"`
Expected: PASS (Task 3 + Task 4 `[scene]` cases). Then the full suite:
Run: `ctest --preset debug`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/scene/scene.hpp tests/unit/scene/scene_load_test.cpp tests/CMakeLists.txt
git commit -m "Add Scene load: scene Value / JSON -> World with reference remap (#19)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`:
- Add a "built and merged" paragraph for Scene load/save (#19) under *Where we are*, mirroring the existing entries: note `engine/scene/scene.hpp` (`to_value`/`from_value`/`save_to_json`/`load_from_json`), the new `EntityRegistry::for_each`/`World::for_each_entity` enumeration, and the `ComponentReflection::save`/`has` save direction. Note the two-pass load with `EntityID` remap via `SceneReferenceResolver`, and that filesystem I/O and asset *resolution* remain out of scope (asset registry #20).
- In *What to build next*, drop Scene from the list and make **Asset registry (#20)** the suggested next target (it re-resolves the `AssetID`/`Handle` ids scenes now round-trip). Keep transform system (#21) and runtime loop (#22).
- Update the *Suggested sequence* diagram to mark `#19 Scene load/save` done (✓).
- Bump **Last updated** to note Scene #19 built.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: Scene load/save built (#19)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- Serialize World → Value/JSON (entities + components) → Task 3 (`to_value`, `save_to_json`).
- Load JSON/Value → World with generic construction → Task 4 (`from_value`, `load_from_json`).
- Preserve component-less entities → Task 1 (enumeration) + Task 3 (empty components table) + Task 4 round-trip test.
- Intra-scene EntityID remap → Task 4 (`SceneReferenceResolver`, remap test).
- Unknown type → fail loudly → Task 4 (`has` check + throw, test).
- Broken reference → null → Task 4 (resolver default, test).
- Malformed JSON → SerializationError → Task 4 (test over `from_json`).
- Save-direction reflection (symmetric) → Task 2 (`save`/`has`, save thunk).
- Deterministic per-entity component order → Task 2 (`savers_` as `std::map`).
- Out-of-scope (filesystem, systems, asset resolution) → not built; recorded in spec + Task 5 handoff.
- Unit tests under `tests/unit/scene/` → Tasks 3, 4 (plus enumeration Task 1, reflection Task 2).

**Placeholder scan:** No TBD/TODO in production or test steps. Task 5 is prose documentation. No "handle edge cases" hand-waving — every error path has explicit code.

**Type consistency:** `EntityRegistry::for_each`/`World::for_each_entity` (Task 1) match their uses in Task 2 (`save` iterates `world.for_each<T>`) and Task 3 (`to_value` calls `for_each_entity`). `SaveSink` / `save` / `has` (Task 2) match Task 3's `types.save(...)` and Task 4's `types.has(...)`. `to_value`/`save_to_json` (Task 3) and `from_value`/`load_from_json`/`SceneReferenceResolver` (Task 4) match the test call sites. `Value`/`Table`/`Array`/`SerializationError` and `is<>`/`as<>`/`find`/`operator[]`/`push_back` match the existing `foundation/serialization/value.hpp` API. `EntityID{index, generation}`, `EntityID(raw)`, `.value()`, `.generation()` match `foundation/core/entity_id.hpp`.
</content>
