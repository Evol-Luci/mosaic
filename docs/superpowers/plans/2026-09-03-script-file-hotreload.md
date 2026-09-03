# External .py-file Script Hot-Reload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `ScriptComponent` can reference an external `.py` file; `ScriptSystem` loads the script from it, polls the file's mtime each tick, and reloads (recompiles) on an on-disk edit — hot-reload from your own editor, no restart.

**Architecture:** `ScriptComponent` gains a reflected `std::string path`. `ScriptSystem` resolves the "effective source" per tick (file contents when `path` is set, reloading when its mtime changes; else the inline `source`) and feeds it into S4's existing recompile-on-change seam. `engine/script` stays Python-free (`std::filesystem`/`std::ifstream` only).

**Tech Stack:** C++20, CMake, Catch2, pybind11 (from S1, via the injected factory). Reuses S4 (`ScriptComponent`/`ScriptSystem`).

**Spec:** `docs/superpowers/specs/2026-09-03-script-file-hotreload-design.md`

## Global Constraints

- **Tier wall:** `engine/script` stays Python-free — `std::filesystem`/`std::ifstream` are standard engine-tier tools; NO pybind11. CPython enters only via the injected factory (unchanged).
- **Precedence:** when `ScriptComponent.path` is non-empty the effective source is the **file contents** (inline `source` ignored); when empty, the inline `source` is used (S4 behavior, unchanged).
- **Reload trigger:** `std::filesystem::last_write_time` polled each tick; a changed mtime (or a new path / first sight) reloads the file → S4 recompile.
- **Never throw:** a missing/unreadable file keeps the last-compiled source (or empty on first sight); a syntax error in the file is non-fatal (S4 swallows the per-tick error).
- **Build:** `cmake --build build/debug -j`; **Test:** `ctest --test-dir build/debug`. Catch2 names not starting with `--`. Namespaces: `mosaic::engine::script`, `mosaic::engine::reflection`, `mosaic::engine::transform`, `mosaic::foundation`, `mosaic::script::python`.
- **API facts (verified):** current `ScriptComponent { std::string source; }` with `reflect` calling `visitor.member("source", c.source)`. `ScriptSystem` (`engine/script/script_system.hpp`) has a per-entity `struct Entry { std::unique_ptr<ScriptEngine> engine; std::string source; }` and an `update()` that recompiles when `!entry.engine || entry.source != sc.source`. `World::for_each<ScriptComponent>(fn: EntityID, ScriptComponent&)`, `add_component`/`get_component`. `create_python_script_engine()` (`modules/script_python/python_script_engine.hpp`, ns `mosaic::script::python`). `mosaic_unit_tests` links `mosaic_script_python`. The existing reflection test is `tests/unit/reflection/component_value_test.cpp::"ScriptComponent round-trips its source through reflection"`. Test-list in `tests/CMakeLists.txt` under `unit/script/`.

---

### Task 1: `ScriptComponent` gains a reflected `path`

**Files:**
- Modify: `engine/script/script_component.hpp`
- Modify: `tests/unit/reflection/component_value_test.cpp` (extend the round-trip test to cover `path`)

**Interfaces:**
- Produces: `ScriptComponent { std::string source; std::string path; }` (both reflected).

- [ ] **Step 1: Extend the failing reflection round-trip test** in `tests/unit/reflection/component_value_test.cpp`. Change the "ScriptComponent round-trips its source through reflection" test to also set + assert `path`. Replace:
```cpp
    mosaic::engine::script::ScriptComponent sc{"def on_update(dt):\n    pass\n"};
```
with:
```cpp
    mosaic::engine::script::ScriptComponent sc{"def on_update(dt):\n    pass\n", "behaviors/mover.py"};
```
and after the existing `REQUIRE(... ->source == sc.source);` line, add:
```cpp
    REQUIRE(world.get_component<mosaic::engine::script::ScriptComponent>(e2)->path == sc.path);
```

- [ ] **Step 2: Build → confirm it fails**
```bash
cmake --build build/debug -j 2>&1 | tail -5
```
Expected: compile error — too many initializers for `ScriptComponent` (no `path` member yet).

- [ ] **Step 3: Add `path` to `engine/script/script_component.hpp`**

Replace the struct + reflect:
```cpp
struct ScriptComponent
{
    std::string source;
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
}
```
with:
```cpp
struct ScriptComponent
{
    std::string source; // inline Python (used when `path` is empty)
    std::string path;   // optional .py file; when set, the script loads from it and hot-reloads
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
    visitor.member("path", c.path);
}
```

- [ ] **Step 4: Build + test**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "ScriptComponent round-trips" --output-on-failure
```
Expected: PASS. Then the full suite once (`ctest --test-dir build/debug 2>&1 | tail -3`) — 0 failures.

- [ ] **Step 5: Commit**
```bash
git add engine/script/script_component.hpp tests/unit/reflection/component_value_test.cpp
git commit -m "feat(script): ScriptComponent gains a reflected .py-file path"
```

---

### Task 2: `ScriptSystem` loads + hot-reloads from the `.py` file

**Files:**
- Modify: `engine/script/script_system.hpp` (`Entry` fields + `update()` resolve/reload)
- Test: `tests/unit/script/script_hotreload_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ScriptComponent.path` (Task 1); S4's recompile seam.
- Produces: `ScriptSystem` loads the source from `path` when set and reloads on an mtime change.

- [ ] **Step 1: Write the failing test `tests/unit/script/script_hotreload_test.cpp`**

```cpp
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h> // ::getpid

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
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace fs = std::filesystem;

namespace
{
std::string mover_script(float x)
{
    return "def on_update(dt):\n"
           "    t = get_component(entity, 'Transform')\n"
           "    t['position']['x'] = " + std::to_string(x) + "\n"
           "    set_component(entity, 'Transform', t)\n";
}
fs::path tmp_py(const char* tag)
{
    return fs::temp_directory_path() /
           ("mosaic_hotreload_" + std::string(tag) + "_" + std::to_string(::getpid()) + ".py");
}
void write_file(const fs::path& p, const std::string& s)
{
    std::ofstream(p, std::ios::binary) << s;
}
} // namespace

TEST_CASE("ScriptComponent loads its on_update from a .py file", "[script]")
{
    const fs::path py = tmp_py("load");
    write_file(py, mover_script(1.0f));

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", py.string()}); // path set, no inline
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
    fs::remove(py);
}

TEST_CASE("editing the .py file hot-reloads the script", "[script]")
{
    const fs::path py = tmp_py("reload");
    write_file(py, mover_script(1.0f));

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", py.string()});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);

    // Edit the file + bump its mtime deterministically (independent of filesystem resolution).
    write_file(py, mover_script(2.0f));
    fs::last_write_time(py, fs::file_time_type::clock::now() + std::chrono::seconds(2));

    world.tick(0.1f);
    REQUIRE(world.get_component<Transform>(e)->position.x == 2.0f); // reloaded
    fs::remove(py);
}

TEST_CASE("a missing .py file is non-fatal", "[script]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{"", "/no/such/mosaic_script_xyz.py"});
    world.add_system<ScriptSystem>(types, [] { return create_python_script_engine(); });

    world.tick(0.1f); // must not throw
    REQUIRE(world.get_component<Transform>(e)->position.x == 0.0f); // unchanged
}
```

- [ ] **Step 2: Register the test + confirm it fails**

Add to `tests/CMakeLists.txt` after `unit/script/script_system_test.cpp`:
```cmake
  unit/script/script_hotreload_test.cpp
```
Build → the tests FAIL (the `path` is ignored — `ScriptSystem` still uses inline `source`, which is empty, so `position.x` stays 0).

- [ ] **Step 3: Add file-load/reload to `engine/script/script_system.hpp`**

Add includes (with the existing ones):
```cpp
#include <filesystem>
#include <fstream>
#include <system_error>
```
Extend `Entry`:
```cpp
    struct Entry
    {
        std::unique_ptr<ScriptEngine>   engine;
        std::string                     source;  // last-compiled source
        std::string                     path;    // last-seen path (detect a path change / switch to inline)
        std::filesystem::file_time_type mtime{}; // last-seen file mtime (detect an on-disk edit)
    };
```
In `update()`, replace the recompile block (the `Entry& entry = engines_[e.value()];` line through the closing `}` of the `if (!entry.engine || entry.source != sc.source)` block) with:
```cpp
                Entry& entry = engines_[e.value()];

                // Resolve the effective source: from the .py file if `path` is set (reloading on an
                // on-disk mtime change), else the inline `source`.
                std::string desired;
                if (sc.path.empty())
                {
                    entry.path.clear();
                    desired = sc.source;
                }
                else
                {
                    std::error_code ec;
                    const auto mtime = std::filesystem::last_write_time(sc.path, ec);
                    const bool reload =
                        !ec && (!entry.engine || entry.path != sc.path || mtime != entry.mtime);
                    if (reload)
                    {
                        std::ifstream f(sc.path, std::ios::binary);
                        if (f)
                        {
                            desired = std::string(std::istreambuf_iterator<char>(f),
                                                  std::istreambuf_iterator<char>());
                            entry.path  = sc.path;
                            entry.mtime = mtime;
                        }
                        else { desired = entry.source; } // open failed -> keep last-good
                    }
                    else { desired = entry.source; }     // no change / missing file -> keep what we have
                }

                if (!entry.engine || entry.source != desired)
                {
                    entry.engine = factory_();
                    entry.source = desired;
                    entry.engine->bind_world(world, types_);
                    entry.engine->set_global(
                        "entity",
                        mosaic::foundation::serialization::Value(
                            static_cast<std::uint64_t>(e.value())));
                    entry.engine->run_string(desired); // (re)compile: define on_update + top-level
                }
```
(The `set_global("dt")` + `run_string("on_update(dt)")` lines after it, and the prune loop, are unchanged.)

- [ ] **Step 4: Build + run the new tests, then the full suite**
```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "loads its on_update from a .py|hot-reloads the script|missing .py file is non-fatal" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the three new `[script]` cases PASS; full suite green.

- [ ] **Step 5: Commit**
```bash
git add engine/script/script_system.hpp tests/unit/script/script_hotreload_test.cpp tests/CMakeLists.txt
git commit -m "feat(script): ScriptSystem loads + hot-reloads a ScriptComponent .py file"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 2, note the external-`.py`-file hot-reload done (a `ScriptComponent.path` loads + reloads on disk change via `ScriptSystem` mtime-polling); the deferred OS file-watcher + `.py`-as-asset + project-relative path resolution remain. Update the running test count. Commit on this branch (rides in the PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- All changes are headless; the "edit the file, watch it live in Studio" is the user's visual acceptance on return. This slice is independent of the in-editor Script Editor (#84) — both edit different sources (inline vs file) and merge cleanly.
