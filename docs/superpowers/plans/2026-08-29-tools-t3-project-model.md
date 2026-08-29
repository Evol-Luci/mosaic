# Tools T3 — `mosaic create` + project model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A project = a TOML manifest + `scenes/` + `assets/`; `mosaic create <name>` scaffolds one; `mosaic editor/run <project>` open its default scene by resolving the project down to a scene path that feeds T2's `populate_world`.

**Architecture:** New `app/project/` module: a reflected `ProjectManifest` (TOML), `create_project` (scaffolder, reusing T2's default-scene serialization), and `resolve_startup_scene` (the "smart positional" — project dir → default scene, `.json` → itself). A `create` subcommand + an optional positional `path` on `run`/`editor`; `main()` dispatches `create` and resolves the positional into `options.scene` before dispatch, so `editor.cpp` needs no change.

**Tech Stack:** C++20 (`<filesystem>`), CMake, Catch2, CLI11, the #10 TOML backend. No new third-party deps.

**Spec:** `docs/superpowers/specs/2026-08-29-tools-t3-project-model-design.md`

## Global Constraints

- **Tier:** all project/CLI code lives in `app/`; `engine/`/`foundation/` unchanged.
- **Serialization is #10:** manifest = reflected struct via `to_toml(to_value(m))` / `from_value(from_toml(text), m)` (the exact pattern in `tests/unit/assets/asset_metadata_test.cpp`). Scenes stay #19 JSON.
- **Reuse T2:** `create_project` uses `build_default_scene` + `save_to_json` + `save_scene_file`; `resolve_startup_scene` feeds T2's `populate_world`. No duplicated load/save logic.
- **Never clobber:** `create_project` refuses if the target dir exists.
- **Build:** `cmake --preset debug` (once) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Exe: `build/debug/app/mosaic`.
- New test sources go in the single `tests/CMakeLists.txt` `mosaic_unit_tests` list; new compiled app sources go in `app/CMakeLists.txt`. Tests already link `mosaic_app`/`mosaic_serialization`.
- Namespaces: `mosaic::app`; TOML/reflect API is `mosaic::foundation::serialization::{to_value,from_value,to_toml,from_toml}`. Catch2 names must not start with `--`.

---

### Task 1: `ProjectManifest` + TOML round-trip

**Files:**
- Create: `app/project/project.hpp`
- Test: `tests/unit/app/project_manifest_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `struct mosaic::app::ProjectManifest { std::string name; std::string default_scene; }` with a `reflect()`; the constants `kManifestName`, `kScenesDir`, `kAssetsDir`, `kStarterScene`.

- [ ] **Step 1: Write the failing test `tests/unit/app/project_manifest_test.cpp`**

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/project/project.hpp"
#include "foundation/serialization/reflect_value.hpp"
#include "foundation/serialization/toml_backend.hpp"

using mosaic::app::ProjectManifest;
using mosaic::foundation::serialization::from_toml;
using mosaic::foundation::serialization::from_value;
using mosaic::foundation::serialization::to_toml;
using mosaic::foundation::serialization::to_value;

TEST_CASE("ProjectManifest round-trips through TOML", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";

    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.name == "mygame");
    REQUIRE(back.default_scene == "scenes/main.json");
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/scene_save_roundtrip_test.cpp` (the last `unit/app/` entry):
```cmake
  unit/app/project_manifest_test.cpp
```
`cmake --build build/debug -j 2>&1 | tail -15` → FAIL (`project.hpp` missing).

- [ ] **Step 3: Write `app/project/project.hpp`**

```cpp
#pragma once

#include <string>

namespace mosaic::app
{
// Well-known names inside a project directory.
inline constexpr const char* kManifestName = "mosaic.toml";
inline constexpr const char* kScenesDir    = "scenes";
inline constexpr const char* kAssetsDir    = "assets";
inline constexpr const char* kStarterScene = "scenes/main.json"; // manifest default_scene

// The project manifest (mosaic.toml), reflected so it round-trips through the #10 TOML backend
// exactly like AssetMetadata. Minimal on purpose (version/author would be speculative).
struct ProjectManifest
{
    std::string name;          // project name (defaults to the directory basename)
    std::string default_scene; // project-relative path to the scene opened by default
};

template <class V>
void reflect(V& v, ProjectManifest& m)
{
    v.member("name", m.name);
    v.member("default_scene", m.default_scene);
}
} // namespace mosaic::app
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "ProjectManifest" --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/project/project.hpp tests/unit/app/project_manifest_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): ProjectManifest struct (TOML round-trip)"
```

---

### Task 2: `create_project` + `load_manifest`

**Files:**
- Create: `app/project/project_io.hpp`
- Test: `tests/unit/app/project_create_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProjectManifest` (Task 1); `build_default_scene` + `save_to_json` (T2, `app/run/demo_scene.hpp` / `engine/scene/scene.hpp`); `save_scene_file`/`load_scene_file` (T2, `app/run/scene_io.hpp`); `to_value`/`to_toml`/`from_value`/`from_toml`; `ComponentReflection`.
- Produces:
  - `std::optional<ProjectManifest> load_manifest(const std::string& project_dir);` — parses `<dir>/mosaic.toml`; `nullopt` if missing/unparseable.
  - `bool create_project(const std::string& dir, const mosaic::engine::reflection::ComponentReflection& types);` — scaffolds the project; `false` (reported) if `dir` exists or a write fails.

- [ ] **Step 1: Write the failing test `tests/unit/app/project_create_test.cpp`**

```cpp
#include <cstdio>
#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/project/project.hpp"
#include "app/project/project_io.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::load_manifest;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string temp_project_dir(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t3_") + tag + std::to_string(::getpid()))).string();
}
} // namespace

TEST_CASE("create_project scaffolds a manifest + starter scene + assets dir", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_project_dir("create");
    fs::remove_all(dir); // clean slate

    REQUIRE(create_project(dir, types));
    REQUIRE(fs::exists(fs::path(dir) / "mosaic.toml"));
    REQUIRE(fs::exists(fs::path(dir) / "scenes" / "main.json"));
    REQUIRE(fs::is_directory(fs::path(dir) / "assets"));

    // The manifest parses back with the expected default scene, named after the dir.
    auto m = load_manifest(dir);
    REQUIRE(m.has_value());
    REQUIRE(m->default_scene == "scenes/main.json");
    REQUIRE(!m->name.empty());

    fs::remove_all(dir);
}

TEST_CASE("create_project refuses to overwrite an existing directory", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_project_dir("exists");
    fs::remove_all(dir);
    fs::create_directories(dir); // pre-existing

    REQUIRE_FALSE(create_project(dir, types)); // refuses
    REQUIRE_FALSE(fs::exists(fs::path(dir) / "mosaic.toml")); // nothing written

    fs::remove_all(dir);
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/project_manifest_test.cpp`:
```cmake
  unit/app/project_create_test.cpp
```
Build → FAIL (`project_io.hpp` missing).

- [ ] **Step 3: Write `app/project/project_io.hpp` (this task's functions)**

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"
#include "app/run/demo_scene.hpp"   // build_default_scene
#include "app/run/scene_io.hpp"     // load_scene_file / save_scene_file
#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"   // save_to_json
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value / from_value
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/toml_backend.hpp"  // to_toml / from_toml

namespace mosaic::app
{
// Parse <project_dir>/mosaic.toml into a ProjectManifest. nullopt if the file is missing or
// unparseable (the caller reports).
inline std::optional<ProjectManifest> load_manifest(const std::string& project_dir)
{
    const std::string path = (std::filesystem::path(project_dir) / kManifestName).string();
    const std::optional<std::string> text = load_scene_file(path); // generic text read
    if (!text)
    {
        return std::nullopt;
    }
    try
    {
        ProjectManifest m;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), m);
        return m;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        return std::nullopt;
    }
}

// Scaffold a new project at `dir`: mosaic.toml + scenes/main.json (the default demo scene) + an
// empty assets/ (with a .gitkeep). Returns false (and logs) if `dir` already exists or a write
// fails — never clobbers existing work.
inline bool create_project(const std::string& dir,
                           const mosaic::engine::reflection::ComponentReflection& types)
{
    namespace fs = std::filesystem;
    if (fs::exists(dir))
    {
        mosaic::foundation::log::error("project", "refusing to overwrite existing path: {}", dir);
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(dir) / kScenesDir, ec);
    fs::create_directories(fs::path(dir) / kAssetsDir, ec);
    if (ec)
    {
        mosaic::foundation::log::error("project", "could not create project dirs: {}", dir);
        return false;
    }

    // Project name = the directory's basename (robust against a trailing separator).
    fs::path p(dir);
    std::string name = p.filename().string();
    if (name.empty())
    {
        name = p.parent_path().filename().string();
    }

    // Manifest.
    ProjectManifest manifest;
    manifest.name = name;
    manifest.default_scene = kStarterScene;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((p / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("project", "could not write manifest for {}", dir);
        return false;
    }

    // Starter scene = the default demo (reuses T2).
    mosaic::engine::World world;
    build_default_scene(world);
    const std::string scene_json = mosaic::engine::scene::save_to_json(world, types);
    if (!save_scene_file((p / kStarterScene).string(), scene_json))
    {
        mosaic::foundation::log::error("project", "could not write starter scene for {}", dir);
        return false;
    }

    // Keep the empty assets/ dir under version control.
    save_scene_file((p / kAssetsDir / ".gitkeep").string(), "");

    mosaic::foundation::log::info("project", "created project {}", dir);
    return true;
}
} // namespace mosaic::app
```

Note: `save_to_json(world, types)` matches the T2 signature `save_to_json(World&, const ComponentReflection&)`. `SerializationError` is declared in `foundation/serialization/reference.hpp` (confirmed by `app/run/scene_load.hpp` — copy that include if this path is wrong).

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "create_project" --output-on-failure
```
Expected: both `create_project` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/project/project_io.hpp tests/unit/app/project_create_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): create_project scaffolder + load_manifest"
```

---

### Task 3: `resolve_startup_scene`

**Files:**
- Modify: `app/project/project_io.hpp` (add the resolver)
- Test: `tests/unit/app/project_resolve_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `load_manifest`, `create_project` (Task 2).
- Produces: `std::optional<std::string> resolve_startup_scene(const std::string& path);` — a directory with `mosaic.toml` → `<path>/<default_scene>`; a `.json` path → itself; otherwise `nullopt`.

- [ ] **Step 1: Write the failing test `tests/unit/app/project_resolve_test.cpp`**

```cpp
#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs under parallel ctest)

#include <catch2/catch_test_macros.hpp>

#include "app/project/project_io.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::resolve_startup_scene;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string temp_dir(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t3r_") + tag + std::to_string(::getpid()))).string();
}
} // namespace

TEST_CASE("resolve_startup_scene maps a project dir to its default scene", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_dir("proj");
    fs::remove_all(dir);
    REQUIRE(create_project(dir, types));

    auto scene = resolve_startup_scene(dir);
    REQUIRE(scene.has_value());
    REQUIRE(*scene == (fs::path(dir) / "scenes" / "main.json").string());
    REQUIRE(fs::exists(*scene));

    fs::remove_all(dir);
}

TEST_CASE("resolve_startup_scene returns a .json path unchanged", "[app]")
{
    auto scene = resolve_startup_scene("some/level.json");
    REQUIRE(scene.has_value());
    REQUIRE(*scene == "some/level.json");
}

TEST_CASE("resolve_startup_scene errors on a missing path or a dir without a manifest", "[app]")
{
    REQUIRE_FALSE(resolve_startup_scene("/no/such/mosaic_path_xyz").has_value()); // missing

    const std::string dir = temp_dir("nomani");
    fs::remove_all(dir);
    fs::create_directories(dir); // a dir but no mosaic.toml
    REQUIRE_FALSE(resolve_startup_scene(dir).has_value());
    fs::remove_all(dir);
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/project_create_test.cpp`:
```cmake
  unit/app/project_resolve_test.cpp
```
Build → FAIL (`resolve_startup_scene` undefined).

- [ ] **Step 3: Add `resolve_startup_scene` to `app/project/project_io.hpp`**

Insert before the closing `} // namespace mosaic::app`:

```cpp
// The "smart positional" resolver. Returns the scene file to load, or nullopt on error:
//   - `path` is a directory containing mosaic.toml -> parse it -> "<path>/<default_scene>"
//   - `path` has a .json extension                 -> `path` itself (a bare scene; existence is
//                                                     validated later by populate_world)
//   - otherwise (missing, or a dir without a manifest) -> nullopt (the caller reports).
inline std::optional<std::string> resolve_startup_scene(const std::string& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(path, ec))
    {
        const std::optional<ProjectManifest> m = load_manifest(path);
        if (!m)
        {
            return std::nullopt;
        }
        return (fs::path(path) / m->default_scene).string();
    }
    if (fs::path(path).extension() == ".json")
    {
        return path;
    }
    return std::nullopt;
}
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "resolve_startup_scene" --output-on-failure
```
Expected: all three cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/project/project_io.hpp tests/unit/app/project_resolve_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): resolve_startup_scene (project dir -> default scene, .json -> itself)"
```

---

### Task 4: CLI — `create` subcommand + positional path

**Files:**
- Modify: `app/cli/options.hpp` (Command::Create; `create_path`, `open_path`)
- Modify: `app/cli/options.cpp` (create subcommand; run/editor positional; mutual exclusion)
- Test: extend `tests/unit/app/options_test.cpp`

**Interfaces:**
- Produces: `Command::Create`; `Options::create_path`, `Options::open_path` (both `std::optional<std::string>`).

- [ ] **Step 1: Extend `app/cli/options.hpp`**

Change the enum and add two fields:
```cpp
enum class Command { Run, Editor, Create };
```
In `struct Options`, after `scene`:
```cpp
    std::optional<std::string>   open_path;    // positional: project dir or scene file (run/editor)
    std::optional<std::string>   create_path;  // positional: project dir to create (create)
```

- [ ] **Step 2: Write the failing options tests — extend `tests/unit/app/options_test.cpp`**

Add (uses the file's existing `parse_args({...})` helper):
```cpp
TEST_CASE("create subcommand captures the project name", "[cli]")
{
    auto pr = parse_args({"mosaic", "create", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Create);
    REQUIRE(pr.options.create_path.has_value());
    REQUIRE(*pr.options.create_path == "mygame");
}

TEST_CASE("editor accepts a positional project/scene path", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Editor);
    REQUIRE(pr.options.open_path.has_value());
    REQUIRE(*pr.options.open_path == "mygame");
}

TEST_CASE("run accepts a positional path", "[cli]")
{
    auto pr = parse_args({"mosaic", "run", "level.json"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.open_path.has_value());
    REQUIRE(*pr.options.open_path == "level.json");
}

TEST_CASE("a positional path together with --scene is rejected", "[cli]")
{
    auto pr = parse_args({"mosaic", "editor", "mygame", "--scene", "x.json"});
    REQUIRE(pr.exit_code.has_value());
    REQUIRE(*pr.exit_code != 0);
}
```
Build + run these → FAIL (create subcommand + positionals not implemented).

- [ ] **Step 3: Implement in `app/cli/options.cpp`**

Add the shared locals (near `std::string scene;`):
```cpp
    std::string open_path;
    std::string create_name;
```
Capture the `--scene` options so they can be excluded, and add the run positional. Change the `run->add_option("--scene", ...)` line to capture it, and add after the run options:
```cpp
    CLI::Option* run_scene = run->add_option("--scene", scene, "Scene JSON file to load before running");
    CLI::Option* run_path  = run->add_option("path", open_path, "Project directory or scene file to open");
    run_path->excludes(run_scene);
```
(Delete the original uncaptured `run->add_option("--scene", scene, ...)` line — it is replaced by the `run_scene` capture above.)

For the editor subcommand, change its `--scene` line to capture it and add a positional:
```cpp
    CLI::Option* editor_scene = editor->add_option("--scene", scene, "Scene JSON file to load before editing");
    CLI::Option* editor_path  = editor->add_option("path", open_path, "Project directory or scene file to open");
    editor_path->excludes(editor_scene);
```
(Replace the existing `editor->add_option("--scene", scene, ...)` line.)

Add the `create` subcommand (after the editor subcommand block, before `app.parse`):
```cpp
    CLI::App* create = app.add_subcommand("create", "Scaffold a new project directory");
    create->add_option("name", create_name, "Project directory to create")->required();
```
Update the require-subcommand comment on line ~13 to mention `create`.

After `app.parse(...)` succeeds, add the command + option captures (near the existing `if (editor->parsed())` block):
```cpp
    if (editor->parsed())
    {
        result.options.command = Command::Editor;
    }
    if (create->parsed())
    {
        result.options.command = Command::Create;
    }
    if (!open_path.empty())
    {
        result.options.open_path = open_path;
    }
    if (!create_name.empty())
    {
        result.options.create_path = create_name;
    }
```

- [ ] **Step 4: Build + run the new CLI tests**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "create subcommand|positional" --output-on-failure
```
Expected: all four new cases PASS. Then the full suite:
```bash
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: green (existing `run`/`editor` tests unaffected).

- [ ] **Step 5: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): create subcommand + positional project/scene path on run/editor"
```

---

### Task 5: Entry wiring — dispatch `create`, resolve the positional

**Files:**
- Create: `app/run/create.hpp`, `app/run/create.cpp`
- Modify: `app/CMakeLists.txt` (add `run/create.cpp`)
- Modify: `app/main.cpp` (dispatch `Create`; resolve `open_path` → `options.scene` before dispatch)

**Interfaces:**
- Consumes: `create_project`, `resolve_startup_scene` (Tasks 2-3); `Options` (Task 4).
- Produces: `int mosaic::app::run_create(const Options& options);`

- [ ] **Step 1: Write `app/run/create.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic create <name>`: scaffold a project directory. Returns a process exit code.
int run_create(const Options& options);
} // namespace mosaic::app
```

- [ ] **Step 2: Write `app/run/create.cpp`**

```cpp
#include "app/run/create.hpp"

#include "app/project/project_io.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_create(const Options& options)
{
    if (!options.create_path)
    {
        mosaic::foundation::log::error("project", "create: no project name given");
        return 1;
    }
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    return create_project(*options.create_path, types) ? 0 : 1;
}
} // namespace mosaic::app
```

- [ ] **Step 3: Wire the new source into `app/CMakeLists.txt`**

Add `run/create.cpp` to the `mosaic_app` source list:
```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
)
```

- [ ] **Step 4: Dispatch + resolve in `app/main.cpp`**

Add includes with the other app includes:
```cpp
#include "app/project/project_io.hpp"
#include "app/run/create.hpp"
```
Immediately after the log-sink `add_sink(...)` line and BEFORE the `if (pr.options.command == Command::Editor)` dispatch, insert:
```cpp
    if (pr.options.command == Command::Create)
    {
        return run_create(pr.options);
    }

    // Smart positional: resolve a project dir / scene file into the scene to load, so run and
    // editor both feed T2's populate_world through options.scene.
    if (pr.options.open_path)
    {
        const std::optional<std::string> resolved =
            resolve_startup_scene(*pr.options.open_path);
        if (!resolved)
        {
            mosaic::foundation::log::error(
                "project", "not a Mosaic project or scene file: {}", *pr.options.open_path);
            return 1;
        }
        pr.options.scene = *resolved;
    }
```
(`pr.options` is a mutable local, so setting `pr.options.scene` is seen by both the editor dispatch below and the `const Options& options = pr.options;` binding in the run path. `editor.cpp` needs no change.) Ensure `#include <optional>` and `#include <string>` are present (they are).

- [ ] **Step 5: Build, full suite, functional (the real payoff)**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
PROJ=$(mktemp -d)/mygame
./build/debug/app/mosaic create "$PROJ"; echo "create exit=$?"
ls -R "$PROJ"    # mosaic.toml, scenes/main.json, assets/.gitkeep
timeout 30 ./build/debug/app/mosaic editor "$PROJ" --ticks 3 2>/tmp/t3_ed.err; echo "editor exit=$?"; grep -iE "error|validation|VUID" /tmp/t3_ed.err || echo "(clean)"
timeout 30 ./build/debug/app/mosaic run "$PROJ" --window --ticks 3 2>/tmp/t3_run.err; echo "run exit=$?"; grep -iE "error|validation|VUID" /tmp/t3_run.err || echo "(clean)"
timeout 30 ./build/debug/app/mosaic editor "$PROJ/scenes/main.json" --ticks 3 2>/tmp/t3_bare.err; echo "bare-scene exit=$?"
```
Expected: suite green; `create` exits 0 and scaffolds the three artifacts; `editor <project>`, `run <project> --window`, and `editor <scene.json>` all exit 0 with no validation errors (the scaffolded project opens + renders the starter demo).

- [ ] **Step 6: Commit**

```bash
git add app/run/create.hpp app/run/create.cpp app/CMakeLists.txt app/main.cpp
git commit -m "feat(app): mosaic create dispatch + resolve positional project/scene in main"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 5, mark T3 complete and point to T4 (`mosaic build`). Commit on this branch (rides in the T3 PR — no separate PR).
- **Finishing:** use `superpowers:finishing-a-development-branch` (present the merge/PR menu; base branch is `main`).
- **Visual check to request from the user:** `mosaic create mygame` then `mosaic editor mygame` opens the starter demo; editing + File → Save writes into `mygame/scenes/main.json`; `mosaic editor mygame` reopens it.
- If the `SerializationError` include (Task 2) is wrong, copy the include `app/run/scene_load.hpp` uses for the same catch.
```
