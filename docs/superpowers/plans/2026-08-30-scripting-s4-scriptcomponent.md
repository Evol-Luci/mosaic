# Scripting S4 — ScriptComponent + ScriptSystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A reflected `ScriptComponent` (Python source defining `on_update(dt)`) + a `ScriptSystem` that runs each scripted entity's `on_update(dt)` per `World::tick`, deterministically — "attach a Python script, press Play."

**Architecture:** `ScriptComponent { std::string source; }` is an ordinary reflected component (Python-free). `ScriptSystem` is an `ecs::System` that drives the NEUTRAL `ScriptEngine` via an injected factory (`std::function<unique_ptr<ScriptEngine>()>`), caching one engine per scripted entity (bind_world + compile source once; run `on_update(dt)` each tick). Per-tick mutations use S2's direct path (no CommandStack — no undo spam). All pybind11 stays in the backend.

**Tech Stack:** C++20, CMake, Catch2, pybind11 (from S1, via the injected factory). Reuses S2 (`bind_world`/`set_global`/`get_component`/`set_component`), S3 (entity surface), #18 reflection, ECS `System`/`World::tick`.

**Spec:** `docs/superpowers/specs/2026-08-30-scripting-s4-scriptcomponent-design.md`

## Global Constraints

- **Tier wall:** `engine/script` (`ScriptComponent`, `ScriptSystem`) stays Python-free — neutral `ScriptEngine` + a `std::function` factory only; NO pybind11. `builtin_components.hpp` gains a Python-free include.
- **Reflection-derived (#18):** `ScriptComponent` is an ordinary reflected component (`reflect(V&, ScriptComponent&)` with `visitor.member("source", c.source)`); scripts mutate through the generic S2/S3 bridge.
- **Determinism:** single-threaded on the tick thread; pure-arithmetic `on_update` is bit-reproducible. EntityID crosses as `uint64`. `Value(double)` for dt, `Value(uint64)` for entity.
- **Never throw into the tick loop:** a per-tick script error (`run_string` returns `ok=false`) is ignored — that entity's mutation is skipped; the tick and other entities proceed.
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::script`, `mosaic::engine::reflection`, `mosaic::foundation::serialization`, `mosaic::foundation`, `mosaic::script::python`.
- **API facts (verified):** `World::for_each<T>(fn)` calls `fn(EntityID, T&)`; `World::add_system<S>(args...)` constructs `S(args...)` and runs it in `World::tick`; `System::update(World&, float dt)` override; `ScriptEngine` (neutral) has `run_string`/`bind_world(World&,ComponentReflection&)`/`set_global(string,Value)`; `create_python_script_engine()` → `unique_ptr<mosaic::engine::script::ScriptEngine>`; the FieldVisitorAdapter maps `std::string` → `field_readonly` (so a string member reflects). `register_builtin_components` has no total-count test asserting the number of types.

---

### Task 1: `ScriptComponent` (reflected) + registration

**Files:**
- Create: `engine/script/script_component.hpp`
- Modify: `engine/reflection/builtin_components.hpp`
- Modify: `tests/unit/reflection/component_value_test.cpp`

**Interfaces:**
- Produces: `struct mosaic::engine::script::ScriptComponent { std::string source; }` + its `reflect`, registered as `"ScriptComponent"` in `register_builtin_components`.

- [ ] **Step 1: Write the failing reflection round-trip test** — add to `tests/unit/reflection/component_value_test.cpp` (add `#include "engine/script/script_component.hpp"` near the other includes, then this TEST_CASE after the last one):

```cpp
TEST_CASE("ScriptComponent round-trips its source through reflection", "[reflection]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    World world;
    auto e = world.create_entity();
    mosaic::engine::script::ScriptComponent sc{"def on_update(dt):\n    pass\n"};
    world.add_component<mosaic::engine::script::ScriptComponent>(e, sc);

    auto v = types.component_value(world, e, "ScriptComponent");
    REQUIRE(v.has_value());
    // Round-trip the Value back through set_value and confirm the source survives.
    auto e2 = world.create_entity();
    REQUIRE(types.set_value(world, e2, "ScriptComponent", *v));
    REQUIRE(world.get_component<mosaic::engine::script::ScriptComponent>(e2)->source == sc.source);
}
```

- [ ] **Step 2: Build → confirm it fails** (`ScriptComponent` undefined / "ScriptComponent" not registered):

```bash
cmake --build build/debug -j 2>&1 | tail -5
```

- [ ] **Step 3: Create `engine/script/script_component.hpp`**

```cpp
#pragma once

#include <string>

namespace mosaic::engine::script
{
// A Python behavior attached to an entity. `source` is a script that defines `on_update(dt)`;
// ScriptSystem (engine/script/script_system.hpp) runs it each tick. Reflected, so it serializes
// with the scene and appears in the inspector (a read-only string for v0.1). No Python types here.
struct ScriptComponent
{
    std::string source;
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
}
} // namespace mosaic::engine::script
```

- [ ] **Step 4: Register it in `engine/reflection/builtin_components.hpp`**

Add the include (with the other engine component includes, Python-free):
```cpp
#include "engine/script/script_component.hpp"
```
Add the registration inside `register_builtin_components(...)`, after the last `register_component` line:
```cpp
    types.register_component<mosaic::engine::script::ScriptComponent>(registry, "ScriptComponent");
```

- [ ] **Step 5: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "ScriptComponent round-trips" --output-on-failure
```
Expected: PASS. Also run the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — confirm 0 failures (no existing test asserts a total type count).

- [ ] **Step 6: Commit**

```bash
git add engine/script/script_component.hpp engine/reflection/builtin_components.hpp tests/unit/reflection/component_value_test.cpp
git commit -m "feat(script): reflected ScriptComponent (source) + register in builtins"
```

---

### Task 2: `ScriptSystem` (per-tick `on_update`)

**Files:**
- Create: `engine/script/script_system.hpp`
- Test: `tests/unit/script/script_system_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScriptComponent` (Task 1); the neutral `ScriptEngine` (`bind_world`/`set_global`/`run_string`); `create_python_script_engine()` (test only, for the factory).
- Produces: `class mosaic::engine::script::ScriptSystem : public ecs::System` with ctor `(reflection::ComponentReflection&, EngineFactory)` where `EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>`.

- [ ] **Step 1: Write the failing integration test `tests/unit/script/script_system_test.cpp`**

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::script::ScriptSystem;
using mosaic::engine::transform::Transform;
using mosaic::foundation::math::Vec3;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace
{
const char* const kMover =
    "def on_update(dt):\n"
    "    t = get_component(entity, 'Transform')\n"
    "    t['position']['x'] += dt\n"
    "    set_component(entity, 'Transform', t)\n";

float mover_after_four_quarter_ticks()
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{kMover});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    return world.get_component<Transform>(e)->position.x;
}
} // namespace

TEST_CASE("ScriptSystem runs on_update each tick, deterministically", "[script]")
{
    const float x1 = mover_after_four_quarter_ticks();
    REQUIRE(x1 == 1.0f);                 // 0.25 * 4, binary-exact
    const float x2 = mover_after_four_quarter_ticks();
    REQUIRE(x1 == x2);                   // bit-identical across runs
}

TEST_CASE("ScriptSystem caches the per-entity engine so script state persists", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "count = 0\n"
        "def on_update(dt):\n"
        "    global count\n"
        "    count += 1\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = float(count)\n"
        "    set_component(entity, 'Transform', t)\n"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    world.tick(0.0f); world.tick(0.0f); world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 3.0f); // counter persisted 1,2,3
}

TEST_CASE("editing the ScriptComponent source recompiles the behavior", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = 1.0\n"
        "    set_component(entity, 'Transform', t)\n"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });
    world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);

    world.get_component<ScriptComponent>(e)->source =
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] = 2.0\n"
        "    set_component(entity, 'Transform', t)\n";
    world.tick(0.0f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 2.0f); // recompiled
}

TEST_CASE("a throwing on_update is non-fatal; other scripted entities still update", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto bad = world.create_entity();
    world.add_component<Transform>(bad, Transform{});
    world.add_component<ScriptComponent>(bad, ScriptComponent{
        "def on_update(dt):\n    raise ValueError('boom')\n"});
    auto good = world.create_entity();
    world.add_component<Transform>(good, Transform{});
    world.add_component<ScriptComponent>(good, ScriptComponent{kMover});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.5f); // must not throw
    REQUIRE(world.get_component<Transform>(good)->position.x == 0.5f);
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/script/script_commands_test.cpp`:
```cmake
  unit/script/script_system_test.cpp
```
Build → FAIL (`script_system.hpp` missing).

- [ ] **Step 3: Create `engine/script/script_system.hpp`**

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "engine/ecs/system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
// Runs each scripted entity's `on_update(dt)` per World::tick. Drives the NEUTRAL ScriptEngine via
// an injected factory (the CPython backend is supplied by the caller), so the engine tier stays
// Python-free. Caches one engine per scripted entity: the engine (with its own namespace) is created
// and the ScriptComponent's source compiled once, then `on_update(dt)` runs each tick. The cache is
// runtime-derived (rebuilt on demand); the authoritative source lives in the component.
class ScriptSystem : public ecs::System
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;

    ScriptSystem(reflection::ComponentReflection& types, EngineFactory factory)
        : types_(types), factory_(std::move(factory))
    {
    }

    void update(World& world, float dt) override
    {
        std::unordered_set<std::uint64_t> seen;
        world.for_each<ScriptComponent>(
            [&](mosaic::foundation::EntityID e, ScriptComponent& sc)
            {
                seen.insert(e.value());
                Entry& entry = engines_[e.value()];
                if (!entry.engine || entry.source != sc.source)
                {
                    entry.engine = factory_();
                    entry.source = sc.source;
                    entry.engine->bind_world(world, types_);
                    entry.engine->set_global(
                        "entity",
                        mosaic::foundation::serialization::Value(
                            static_cast<std::uint64_t>(e.value())));
                    entry.engine->run_string(sc.source); // define on_update + top-level setup
                }
                entry.engine->set_global(
                    "dt", mosaic::foundation::serialization::Value(static_cast<double>(dt)));
                entry.engine->run_string("on_update(dt)"); // non-fatal on error (result ignored)
            });

        // Prune engines for entities no longer scripted/alive (not visited this tick).
        for (auto it = engines_.begin(); it != engines_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : engines_.erase(it);
        }
    }

private:
    struct Entry
    {
        std::unique_ptr<ScriptEngine> engine;
        std::string source;
    };

    reflection::ComponentReflection& types_;
    EngineFactory factory_;
    std::unordered_map<std::uint64_t, Entry> engines_;
};
} // namespace mosaic::engine::script
```

- [ ] **Step 4: Build + run the new tests, then the full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "ScriptSystem runs|caches the per-entity|recompiles the behavior|throwing on_update" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the four `[script]` cases PASS; full suite green.

- [ ] **Step 5: Commit**

```bash
git add engine/script/script_system.hpp tests/unit/script/script_system_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): ScriptSystem runs ScriptComponent on_update per tick"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, note S4 complete + point to S5 (Studio script console — the visible cap). Commit on this branch (rides in the S4 PR). Update the running test count.
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `claude/scripting-s3-world` (this branch is STACKED on S3, which is stacked on S2 #78). Note the stack in the PR body.
- **Lifetime:** in each test, declare `types` before `world` so `types` outlives the `World` (the system holds `ComponentReflection&`; per-entity engines hold non-owning `World*`/`ComponentReflection*` from `bind_world`). The plan's tests already order them correctly.
- The GIL/single-thread assumption from S1–S3 holds (ScriptSystem runs on the tick/main thread).
