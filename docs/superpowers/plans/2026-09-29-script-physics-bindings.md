# Scripting — Physics Bindings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a scene's Python script push a physics body, via `apply_impulse(entity, impulse, point)`.

**Architecture:** A new optional callback on `ScriptHost` (keeping the script backend physics-agnostic, as `raycast` already does), bound into the Python engine's `bind_host`, and wired to the real world by `setup_runtime` through a `make_physics_apply_impulse` helper mirroring `make_physics_raycast`. Separately, the docs generator switches from hardcoded name arrays to iterating the installed globals, which also surfaces four bindings that are already undocumented.

**Tech Stack:** C++20, pybind11 (embedded CPython), Box3D, Catch2, CMake presets.

**Spec:** `docs/superpowers/specs/2026-09-29-script-physics-bindings-design.md`

## Global Constraints

- The binding goes through `ScriptHost`. Do **not** make `modules/script_python` depend on `engine/physics` — `ScriptHost::raycast`'s comment states the rule: *"Kept neutral … so the script backend never depends on the physics system."*
- Vector arguments are **3-element sequences** (`py::sequence`), matching `raycast`. Not dicts.
- An unset host callback is a **silent no-op returning `False`** — `ScriptHost`'s documented contract for every hook.
- A malformed sequence argument (fewer than 3 elements, or a non-number) returns `False`. It must **not** raise, which would kill the tick.
- Do **not** change `raycast`'s existing unguarded indexing. It is recorded as deferred; altering it here would change behaviour outside this slice.
- Entities cross the script boundary as `std::uint64_t`, converted with `EntityID(eid)` / `e.value()`.
- Build: `cmake --build --preset debug -j 20`. Test binary: `./build/debug/tests/mosaic_unit_tests`. Gate: `ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"`.
- **Run only one build at a time** — concurrent `cmake --build` in this tree corrupts archives.
- Never `git add -A`: `docs/project/advancement-plans-and-evaluation.md` is modified and must never be staged. Stage explicit paths.
- **Never `git commit --amend` without first checking `git log -1`** — a commit rejected by the pre-commit hook leaves HEAD on the *previous* commit, and amending then rewrites that one. This has happened twice.
- Catch2 `TEST_CASE` names: no commas, no leading `--`, no unmatched `[`.

## Review Focus

Input classes the spec implies that no task's tests would otherwise exercise. Each gets a test in the task that owns the code.

1. **A script passing a 2-element sequence** — the most likely typo when hand-writing a vector literal. Must return `False`, not raise out of pybind11 and kill the tick. → Task 1.
2. **A script passing a non-number element** (a string, `None`) inside an otherwise well-formed sequence. Same requirement, different failure path — the cast throws rather than the index. → Task 1.
3. **`apply_impulse` on an entity id that is not live** (destroyed, or never created). The C++ `PhysicsSystem::apply_impulse` already no-ops for an unknown entity, but the script path must return `False` rather than report success. → Task 2.
4. **A script calling `apply_impulse` in a world with no `PhysicsSystem`** — live, because scripts also run under the MCP server and asset importers. → Task 2.
5. **A global installed but absent from the reference** — the failure the generator change exists to prevent. The test must be shown to fail when a global goes undocumented, or it is not a guard. → Task 3.

---

### Task 1: The host callback and its Python binding

Adds the neutral hook and binds it. Testable on its own with a stub host — no physics involved yet.

**Files:**
- Modify: `engine/script/script_host.hpp`
- Modify: `modules/script_python/python_script_engine.cpp`
- Test: `tests/unit/script/script_apply_impulse_test.cpp` (create)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `ScriptHost::apply_impulse`, of type `std::function<bool(std::uint64_t, Vec3 impulse, Vec3 point)>`; and the Python global `apply_impulse(entity, impulse, point) -> bool`.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/script/script_apply_impulse_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptHost;
using mosaic::foundation::math::Vec3;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_host exposes apply_impulse and marshals its arguments", "[script]")
{
    std::uint64_t seen_entity = 0;
    Vec3          seen_impulse{}, seen_point{};

    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t e, Vec3 impulse, Vec3 point) -> bool
    {
        seen_entity = e; seen_impulse = impulse; seen_point = point;
        return true;
    };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(7, [2.0, 0.0, -1.0], [0.0, 0.25, 0.0])\n"
        "assert ok is True\n");
    REQUIRE(r.ok);

    REQUIRE(seen_entity == 7);
    REQUIRE(seen_impulse.x == 2.0f);
    REQUIRE(seen_impulse.z == -1.0f);
    REQUIRE(seen_point.y == 0.25f);
}

TEST_CASE("apply_impulse with no host callback returns False", "[script]")
{
    // ScriptHost's contract: an unset hook is a silent no-op.
    ScriptHost host; // apply_impulse deliberately unset
    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
}

TEST_CASE("apply_impulse returns False for a too-short sequence", "[script]")
{
    // Review Focus 1: the likeliest hand-written typo. It must not raise out of
    // pybind11, which would abort the whole tick rather than this one call.
    bool called = false;
    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, Vec3, Vec3) -> bool { called = true; return true; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n"
        "after = 42\n"); // the script keeps running past the failed call
    REQUIRE(r.ok);
    REQUIRE_FALSE(called); // never reached the host
}

TEST_CASE("apply_impulse returns False for a non-numeric element", "[script]")
{
    // Review Focus 2: well-formed length, bad contents. This fails in the cast
    // rather than the index, so it is a genuinely different path from the test above.
    bool called = false;
    ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, Vec3, Vec3) -> bool { called = true; return true; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 'nope', 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
    REQUIRE_FALSE(called);
}

TEST_CASE("apply_impulse propagates a False result from the host", "[script]")
{
    // The host returning false (no body, no physics system) must reach the script
    // as False rather than being swallowed into True.
    ScriptHost host;
    host.apply_impulse = [](std::uint64_t, Vec3, Vec3) -> bool { return false; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    auto r = engine->run_string(
        "ok = apply_impulse(1, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
        "assert ok is False\n");
    REQUIRE(r.ok);
}
```

Register it in `tests/CMakeLists.txt` immediately after the `unit/script/script_raycast_test.cpp` line:

```
  unit/script/script_apply_impulse_test.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `'struct mosaic::engine::script::ScriptHost' has no member named 'apply_impulse'`.

- [ ] **Step 3: Add the host hook**

In `engine/script/script_host.hpp`, inside `struct ScriptHost`, after the `raycast` member:

```cpp
    // Push a dynamic body with an instantaneous impulse applied at a world point;
    // unset => no-op returning false. Kept neutral (entity id + Vec3 in, bool out)
    // for the same reason as raycast: the script backend never depends on the
    // physics system.
    std::function<bool(std::uint64_t entity,
                       mosaic::foundation::math::Vec3 impulse,
                       mosaic::foundation::math::Vec3 point)>
        apply_impulse;
```

- [ ] **Step 4: Bind it in the Python engine**

In `modules/script_python/python_script_engine.cpp`, in `bind_host`, immediately after the `globals_["raycast"] = ...` registration closes, add:

```cpp
        // apply_impulse(entity, impulse, point) -> bool. impulse/point are 3-element
        // sequences (matching raycast). Malformed input returns False rather than
        // raising: a raising binding would abort the whole tick over one bad call.
        globals_["apply_impulse"] = py::cpp_function(
            [this](std::uint64_t entity, py::sequence impulse, py::sequence point) -> bool
            {
                using mosaic::foundation::math::Vec3;
                if (!host_.apply_impulse) { return false; }
                Vec3 i{}, p{};
                if (!to_vec3(impulse, i) || !to_vec3(point, p)) { return false; }
                return host_.apply_impulse(entity, i, p);
            },
            py::doc("Apply an instantaneous impulse to a dynamic body at a world-space "
                    "point; impulse and point are 3-element sequences. Applying it away "
                    "from the body's centre induces spin, which is what makes a sphere "
                    "roll rather than slide. Returns False if there is no physics "
                    "system, the entity has no simulated body, or an argument is "
                    "malformed."));
```

In the same file's anonymous namespace (or just above the class if there is none), add the guarded converter:

```cpp
// Reads a 3-element Python sequence into a Vec3. Returns false instead of throwing
// for a short sequence or a non-numeric element, so one malformed script argument
// fails that call rather than aborting the tick.
bool to_vec3(const py::sequence& s, mosaic::foundation::math::Vec3& out)
{
    if (py::len(s) < 3) { return false; }
    try
    {
        out.x = s[0].cast<float>();
        out.y = s[1].cast<float>();
        out.z = s[2].cast<float>();
    }
    catch (const py::cast_error&)
    {
        return false;
    }
    return true;
}
```

Place `to_vec3` before the class that uses it. If the file has no anonymous namespace, add one containing this function, directly after the includes.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[script]"
```

Expected: PASS, including all pre-existing `[script]` cases.

If the too-short-sequence test fails with a pybind11 error escaping instead of returning False, `to_vec3`'s length check is not running before the indexing — check that `py::len` is called first.

- [ ] **Step 6: Commit**

```bash
git add engine/script/script_host.hpp modules/script_python/python_script_engine.cpp tests/unit/script/script_apply_impulse_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): apply_impulse binding on ScriptHost"
```

---

### Task 2: Wire it to the real physics world

Connects the hook to `PhysicsSystem`, so a real scene script can move a real body.

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Test: `tests/unit/app/script_runtime_test.cpp`

**Interfaces:**
- Consumes: `ScriptHost::apply_impulse` from Task 1.
- Produces: `mosaic::app::make_physics_apply_impulse(World&)` returning `std::function<bool(std::uint64_t, Vec3, Vec3)>`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/app/script_runtime_test.cpp`:

```cpp
TEST_CASE("make_physics_apply_impulse resolves the world's PhysicsSystem", "[app]")
{
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::RigidBody;
    using mosaic::engine::physics::SphereCollider;
    using mosaic::engine::physics::WorldDef;
    using mosaic::foundation::math::Quat;
    using mosaic::foundation::math::Vec3;
    using mosaic::physics::box3d::create_box3d_backend;

    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto ball = world.create_entity();
    world.add_component<Transform>(
        ball, Transform{Vec3{0, 5, 0}, Quat::identity(), Vec3{1, 1, 1}, {}});
    world.add_component<RigidBody>(ball, RigidBody{true, 1.0f});
    world.add_component<SphereCollider>(ball, SphereCollider{0.5f});
    world.tick(1.0f / 60.0f); // create the backend body + handle->entity map

    auto push = make_physics_apply_impulse(world);
    // Applied at the bottom of the ball, off the centre of mass, so it spins too.
    REQUIRE(push(ball.value(), Vec3{2.0f, 0.0f, 0.0f}, Vec3{0.0f, 4.5f, 0.0f}));

    const Quat before = world.get_component<Transform>(ball)->rotation;
    for (int i = 0; i < 30; ++i) { world.tick(1.0f / 60.0f); }
    const auto* after = world.get_component<Transform>(ball);
    REQUIRE(after != nullptr);
    REQUIRE(after->position.x > 0.05f); // it moved along the push

    // Review Focus 6 from the spec: off-centre means it SPINS, not just translates.
    const Quat r = after->rotation;
    const float dq = std::fabs(r.x - before.x) + std::fabs(r.y - before.y)
                   + std::fabs(r.z - before.z) + std::fabs(r.w - before.w);
    REQUIRE(dq > 0.01f);
}

TEST_CASE("make_physics_apply_impulse returns false for an entity with no body", "[app]")
{
    // Review Focus 3: a destroyed or never-simulated entity must report failure
    // rather than silently claiming the push landed.
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::engine::physics::PhysicsSystem;
    using mosaic::engine::physics::WorldDef;
    using mosaic::foundation::math::Vec3;
    using mosaic::physics::box3d::create_box3d_backend;

    World world;
    world.add_system<PhysicsSystem>(create_box3d_backend(WorldDef{}));
    const auto bare = world.create_entity(); // no RigidBody, no collider
    world.tick(1.0f / 60.0f);

    auto push = make_physics_apply_impulse(world);
    REQUIRE_FALSE(push(bare.value(), Vec3{1, 0, 0}, Vec3{0, 0, 0}));
    REQUIRE_FALSE(push(999999u, Vec3{1, 0, 0}, Vec3{0, 0, 0})); // never existed
}

TEST_CASE("make_physics_apply_impulse returns false with no physics system", "[app]")
{
    // Review Focus 4: scripts also run under the MCP server and asset importers,
    // which have no PhysicsSystem at all.
    using mosaic::app::make_physics_apply_impulse;
    using mosaic::foundation::math::Vec3;

    World world; // deliberately no PhysicsSystem
    auto  push = make_physics_apply_impulse(world);
    REQUIRE_FALSE(push(1u, Vec3{1, 0, 0}, Vec3{0, 0, 0}));
}
```

Add `#include <cmath>` to the file's includes if it is not already present.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `'make_physics_apply_impulse' is not a member of 'mosaic::app'`.

- [ ] **Step 3: Write the helper**

In `app/run/demo_scene.hpp`, directly after `make_physics_raycast`'s closing brace:

```cpp
// A neutral impulse callback backed by the world's PhysicsSystem (looked up lazily at
// call time, so system-add order doesn't matter). Injected into the ScriptHost by
// setup_runtime so scripts get apply_impulse() while the script backend stays
// physics-agnostic. Capturing &world is safe: the world owns the systems that own the
// ScriptHost copies. Returns false when there is no PhysicsSystem or the entity has no
// simulated body, matching ScriptHost's "unset hook is a silent no-op" contract.
inline std::function<bool(std::uint64_t, mosaic::foundation::math::Vec3,
                          mosaic::foundation::math::Vec3)>
make_physics_apply_impulse(mosaic::engine::World& world)
{
    return [&world](std::uint64_t entity, mosaic::foundation::math::Vec3 impulse,
                    mosaic::foundation::math::Vec3 point) -> bool
    {
        auto* phys = world.get_system<mosaic::engine::physics::PhysicsSystem>();
        if (phys == nullptr) { return false; }
        const auto e = mosaic::foundation::EntityID(entity);
        if (!world.is_valid(e)) { return false; }
        if (!phys->has_body(e)) { return false; }
        phys->apply_impulse(e, impulse, point);
        return true;
    };
}
```

This needs a way to ask whether a body exists, because `PhysicsSystem::apply_impulse` returns `void` and silently no-ops. Add to `engine/physics/physics_system.hpp`, directly above `apply_impulse`:

```cpp
    // Whether this entity currently has a simulated body. Callers that must report
    // success -- the scripting binding does -- need this, because apply_impulse
    // returns void and no-ops silently for an entity with no body.
    bool has_body(mosaic::foundation::EntityID entity) const
    {
        return bodies_.find(entity) != bodies_.end();
    }
```

- [ ] **Step 4: Wire it into setup_runtime**

In `app/run/demo_scene.hpp`, next to the existing raycast wiring line:

```cpp
    if (!host.raycast) { host.raycast = make_physics_raycast(world); }
```

add immediately below:

```cpp
    if (!host.apply_impulse) { host.apply_impulse = make_physics_apply_impulse(world); }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]"
```

Expected: PASS, including all pre-existing `[app]` cases.

- [ ] **Step 6: Commit**

```bash
git add app/run/demo_scene.hpp engine/physics/physics_system.hpp tests/unit/app/script_runtime_test.cpp
git commit -m "feat(app): wire apply_impulse to the world's PhysicsSystem"
```

---

### Task 3: Document every installed global, not a hardcoded list

Replaces three duplicated name arrays with iteration. Surfaces `apply_impulse` **and** the four bindings already invisible.

**Files:**
- Modify: `app/docs/scripting_globals_reference.cpp`
- Modify: `tests/unit/app/scripting_globals_reference_test.cpp`

**Interfaces:**
- Consumes: the `apply_impulse` global from Task 1.
- Produces: nothing consumed by a later task.

- [ ] **Step 1: Write the failing tests**

In `tests/unit/app/scripting_globals_reference_test.cpp`, **replace** the body of the case `"scripting globals reference documents every world global under its section"` with:

```cpp
    const std::string md = generate_scripting_globals_reference_markdown();
    const std::size_t world_section = md.find("## World & Entity Globals");
    const std::size_t panel_section = md.find("## Studio Panel");
    REQUIRE(world_section != std::string::npos);
    REQUIRE(panel_section != std::string::npos);
    REQUIRE(world_section < panel_section);

    // Every global the generator INSTALLS must be documented. The previous version of
    // this test checked a hardcoded array that duplicated the generator's own array,
    // so a binding added without touching both lists failed nothing -- which is how
    // raycast, play, pause and stop all ended up undocumented.
    for (const char* name : mosaic::app::documented_world_global_names())
    {
        const std::string heading = std::string("### ") + name;
        const std::size_t pos = md.find(heading);
        INFO("global: " << name);
        REQUIRE(pos != std::string::npos);
        REQUIRE(pos > world_section);
        REQUIRE(pos < panel_section);
    }

    // The four host globals that were invisible before this change.
    for (const char* name : {"raycast", "play", "pause", "stop", "apply_impulse"})
    {
        INFO("host global: " << name);
        REQUIRE(md.find(std::string("### ") + name) != std::string::npos);
    }
```

Add `#include <vector>` and `#include <string>` to that test file if absent.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
```

Expected: a **compile** failure — `'documented_world_global_names' is not a member of 'mosaic::app'`.

- [ ] **Step 3: Make the generator iterate**

In `app/docs/scripting_globals_reference.hpp`, add beside the existing declaration:

```cpp
// The non-imgui globals the reference documents, in the order it renders them.
// Exposed so the reference test asserts against what is actually INSTALLED rather
// than against a second copy of a hardcoded list.
std::vector<std::string> documented_world_global_names();
```

Add `#include <string>` and `#include <vector>` to that header.

In `app/docs/scripting_globals_reference.cpp`, add a helper that builds the installed dict once:

```cpp
namespace
{
// Installs every global the reference documents into one dict. Shared by the
// generator and by documented_world_global_names(), so the two cannot disagree.
void install_all_globals(py::dict& globals, mosaic::engine::World** world_ptr,
                         const mosaic::engine::reflection::ComponentReflection** types_ptr,
                         mosaic::foundation::commands::CommandStack** stack_ptr,
                         int* child_depth)
{
    mosaic::script::python::install_world_globals(globals, world_ptr, types_ptr, stack_ptr);
    mosaic::script::python_studio::install_imgui_globals(globals, [] {}, child_depth);
    mosaic::script::python::install_host_globals_for_docs(globals);
}

// Sorted, stable ordering so the generated file does not churn between runs.
std::vector<std::string> world_names_from(const py::dict& globals)
{
    std::vector<std::string> names;
    for (auto item : globals)
    {
        const std::string name = py::str(item.first).cast<std::string>();
        if (name.rfind("imgui_", 0) == 0) { continue; }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace
```

Replace the two hardcoded loops in `generate_scripting_globals_reference_markdown` with:

```cpp
    out << "## World & Entity Globals\n\n";
    for (const std::string& name : world_names_from(globals))
    {
        render_global(out, globals, name.c_str());
    }

    out << "## Studio Panel (`imgui_*`) Globals\n\n";
    out << "Available only inside a `MOSAIC_PANELS` draw function.\n\n";
    std::vector<std::string> panel_names;
    for (auto item : globals)
    {
        const std::string name = py::str(item.first).cast<std::string>();
        if (name.rfind("imgui_", 0) == 0) { panel_names.push_back(name); }
    }
    std::sort(panel_names.begin(), panel_names.end());
    for (const std::string& name : panel_names) { render_global(out, globals, name.c_str()); }
```

and switch its setup to call `install_all_globals(...)` instead of the two install calls. Add `#include <algorithm>`, `#include <string>` and `#include <vector>`.

Then add the accessor:

```cpp
std::vector<std::string> documented_world_global_names()
{
    mosaic::script::python::ensure_interpreter();
    mosaic::engine::World* world_ptr = nullptr;
    const mosaic::engine::reflection::ComponentReflection* types_ptr = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ptr = nullptr;
    int child_depth = 0;
    py::dict globals;
    install_all_globals(globals, &world_ptr, &types_ptr, &stack_ptr, &child_depth);
    return world_names_from(globals);
}
```

- [ ] **Step 4: Expose the host globals to the generator**

The host globals (`raycast`, `play`, `pause`, `stop`, `apply_impulse`) are installed inside the engine's `bind_host`, which needs a live `ScriptHost`. Add a docs-only installer so the generator can render them without one.

Create a new `modules/script_python/host_globals.hpp` for this declaration. Do
**not** put it in `python_script_engine.hpp`: that header is deliberately free of
pybind11, exposing only `std` types and engine interfaces, and a `py::dict`
parameter would drag pybind11 into every consumer. `world_globals.hpp` is the
precedent — it includes `<pybind11/pybind11.h>` and declares its installer with
`py::dict&`. Mirror it:

```cpp
#pragma once

#include <pybind11/pybind11.h>

namespace mosaic::script::python
{
namespace py = pybind11;
```

and declare:

```cpp
// Installs the ScriptHost-backed globals (raycast, play, pause, stop, apply_impulse)
// with no host attached, purely so the docs generator can render their signatures and
// docstrings. Every one is a no-op in this state, which is exactly ScriptHost's
// documented behaviour for an unset hook.
void install_host_globals_for_docs(py::dict& globals);
```

Close the namespace in the new header. Implement it in `python_script_engine.cpp` (which already includes pybind11) by installing the same `py::cpp_function` registrations `bind_host` uses, against a default-constructed `ScriptHost` held in a function-local `static`. Extract the shared registration body into a file-local helper taking a `ScriptHost&`, and call it from both `bind_host` and `install_host_globals_for_docs`, so the two cannot drift.

Ensure every one of those registrations carries a `py::doc(...)`: `raycast`, `play`, `pause` and `stop` currently have none, and `render_global` reads `__doc__` for the body. Give each a one-line description matching the style of the world globals.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[docs]"
```

Expected: PASS.

- [ ] **Step 6: Prove the guard actually guards**

A test that cannot fail is the thing this task exists to prevent, so demonstrate it:

Temporarily add a throwaway global to `install_all_globals`:

```cpp
    globals["zz_dummy_global"] = py::cpp_function([] { return 0; }, py::doc("throwaway"));
```

but do **not** let `render_global` see it — temporarily filter it out of `world_names_from` by adding `if (name == "zz_dummy_global") { continue; }`. Rebuild and run `"[docs]"`.

Expected: **FAIL** on the `documented_world_global_names` loop, because the name is installed but absent from the rendered output.

Then remove both temporary edits, rebuild, and confirm `"[docs]"` passes again.

- [ ] **Step 7: Regenerate the reference and run the full gate**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
cmake --build build --target mosaic -j 20 && ./build/app/mosaic docs
```

This rebuilds the ad-hoc `build/app/mosaic` the pre-commit hook prefers and regenerates `docs/reference/*.md`. The hook uses whichever `mosaic` binary is built and does **not** check it is newer than the source, so skipping this means committing a stale reference that the hook will silently accept.

Confirm the regenerated `docs/reference/scripting-globals.md` now contains `apply_impulse`, `raycast`, `play`, `pause` and `stop`. Expect ordering churn throughout the file — that is the switch from hand-ordered arrays to sorted iteration, not a content change.

```bash
ctest --preset debug --output-on-failure -E "threaded ScriptSystem applies script writes|ScriptWorker runs a job on its thread"
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add app/docs/scripting_globals_reference.cpp app/docs/scripting_globals_reference.hpp modules/script_python/python_script_engine.cpp modules/script_python/python_script_engine.hpp tests/unit/app/scripting_globals_reference_test.cpp docs/reference/scripting-globals.md
git commit -m "fix(docs): document every installed scripting global"
```

---

### Task 4: Demonstrate it in the demo scene

Gives the binding a visible user, and proves the whole path end to end in a real run.

**Files:**
- Modify: `app/run/demo_scene.hpp`
- Test: `tests/unit/app/demo_scene_test.cpp`

**Interfaces:**
- Consumes: the wired `host.apply_impulse` from Task 2.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/demo_scene_test.cpp`:

```cpp
TEST_CASE("build_default_scene includes a script that pushes a body", "[app]")
{
    // The binding needs a visible user, and a scripted push is the one thing that
    // proves the whole path -- script -> ScriptHost -> PhysicsSystem -- in a real run.
    World world;
    build_default_scene(world);

    int pushers = 0;
    world.for_each<mosaic::engine::script::ScriptComponent>(
        [&](World::EntityID, mosaic::engine::script::ScriptComponent& sc)
        {
            if (sc.source.find("apply_impulse") != std::string::npos) { ++pushers; }
        });
    REQUIRE(pushers == 1);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "build_default_scene includes a script that pushes a body"
```

Expected: FAIL with `pushers == 1` where `pushers` is 0.

- [ ] **Step 3: Add the script**

In `app/run/demo_scene.hpp`, beside the existing script sources (near `kOrbiterScript`), add:

```cpp
// Script-driven physics: nudge the sphere every ~3s so the impulse binding has a
// visible user. Applied off-centre (at the ball's underside) so it ROLLS rather than
// slides -- the same distinction the C++ impulse API turns on.
inline constexpr const char* kBallPusherScript = R"py(
_t = 0.0
def on_update(dt):
    global _t
    _t += dt
    if _t >= 3.0:
        _t -= 3.0
        tr = get_component(entity, 'Transform')
        p = tr['position']
        apply_impulse(entity, [1.5, 0.0, 0.0], [p['x'], p['y'] - 0.5, p['z']])
)py";
```

Attach it to the existing `sphere` entity in `build_default_scene`, directly after its `TrailRenderer` is added:

```cpp
    world.add_component<mosaic::engine::script::ScriptComponent>(
        sphere, mosaic::engine::script::ScriptComponent{kBallPusherScript});
```

This adds no entity, so the demo's Transform-count assertions are unaffected. It **does** add a `ScriptComponent`, and the existing case `"build_default_scene includes the scripted (ScriptComponent) demo objects"` asserts an exact count. In `tests/unit/app/demo_scene_test.cpp`, change:

```cpp
    REQUIRE(scripted == 2); // the orbiter cube + the scripted puffer emitter
```

to:

```cpp
    REQUIRE(scripted == 3); // the orbiter cube + the scripted puffer emitter + the ball pusher
```

That same test also requires every script to contain `on_update`, which `kBallPusherScript` does.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build --preset debug --target mosaic_unit_tests -j 20
./build/debug/tests/mosaic_unit_tests "[app]"
```

Expected: PASS, including the scripted-objects case.

- [ ] **Step 5: Verify it in a real run**

```bash
cmake --build --preset debug -j 20
```

Wait for it to exit, then:

```bash
./build/debug/app/mosaic run --window --duration 12
```

Expected: exit 0, **zero Vulkan validation errors**, and the red sphere visibly receiving a shove roughly every 3 seconds — rolling, with its specular highlight staying anchored while the surface turns beneath it. If the ball never moves, the binding is reaching the host but the host is returning false: check `setup_runtime`'s wiring line from Task 2.

- [ ] **Step 6: Commit**

```bash
git add app/run/demo_scene.hpp tests/unit/app/demo_scene_test.cpp
git commit -m "feat(demo): scripted impulse nudges the demo sphere"
```
