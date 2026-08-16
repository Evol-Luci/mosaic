# Engine `World` Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the runtime `World` — the engine container that owns entities, components, and systems, runs a per-tick update, and drains the deferred event channel.

**Architecture:** Add single-component dense iteration to the ECS (`SparseSet::each`, `ComponentRegistry::for_each<T>`), give `System` a virtual `update(World&, dt)` entry point, then compose a `World` that delegates entity/component ops to the registries, owns an `EventBus`, runs registered systems in order, and defers entity destruction to the end of each tick.

**Tech Stack:** C++20, CMake + CMakePresets, Catch2 (via FetchContent).

**Design spec:** [docs/superpowers/specs/2026-08-16-engine-world-runtime-design.md](../specs/2026-08-16-engine-world-runtime-design.md)

## Global Constraints

- **C++20.** `target_compile_features(... cxx_std_20)`; no compiler extensions.
- **Tier/dependency contract:** `engine/` depends only on `foundation/` and other `engine/` code — never upward. `foundation/` knows nothing above it. (See [ARCHITECTURE.md](../../architecture/ARCHITECTURE.md).)
- **Namespaces:** ECS code is `mosaic::engine::ecs`; the `World` is `mosaic::engine`; foundation event code is `mosaic::foundation::events`.
- **Style (match existing files):** 4-space indent, Allman braces, `snake_case` methods/functions, `PascalCase` types, trailing-underscore private members (`entities_`).
- **Testing:** Catch2, unit tests under `tests/unit/`; register each new `.cpp` in `tests/CMakeLists.txt`. TDD — write the failing test first and watch it fail before implementing.
- **Build/test commands:**
  - Build: `cmake --build --preset debug`
  - Run a subset: `./build/debug/tests/mosaic_unit_tests "[tag]"`
  - Full suite: `ctest --preset debug`
- **Commit** after each task (this branch is `claude/recursing-matsumoto-f36c24`).

## File Structure

| File | Responsibility |
|------|----------------|
| `engine/ecs/sparse_set.hpp` (modify) | Add `each(fn)` dense iteration over `(EntityID, T&)`. |
| `engine/ecs/component_registry.hpp` (modify) | Add `ComponentStorage<T>::each` and `ComponentRegistry::for_each<T>(fn)`. |
| `engine/ecs/system.hpp` (modify) | Add `virtual void update(World&, float dt) {}`; forward-declare `World`. |
| `engine/world/world.hpp` (create) | The `World` class: entity/component/system/event API. |
| `engine/world/world.cpp` (create) | Non-template method bodies: `create_entity`, `destroy_entity`, `tick`. |
| `engine/CMakeLists.txt` (modify) | Add `world/world.cpp` to `mosaic_engine`. |
| `tests/unit/ecs/sparse_set_iteration_test.cpp` (create) | Task 1 test. |
| `tests/unit/ecs/component_registry_iteration_test.cpp` (create) | Task 2 test. |
| `tests/unit/world/world_test.cpp` (create) | Task 3 test. |
| `tests/unit/world/world_tick_test.cpp` (create) | Task 4 test. |
| `tests/unit/world/world_destroy_test.cpp` (create) | Task 5 test. |
| `tests/unit/world/world_integration_test.cpp` (create) | Task 6 test. |
| `tests/CMakeLists.txt` (modify) | Register the six new test files. |

---

### Task 1: `SparseSet::each` dense iteration

**Files:**
- Modify: `engine/ecs/sparse_set.hpp`
- Test: `tests/unit/ecs/sparse_set_iteration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `template<class Fn> void SparseSet<T>::each(Fn&& fn)` — invokes `fn(EntityID, T&)` for each stored pair in dense (insertion) order.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ecs/sparse_set_iteration_test.cpp`:

```cpp
#include <cstdint>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/sparse_set.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::ecs::SparseSet;
using mosaic::foundation::EntityID;

TEST_CASE("SparseSet::each visits every stored pair in dense order", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{1, 1}, 10);
    set.add(EntityID{2, 1}, 20);

    std::vector<std::pair<std::uint32_t, int>> seen;
    set.each([&](EntityID id, int& value) { seen.emplace_back(id.index(), value); });

    REQUIRE(seen.size() == 2);
    REQUIRE(seen[0] == std::make_pair(std::uint32_t{1}, 10));
    REQUIRE(seen[1] == std::make_pair(std::uint32_t{2}, 20));
}

TEST_CASE("SparseSet::each yields mutable references", "[sparse_set]")
{
    SparseSet<int> set;
    set.add(EntityID{5, 1}, 1);
    set.each([](EntityID, int& value) { value = 99; });
    REQUIRE(*set.get(EntityID{5, 1}) == 99);
}
```

Register it in `tests/CMakeLists.txt` — add this line inside the `add_executable(mosaic_unit_tests ...)` source list, after `unit/ecs/system_test.cpp`:

```cmake
  unit/ecs/sparse_set_iteration_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'each' is not a member of 'SparseSet<int>'`.

- [ ] **Step 3: Write minimal implementation**

In `engine/ecs/sparse_set.hpp`, add this public method to `SparseSet` (immediately after the `size()` method, before the `private:` label):

```cpp
    // Visit every stored (entity, component) pair in dense order. Do not add or
    // remove elements of this set from within fn — that can reallocate the dense
    // arrays under the loop.
    template <typename Fn>
    void each(Fn&& fn)
    {
        for (std::size_t i = 0; i < dense_.size(); ++i)
        {
            fn(dense_[i], components_[i]);
        }
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[sparse_set]"`
Expected: PASS (all `[sparse_set]` cases, including the pre-existing ones).

- [ ] **Step 5: Commit**

```bash
git add engine/ecs/sparse_set.hpp tests/unit/ecs/sparse_set_iteration_test.cpp tests/CMakeLists.txt
git commit -m "Add SparseSet::each dense iteration

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `ComponentRegistry::for_each<T>`

**Files:**
- Modify: `engine/ecs/component_registry.hpp`
- Test: `tests/unit/ecs/component_registry_iteration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SparseSet<T>::each` (Task 1).
- Produces:
  - `template<class Fn> void ComponentStorage<T>::each(Fn&& fn)` — forwards to the underlying set.
  - `template<class T, class Fn> void ComponentRegistry::for_each(Fn&& fn)` — visits `(EntityID, T&)` for every entity holding a `T`; a no-op if no `T` storage exists.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/ecs/component_registry_iteration_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/component_registry.hpp"
#include "engine/ecs/entity_registry.hpp"
#include "foundation/core/entity_id.hpp"

using mosaic::engine::ecs::ComponentRegistry;
using mosaic::engine::ecs::EntityRegistry;
using mosaic::foundation::EntityID;

namespace
{
struct Health { int value; };
} // namespace

TEST_CASE("ComponentRegistry::for_each visits only entities holding the component", "[component_registry]")
{
    EntityRegistry entities;
    ComponentRegistry components(entities);

    const EntityID a = entities.create();
    const EntityID b = entities.create();
    const EntityID c = entities.create();

    components.add<Health>(a, Health{10});
    components.add<Health>(c, Health{30});
    // b intentionally has no Health.

    std::vector<int> seen;
    components.for_each<Health>([&](EntityID, Health& h) { seen.push_back(h.value); });

    REQUIRE(seen == std::vector<int>{10, 30});
}

TEST_CASE("ComponentRegistry::for_each on an unused component type is a no-op", "[component_registry]")
{
    EntityRegistry entities;
    ComponentRegistry components(entities);

    int calls = 0;
    components.for_each<Health>([&](EntityID, Health&) { ++calls; });
    REQUIRE(calls == 0);
}
```

Register it in `tests/CMakeLists.txt`, after the line added in Task 1:

```cmake
  unit/ecs/component_registry_iteration_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'for_each' is not a member of 'ComponentRegistry'`.

- [ ] **Step 3: Write minimal implementation**

In `engine/ecs/component_registry.hpp`:

(a) Add an `each` method to `ComponentStorage<T>` (after its `contains` override, before `private:`):

```cpp
    template <typename Fn>
    void each(Fn&& fn) { set_.each(std::forward<Fn>(fn)); }
```

(b) Add `for_each` to `ComponentRegistry` (in the `public:` section, after the `remove` method):

```cpp
    // Visit (EntityID, T&) for every entity holding a T, in dense order. No-op if
    // no T has ever been added. Do not add/remove T within fn (see SparseSet::each).
    template <typename T, typename Fn>
    void for_each(Fn&& fn)
    {
        if (ComponentStorage<T>* s = find_storage<T>())
        {
            s->each(std::forward<Fn>(fn));
        }
    }
```

(`<utility>` is already included in this header for `std::forward`/`std::move`.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[component_registry]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/ecs/component_registry.hpp tests/unit/ecs/component_registry_iteration_test.cpp tests/CMakeLists.txt
git commit -m "Add ComponentRegistry::for_each single-component iteration

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `World` container — entities, components, events

**Files:**
- Create: `engine/world/world.hpp`
- Create: `engine/world/world.cpp`
- Modify: `engine/CMakeLists.txt`
- Test: `tests/unit/world/world_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EntityRegistry`, `ComponentRegistry` (+ its `for_each`), `foundation::events::EventBus`.
- Produces (relied on by Tasks 4–6):
  - `mosaic::engine::World` with `using EntityID = mosaic::foundation::EntityID;`
  - `EntityID create_entity();`
  - `bool is_valid(EntityID) const;`
  - `std::size_t entity_count() const;`
  - `template<class T> bool add_component(EntityID, T);`
  - `template<class T> T* get_component(EntityID);`
  - `template<class T> bool has_component(EntityID) const;`
  - `template<class T> bool remove_component(EntityID);`
  - `template<class T, class Fn> void for_each(Fn&&);`
  - `foundation::events::EventBus& events();`

  (`add_system`, `tick`, `destroy_entity` arrive in later tasks.)

- [ ] **Step 1: Write the failing test**

Create `tests/unit/world/world_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/world/world.hpp"

using mosaic::engine::World;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };
} // namespace

TEST_CASE("World creates valid entities and tracks the live count", "[world]")
{
    World world;
    REQUIRE(world.entity_count() == 0);

    const EntityID e = world.create_entity();
    REQUIRE(world.is_valid(e));
    REQUIRE(world.entity_count() == 1);
}

TEST_CASE("World delegates component add/get/has/remove", "[world]")
{
    World world;
    const EntityID e = world.create_entity();

    REQUIRE(world.add_component<Position>(e, Position{1.0f, 2.0f}));
    REQUIRE(world.has_component<Position>(e));
    REQUIRE(world.get_component<Position>(e) != nullptr);
    REQUIRE(world.get_component<Position>(e)->x == 1.0f);

    REQUIRE(world.remove_component<Position>(e));
    REQUIRE_FALSE(world.has_component<Position>(e));
    REQUIRE(world.get_component<Position>(e) == nullptr);
}

TEST_CASE("World::for_each visits entities holding the component", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 1.0f});
    world.add_component<Position>(b, Position{2.0f, 2.0f});

    int count = 0;
    world.for_each<Position>([&](EntityID, Position&) { ++count; });
    REQUIRE(count == 2);
}
```

Register in `tests/CMakeLists.txt`, after the Task 2 line:

```cmake
  unit/world/world_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `fatal error: engine/world/world.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `engine/world/world.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "engine/ecs/component_registry.hpp"
#include "engine/ecs/entity_registry.hpp"
#include "engine/ecs/system.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/events/event_bus.hpp"

namespace mosaic::engine
{

// The runtime container: owns the entities, their components, the active system
// set, and a per-world event channel. `tick` advances the world one step.
// Scene (de)serialization and the outer runtime loop live outside this class.
class World
{
public:
    using EntityID = mosaic::foundation::EntityID;
    using EventBus = mosaic::foundation::events::EventBus;

    EntityID create_entity();
    bool is_valid(EntityID id) const { return entities_.is_valid(id); }
    std::size_t entity_count() const { return entities_.alive_count(); }

    template <typename T>
    bool add_component(EntityID id, T value) { return components_.add<T>(id, std::move(value)); }

    template <typename T>
    T* get_component(EntityID id) { return components_.get<T>(id); }

    template <typename T>
    bool has_component(EntityID id) const { return components_.has<T>(id); }

    template <typename T>
    bool remove_component(EntityID id) { return components_.remove<T>(id); }

    template <typename T, typename Fn>
    void for_each(Fn&& fn) { components_.for_each<T>(std::forward<Fn>(fn)); }

    EventBus& events() { return events_; }

private:
    ecs::EntityRegistry entities_;
    ecs::ComponentRegistry components_{entities_};
    EventBus events_;
    std::vector<std::unique_ptr<ecs::System>> systems_;
    std::vector<EntityID> pending_destroy_;
};

} // namespace mosaic::engine
```

Create `engine/world/world.cpp`:

```cpp
#include "engine/world/world.hpp"

namespace mosaic::engine
{

World::EntityID World::create_entity()
{
    return entities_.create();
}

} // namespace mosaic::engine
```

Add `world/world.cpp` to `engine/CMakeLists.txt` — change the `add_library` source list so it reads:

```cmake
add_library(mosaic_engine STATIC
  ecs/entity_registry.cpp
  world/world.cpp
)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[world]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/world/world.hpp engine/world/world.cpp engine/CMakeLists.txt tests/unit/world/world_test.cpp tests/CMakeLists.txt
git commit -m "Add World container: entities, components, event channel

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Systems + `tick` (event drain, ordered system update)

**Files:**
- Modify: `engine/ecs/system.hpp`
- Modify: `engine/world/world.hpp`
- Modify: `engine/world/world.cpp`
- Test: `tests/unit/world/world_tick_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `World` (Task 3), `EventBus::drain_deferred`, `EventBus::emit`.
- Produces:
  - `virtual void ecs::System::update(World&, float dt)` — default no-op.
  - `template<class S, class... A> S& World::add_system(A&&... args)` — constructs `S`, stores it, returns a reference. Systems run in registration order.
  - `void World::tick(float dt)` — (1) drains deferred events, (2) runs systems in order.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/world/world_tick_test.cpp`:

```cpp
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::System;

namespace
{
struct Ping { int value; };

// Records its id into a shared vector each update, to observe run order.
class Recorder final : public System
{
public:
    Recorder(std::vector<int>& order, int id) : order_(order), id_(id) {}
    void update(World&, float) override { order_.push_back(id_); }

private:
    std::vector<int>& order_;
    int id_;
};

// Captures the dt it was ticked with.
class DtProbe final : public System
{
public:
    float last_dt = -1.0f;
    void update(World&, float dt) override { last_dt = dt; }
};

// Emits a deferred Ping during its update.
class Emitter final : public System
{
public:
    void update(World& world, float) override { world.events().emit(Ping{9}); }
};
} // namespace

TEST_CASE("systems run in registration order each tick", "[world]")
{
    World world;
    std::vector<int> order;
    world.add_system<Recorder>(order, 1);
    world.add_system<Recorder>(order, 2);
    world.add_system<Recorder>(order, 3);

    world.tick(0.016f);
    REQUIRE(order == std::vector<int>{1, 2, 3});
}

TEST_CASE("tick passes dt through to system update", "[world]")
{
    World world;
    DtProbe& probe = world.add_system<DtProbe>();
    world.tick(0.25f);
    REQUIRE(probe.last_dt == 0.25f);
}

TEST_CASE("tick drains deferred events at the start of the tick", "[world]")
{
    World world;
    std::vector<int> got;
    world.events().subscribe_deferred<Ping>([&](const Ping& p) { got.push_back(p.value); });

    world.events().emit(Ping{5});
    REQUIRE(got.empty());        // not delivered mid-emit

    world.tick(0.0f);
    REQUIRE(got == std::vector<int>{5});
}

TEST_CASE("events emitted during a tick surface on the next tick's drain", "[world]")
{
    World world;
    std::vector<int> got;
    world.events().subscribe_deferred<Ping>([&](const Ping& p) { got.push_back(p.value); });
    world.add_system<Emitter>();

    world.tick(0.0f);            // Emitter emits Ping{9}; drain ran first (empty)
    REQUIRE(got.empty());

    world.tick(0.0f);            // now the queued Ping{9} drains
    REQUIRE(got == std::vector<int>{9});
}
```

Register in `tests/CMakeLists.txt`, after the Task 3 line:

```cmake
  unit/world/world_tick_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'class mosaic::engine::World' has no member named 'add_system'` (and no `update` override match).

- [ ] **Step 3: Write minimal implementation**

(a) In `engine/ecs/system.hpp`, add a forward declaration of `World` and the `update` entry point. Replace the top of the file (the include and namespace open) so it reads:

```cpp
#pragma once

#include <vector>

#include "engine/ecs/component_type_id.hpp"

namespace mosaic::engine { class World; }

namespace mosaic::engine::ecs
{
```

Then, inside `class System`'s `public:` section (after the destructor `virtual ~System() = default;`), add:

```cpp
    // Advance this system one tick. Default is a no-op so declaration-only
    // systems (access sets but no per-tick work) still compile.
    virtual void update(mosaic::engine::World&, float /*dt*/) {}
```

(b) In `engine/world/world.hpp`, add `add_system` and declare `tick`. In the `public:` section, after the `events()` accessor, add:

```cpp
    template <typename S, typename... Args>
    S& add_system(Args&&... args)
    {
        auto system = std::make_unique<S>(std::forward<Args>(args)...);
        S& ref = *system;
        systems_.push_back(std::move(system));
        return ref;
    }

    void tick(float dt);
```

(c) In `engine/world/world.cpp`, add the `tick` definition (inside the namespace, after `create_entity`):

```cpp
void World::tick(float dt)
{
    // 1. Deferred events emitted before this tick are delivered now, at the
    //    tick's start (the fixed sync point).
    events_.drain_deferred();

    // 2. Run systems in registration order (single-threaded for now).
    for (std::unique_ptr<ecs::System>& system : systems_)
    {
        system->update(*this, dt);
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[world]"`
Expected: PASS (all `[world]` cases from Tasks 3 and 4).

- [ ] **Step 5: Commit**

```bash
git add engine/ecs/system.hpp engine/world/world.hpp engine/world/world.cpp tests/unit/world/world_tick_test.cpp tests/CMakeLists.txt
git commit -m "Add World system execution and per-tick event drain

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Deferred entity destruction

**Files:**
- Modify: `engine/world/world.hpp`
- Modify: `engine/world/world.cpp`
- Test: `tests/unit/world/world_destroy_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `World::tick` (Task 4), `ComponentRegistry::on_destroy`, `EntityRegistry::destroy`.
- Produces:
  - `void World::destroy_entity(EntityID)` — enqueues; the entity stays valid until the next `tick` commits it.
  - `tick` gains a third step: commit queued destructions (`components_.on_destroy(id)` then `entities_.destroy(id)` for still-valid ids), then clear the queue.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/world/world_destroy_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/ecs/system.hpp"
#include "engine/world/world.hpp"

using mosaic::engine::World;
using mosaic::engine::ecs::System;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };

// Destroys every entity holding a Position during its update — the classic
// "destroy while iterating" case that deferred destruction must make safe.
class Reaper final : public System
{
public:
    void update(World& world, float) override
    {
        world.for_each<Position>([&](EntityID id, Position&) { world.destroy_entity(id); });
    }
};
} // namespace

TEST_CASE("destroy_entity is deferred until the next tick commits it", "[world]")
{
    World world;
    const EntityID e = world.create_entity();
    world.add_component<Position>(e, Position{0.0f, 0.0f});

    world.destroy_entity(e);
    // Still present until the tick commits.
    REQUIRE(world.is_valid(e));
    REQUIRE(world.has_component<Position>(e));

    world.tick(0.0f);
    REQUIRE_FALSE(world.is_valid(e));
    REQUIRE(world.entity_count() == 0);
}

TEST_CASE("destroying during for_each is safe; commit removes all after systems run", "[world]")
{
    World world;
    const EntityID a = world.create_entity();
    const EntityID b = world.create_entity();
    world.add_component<Position>(a, Position{0.0f, 0.0f});
    world.add_component<Position>(b, Position{0.0f, 0.0f});
    world.add_system<Reaper>();

    world.tick(0.0f);
    REQUIRE(world.entity_count() == 0);
}

TEST_CASE("destroy_entity on an already-destroyed handle is a harmless no-op", "[world]")
{
    World world;
    const EntityID e = world.create_entity();
    world.destroy_entity(e);
    world.tick(0.0f);
    REQUIRE(world.entity_count() == 0);

    world.destroy_entity(e);     // stale handle
    world.tick(0.0f);            // must not throw or corrupt state
    REQUIRE(world.entity_count() == 0);
}
```

Register in `tests/CMakeLists.txt`, after the Task 4 line:

```cmake
  unit/world/world_destroy_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — compile error, `'class mosaic::engine::World' has no member named 'destroy_entity'`.

- [ ] **Step 3: Write minimal implementation**

(a) In `engine/world/world.hpp`, declare `destroy_entity` right after the `create_entity()` declaration:

```cpp
    void destroy_entity(EntityID id);
```

(b) In `engine/world/world.cpp`, add the definition (after `create_entity`):

```cpp
void World::destroy_entity(EntityID id)
{
    // Deferred: committed at the end of the next tick so systems iterating a
    // component set never have entities vanish mid-iteration.
    pending_destroy_.push_back(id);
}
```

(c) In `engine/world/world.cpp`, extend `tick` with the commit step. Append this to the end of the `tick` body (after the systems loop):

```cpp
    // 3. Commit deferred destructions: components first, then the entity slot.
    for (EntityID id : pending_destroy_)
    {
        if (entities_.is_valid(id))
        {
            components_.on_destroy(id);
            entities_.destroy(id);
        }
    }
    pending_destroy_.clear();
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[world]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/world/world.hpp engine/world/world.cpp tests/unit/world/world_destroy_test.cpp tests/CMakeLists.txt
git commit -m "Add deferred entity destruction committed at end of tick

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Interleaved create/add/destroy/recycle integration test

**Files:**
- Test: `tests/unit/world/world_integration_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the full `World` API (Tasks 3–5). No new production code — this is the cross-cutting cycle test the #9 handoff requested.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/world/world_integration_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/world/world.hpp"

using mosaic::engine::World;
using EntityID = mosaic::foundation::EntityID;

namespace
{
struct Position { float x, y; };
struct Velocity { float dx, dy; };
} // namespace

TEST_CASE("interleaved create/add/destroy/recycle across ticks", "[world]")
{
    World world;

    const EntityID a = world.create_entity();
    world.add_component<Position>(a, Position{1.0f, 0.0f});
    const EntityID b = world.create_entity();
    world.add_component<Velocity>(b, Velocity{0.0f, 1.0f});
    REQUIRE(world.entity_count() == 2);

    // Destroy a and commit the destruction.
    world.destroy_entity(a);
    world.tick(0.0f);
    REQUIRE_FALSE(world.is_valid(a));            // stale handle rejects
    REQUIRE(world.entity_count() == 1);
    REQUIRE_FALSE(world.has_component<Position>(a));
    REQUIRE(world.is_valid(b));                  // untouched entity survives
    REQUIRE(world.get_component<Velocity>(b)->dy == 1.0f);

    // Create a new entity — it recycles a's freed slot with a bumped generation.
    const EntityID c = world.create_entity();
    REQUIRE(c.index() == a.index());             // slot recycled
    REQUIRE(c.generation() != a.generation());   // generation advanced
    REQUIRE(world.is_valid(c));
    REQUIRE_FALSE(world.is_valid(a));            // old handle still stale post-recycle

    // c's components are its own; a's old Position did not leak in.
    REQUIRE_FALSE(world.has_component<Position>(c));
    REQUIRE(world.add_component<Position>(c, Position{9.0f, 9.0f}));
    REQUIRE(world.get_component<Position>(c)->x == 9.0f);
    REQUIRE_FALSE(world.has_component<Position>(a));
    REQUIRE(world.entity_count() == 2);
}
```

Register in `tests/CMakeLists.txt`, after the Task 5 line:

```cmake
  unit/world/world_integration_test.cpp
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — link error (test file not yet compiled) resolves once registered; the assertions must then pass. If any assertion fails, it indicates a real sequencing bug in Tasks 3–5 to fix before proceeding.

Note: because this task adds no production code, the "watch it fail" step is the *registration* step — build once before adding the CMake line to confirm the new file is absent, then add it. If instead every assertion passes immediately on first compile, that is expected here (the behavior was built in Tasks 3–5); the value of this task is codifying the cross-cutting cycle as a regression guard.

- [ ] **Step 3: (no production code)**

This task only adds a test. Skip to verification.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[world]" && ctest --preset debug`
Expected: PASS — the `[world]` subset and the full suite are green.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/world/world_integration_test.cpp tests/CMakeLists.txt
git commit -m "Add interleaved World create/add/destroy/recycle integration test

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Update the build handoff

**Files:**
- Modify: `docs/project/next-steps.md`

- [ ] **Step 1: Update next-steps**

In `docs/project/next-steps.md`, record that the Engine `World` runtime is built and point the next worker at the follow-on Engine pieces (asset registry, transform system, the outer runtime loop, and Scene + reflection). Bump **Last updated** to the implementation date. Note the remaining gap the World deliberately left open: reflection-driven generic component construction, which Scene load/save needs.

- [ ] **Step 2: Commit**

```bash
git add docs/project/next-steps.md
git commit -m "Update next-steps: engine World runtime built

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**Spec coverage:**
- World container (entities/components/systems/events) → Tasks 3, 4.
- System `update(World&, dt)` + single-component `for_each` → Tasks 1, 2, 4.
- World owns EventBus, drains each tick → Tasks 3, 4.
- Tick order (drain → systems → commit destroy) → Tasks 4, 5.
- Deferred destruction with `on_destroy → destroy` sequencing → Task 5.
- Interleaved create/add/destroy/recycle test → Task 6.
- Out-of-scope items (Scene, reflection, transform system, runtime loop) → not built; recorded in Task 7 handoff.

**Placeholder scan:** No TBD/TODO in production or test steps; Task 6 Step 3 is an explicit "no production code" note, and Task 7 is prose documentation, not code — both intentional.

**Type consistency:** `create_entity`, `destroy_entity`, `is_valid`, `entity_count`, `add_component`, `get_component`, `has_component`, `remove_component`, `for_each`, `add_system`, `events`, `tick` are named identically across the header definition (Task 3/4/5) and every test that calls them. `System::update(World&, float)` matches the overrides in Tasks 4–5. `ComponentRegistry::for_each<T>` (Task 2) matches the delegation in `World::for_each` (Task 3). `SparseSet::each` (Task 1) matches `ComponentStorage::each` (Task 2).
