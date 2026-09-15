# Project Templates Mechanism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `mosaic create <path> --template <name>` scaffold a new project from a
named, file-based `templates/<name>/` directory instead of always generating the
same hardcoded demo scene in-process.

**Architecture:** `create_project` changes from "build a `World` via
`build_default_scene`, serialize it" to "copy `templates/<name>/`'s files into the
new project directory, then write a fresh `mosaic.toml`". Template location resolves
via a new compile-time `MOSAIC_TEMPLATES_DIR` define, mirroring the existing
`MOSAIC_FONT_DIR` pattern. `templates/default/` ships as a one-time, checked-in
snapshot of today's exact starter-scene output, so this ships with zero observable
behavior change for anyone not passing `--template`.

**Tech Stack:** C++20, CMake, Catch2, `std::filesystem`, CLI11.

**Spec:** `docs/superpowers/specs/2026-09-14-project-templates-mechanism-design.md`

## Global Constraints

- `build_default_scene` (`app/run/demo_scene.hpp`) stays completely unchanged — it
  has an unrelated second caller (`app/run/scene_load.hpp`'s no-scene-file fallback)
  that this plan does not touch.
- Template location resolves via a compile-time `MOSAIC_TEMPLATES_DIR` define,
  exactly mirroring the existing `MOSAIC_FONT_DIR` pattern in `app/CMakeLists.txt`.
- `create_project`'s existing guarantees are preserved: refuses to overwrite an
  existing `dir`; every project ends up with an `assets/` directory (with
  `.gitkeep` if nothing else populated it); never leaves a half-written project on
  any failure.
- `create_project`'s `types` (`const ComponentReflection&`) parameter is kept, even
  though it becomes unused inside the function — 26 call sites across 7 files
  construct and pass it for reasons unrelated to `create_project` itself; touching
  all of them is out of scope for this plan (see the spec's stated trade-off).
- `create_project` gains a `templates_root` parameter (defaulting to the real
  compile-time location) specifically so tests can point it at a fixture directory
  instead of the real repo's `templates/` folder — the same "inject for
  testability, default to the real thing" shape already used elsewhere in this
  codebase (e.g. `EngineFactory` parameters).

---

### Task 0 (controller-only, before Task 1): generate the `default` template snapshot

This step must run against the **current, unmodified** code (before any change in
this plan lands), because it captures what `mosaic create` produces *today* as the
frozen baseline for the `default` template. Do this yourself before dispatching
Task 1's implementer — it is not something to hand to a subagent, since it requires
building and running the pre-change binary.

```bash
cmake --build build/debug --target mosaic -j"$(nproc)"
rm -rf /tmp/mosaic_template_snapshot
./build/debug/app/mosaic create /tmp/mosaic_template_snapshot
mkdir -p templates/default/scenes
cp /tmp/mosaic_template_snapshot/scenes/main.json templates/default/scenes/main.json
rm -rf /tmp/mosaic_template_snapshot
git add templates/default/scenes/main.json
git commit -m "chore(templates): snapshot the current default starter scene"
```

Verify the snapshot is valid JSON and non-empty before committing (e.g. `cat
templates/default/scenes/main.json` and eyeball it — it should look like a scene
with several entities, matching whatever `build_default_scene` currently produces).

---

### Task 1: `create_project` becomes template-based

**Files:**
- Modify: `app/project/project_io.hpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/unit/app/project_create_test.cpp`

**Interfaces:**
- Consumes: `templates/default/scenes/main.json` (Task 0's committed snapshot),
  `MOSAIC_TEMPLATES_DIR` (new compile-time define, this task).
- Produces:
  ```cpp
  bool create_project(const std::string& dir,
                      const mosaic::engine::reflection::ComponentReflection& types,
                      const std::string& template_name = "default",
                      const std::string& templates_root = default_templates_root());
  std::string default_templates_root(); // returns MOSAIC_TEMPLATES_DIR
  ```
  Task 2's `app/run/create.cpp` calls `create_project(*options.create_path, types,
  options.template_name.value_or("default"))` (relying on `templates_root`'s
  default — it never passes that argument itself).

- [ ] **Step 1: Write the failing tests**

In `tests/unit/app/project_create_test.cpp`, add `#include <fstream>` to the
existing include list (right after `#include <filesystem>`), then append these
test cases to the end of the file:

```cpp
TEST_CASE("create_project fails cleanly for an unknown template name", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string dir = temp_project_dir("badtemplate");
    fs::remove_all(dir);

    REQUIRE_FALSE(create_project(dir, types, "does-not-exist"));
    REQUIRE_FALSE(fs::exists(dir));
}

TEST_CASE("create_project copies a template's assets/ and plugins/ content", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string root = temp_project_dir("tmplroot");
    fs::remove_all(root);
    fs::create_directories(fs::path(root) / "mine" / "scenes");
    fs::create_directories(fs::path(root) / "mine" / "assets");
    fs::create_directories(fs::path(root) / "mine" / "plugins");
    std::ofstream(fs::path(root) / "mine" / "scenes" / "main.json") << "{\"entities\":[]}";
    std::ofstream(fs::path(root) / "mine" / "assets" / "note.txt") << "hello";
    std::ofstream(fs::path(root) / "mine" / "plugins" / "hook.py") << "MOSAIC_MENU_ITEMS = {}\n";

    const std::string dir = temp_project_dir("fromtmpl");
    fs::remove_all(dir);
    REQUIRE(create_project(dir, types, "mine", root));

    REQUIRE(fs::exists(fs::path(dir) / "scenes" / "main.json"));
    REQUIRE(fs::exists(fs::path(dir) / "assets" / "note.txt"));
    REQUIRE(fs::exists(fs::path(dir) / "plugins" / "hook.py"));
    REQUIRE(fs::exists(fs::path(dir) / "mosaic.toml")); // always freshly generated

    fs::remove_all(root); fs::remove_all(dir);
}

TEST_CASE("create_project still writes assets/.gitkeep when a template has no assets/",
         "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    const std::string root = temp_project_dir("tmplroot2");
    fs::remove_all(root);
    fs::create_directories(fs::path(root) / "bare" / "scenes");
    std::ofstream(fs::path(root) / "bare" / "scenes" / "main.json") << "{\"entities\":[]}";

    const std::string dir = temp_project_dir("fromtmpl2");
    fs::remove_all(dir);
    REQUIRE(create_project(dir, types, "bare", root));
    REQUIRE(fs::exists(fs::path(dir) / "assets" / ".gitkeep"));

    fs::remove_all(root); fs::remove_all(dir);
}
```

(The two existing test cases in this file —
`"create_project scaffolds a manifest + starter scene + assets dir"` and
`"create_project refuses to overwrite an existing directory"` — are left
untouched. The first is the regression guard: once Task 0's snapshot exists and
this task's implementation lands, it must still pass unmodified, proving the
`default` template produces the same guarantees as before.)

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `create_project` doesn't yet accept a third/fourth
argument (`"no matching function for call"`).

- [ ] **Step 3: Implement**

In `app/CMakeLists.txt`, find:
```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts")
```
and change it to:
```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts"
  MOSAIC_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates")
```

In `app/project/project_io.hpp`, replace the include list:
```cpp
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
```
with (drops `demo_scene.hpp`, `engine/scene/scene.hpp`, `engine/world/world.hpp` —
no longer used):
```cpp
#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"
#include "app/run/scene_io.hpp"     // load_scene_file / save_scene_file
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value / from_value
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/toml_backend.hpp"  // to_toml / from_toml
```

Then replace the entire `create_project` function body (everything from
`// Scaffold a new project at ...` through its closing `}`) with:

```cpp
// The compile-time templates/ root (see app/CMakeLists.txt's MOSAIC_TEMPLATES_DIR).
// A separate function (not the raw macro) so create_project's default argument
// can reference it, and so tests can point templates_root at a fixture directory
// instead of overriding a macro.
inline std::string default_templates_root() { return MOSAIC_TEMPLATES_DIR; }

// Scaffold a new project at `dir` by copying `templates_root/template_name/`'s
// contents into it (scenes/, and optionally assets/ and/or plugins/), then writing
// a fresh mosaic.toml (name derived from `dir`'s basename). Guarantees an assets/
// directory exists afterward (with .gitkeep if the template left it empty or
// omitted it), matching every project's existing contract. Returns false (and logs)
// if `dir` already exists, `template_name` isn't a subdirectory of `templates_root`
// (the error names every available template), or any copy/write fails — never
// leaves a half-written project. `types` is accepted for signature stability with
// existing call sites but is unused now that templates are static files, not
// in-process-built scenes (see docs/design/project-templates.md).
inline bool create_project(const std::string& dir,
                           const mosaic::engine::reflection::ComponentReflection& types,
                           const std::string& template_name = "default",
                           const std::string& templates_root = default_templates_root())
{
    namespace fs = std::filesystem;
    (void)types;
    if (fs::exists(dir))
    {
        mosaic::foundation::log::error("project", "refusing to overwrite existing path: {}", dir);
        return false;
    }

    const fs::path template_dir = fs::path(templates_root) / template_name;
    std::error_code exists_ec;
    if (!fs::is_directory(template_dir, exists_ec))
    {
        std::string available;
        std::error_code list_ec;
        for (const auto& entry : fs::directory_iterator(templates_root, list_ec))
        {
            if (!entry.is_directory()) { continue; }
            if (!available.empty()) { available += ", "; }
            available += entry.path().filename().string();
        }
        mosaic::foundation::log::error(
            "project", "unknown template '{}' (available: {})", template_name, available);
        return false;
    }

    std::error_code copy_ec;
    fs::copy(template_dir, dir, fs::copy_options::recursive, copy_ec);
    if (copy_ec)
    {
        mosaic::foundation::log::error("project", "could not copy template '{}' into {}: {}",
                                       template_name, dir, copy_ec.message());
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }

    std::error_code mkdir_ec;
    fs::create_directories(fs::path(dir) / kAssetsDir, mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error("project", "could not create assets/ for {}", dir);
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }
    std::error_code empty_ec;
    if (fs::is_empty(fs::path(dir) / kAssetsDir, empty_ec) && !empty_ec)
    {
        if (!save_scene_file((fs::path(dir) / kAssetsDir / ".gitkeep").string(), ""))
        {
            mosaic::foundation::log::error("project", "could not write assets/.gitkeep for {}", dir);
            std::error_code rmec; fs::remove_all(dir, rmec);
            return false;
        }
    }

    // Project name = the directory's basename (robust against a trailing separator).
    fs::path p(dir);
    std::string name = p.filename().string();
    if (name.empty())
    {
        name = p.parent_path().filename().string();
    }

    // Manifest -- always freshly generated, overwriting anything a template might
    // (incorrectly) have included.
    ProjectManifest manifest;
    manifest.name = name;
    manifest.default_scene = kStarterScene;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((p / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("project", "could not write manifest for {}", dir);
        std::error_code rmec; fs::remove_all(dir, rmec);
        return false;
    }

    mosaic::foundation::log::info("project", "created project {} from template '{}'",
                                  dir, template_name);
    return true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --preset debug   # CMakeLists.txt changed, needs a reconfigure
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "create_project*"
```
Expected: all 5 cases pass (2 pre-existing + 3 new).

- [ ] **Step 5: Commit**

```bash
git add app/project/project_io.hpp app/CMakeLists.txt tests/unit/app/project_create_test.cpp
git commit -m "feat(app): make create_project copy a named template instead of building one in-process"
```

---

### Task 2: `--template` CLI flag

**Files:**
- Modify: `app/cli/options.hpp`
- Modify: `app/cli/options.cpp`
- Modify: `app/run/create.cpp`
- Modify: `tests/unit/app/options_test.cpp`

**Interfaces:**
- Consumes: Task 1's `create_project(dir, types, template_name, templates_root)` —
  this task only ever supplies the third argument, never the fourth (the real
  binary always uses the real `default_templates_root()`).
- Produces: `Options::template_name` (`std::optional<std::string>`) — nothing else
  in this plan consumes it beyond `create.cpp`.

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/options_test.cpp`:

```cpp
TEST_CASE("create subcommand accepts an optional --template", "[cli]")
{
    auto pr = parse_args({"mosaic", "create", "mygame", "--template", "adventure"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Create);
    REQUIRE(pr.options.template_name.has_value());
    REQUIRE(*pr.options.template_name == "adventure");
}

TEST_CASE("create subcommand without --template leaves it unset", "[cli]")
{
    auto pr = parse_args({"mosaic", "create", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE_FALSE(pr.options.template_name.has_value());
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `Options` has no member `template_name`.

- [ ] **Step 3: Implement**

In `app/cli/options.hpp`, find:
```cpp
    std::optional<std::string>   create_path;  // positional: project dir to create (create)
```
and add right after it:
```cpp
    std::optional<std::string>   template_name; // --template <name> (create)
```

In `app/cli/options.cpp`, find:
```cpp
    CLI::App* create = app.add_subcommand("create", "Scaffold a new project directory");
    create->add_option("name", create_name, "Project directory to create")->required();
```
and add a `template_name` local + the new option right after it:
```cpp
    CLI::App* create = app.add_subcommand("create", "Scaffold a new project directory");
    create->add_option("name", create_name, "Project directory to create")->required();
    std::string template_name;
    create->add_option("--template", template_name,
                       "Named template to scaffold from (default: \"default\")");
```

Then find:
```cpp
    if (!create_name.empty())
    {
        result.options.create_path = create_name;
    }
```
and add right after it:
```cpp
    if (!template_name.empty())
    {
        result.options.template_name = template_name;
    }
```

In `app/run/create.cpp`, find:
```cpp
    return create_project(*options.create_path, types) ? 0 : 1;
```
and change it to:
```cpp
    return create_project(*options.create_path, types,
                          options.template_name.value_or("default")) ? 0 : 1;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[cli]"
```
Expected: all `[cli]` cases pass, including the 2 new ones, zero regressions.

- [ ] **Step 5: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp app/run/create.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): add --template to the create subcommand"
```

---

### Task 3: Docs + full-suite verification

**Files:**
- Create: `docs/design/project-templates.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: nothing new — documentation of Tasks 0-2's shipped behavior.
- Produces: nothing consumed elsewhere in this plan — final task.

- [ ] **Step 1: Write the design doc**

Create `docs/design/project-templates.md`:

```markdown
# Project templates

`mosaic create <path> [--template <name>]` scaffolds a new project by copying
`templates/<name>/`'s contents into `<path>`, then writing a fresh `mosaic.toml`
(project name derived from `<path>`'s basename, exactly as before). Omitting
`--template` uses `default`.

## Template shape

A template directory has the same shape as a real project's contents, minus the
manifest:

```
templates/
  default/
    scenes/main.json
  <name>/
    scenes/main.json
    assets/        (optional)
    plugins/       (optional)
```

A template's `mosaic.toml` is never checked in -- `create_project` always
generates a fresh one after copying, so a template's own name never leaks into
instantiated projects. If a template has no `assets/` directory (or an empty
one), the new project still gets one with a `.gitkeep`, exactly like the
`default` template does today.

## Where templates are found

`MOSAIC_TEMPLATES_DIR`, a compile-time define set in `app/CMakeLists.txt` from
`${CMAKE_SOURCE_DIR}/templates` -- the same mechanism `MOSAIC_FONT_DIR` already
uses for `assets/fonts`. This sidesteps the CWD-relative-path gap already
documented for `plugins/` resolution (see "Where the plugins directory lives"
in `docs/design/scripting.md`) rather than repeating it: templates are found by
an absolute, baked-in path, not by the process's current working directory.

## The `default` template's origin, and its trade-off

`templates/default/scenes/main.json` is a **one-time, checked-in snapshot** of
what `build_default_scene()` (`app/run/demo_scene.hpp`) produced at the moment
this mechanism was built -- captured by running `mosaic create` once and copying
its output. From that point on, `create_project`'s `default` path no longer
calls `build_default_scene()` at all; it's a static file.

`build_default_scene()` itself is unchanged and still has its own, unrelated
caller: `app/run/scene_load.hpp`'s in-memory fallback when `mosaic run`/`editor`
is invoked with no scene file at all (an ad-hoc demo, no project on disk).

**This means the two can drift.** If `build_default_scene()` is changed later for
that ad-hoc-run purpose, `templates/default/scenes/main.json` will not
automatically follow. A contributor who wants new projects to match a changed
demo scene needs to regenerate the snapshot manually (rerun `mosaic create` on a
scratch directory, copy its `scenes/main.json` over `templates/default/`'s).
This is the same shape of accepted, documented gap as the `plugins/` CWD issue
above -- not a bug, a known and intentional decoupling.

## Adding a new template

Create `templates/<name>/` with at least `scenes/main.json` (any valid scene
JSON, e.g. saved from Studio). Add `assets/` and/or `plugins/` if the template
needs them. No other registration step exists -- `create_project` discovers
templates by directory listing, not a manifest.

## `create_project`'s `types` parameter

`create_project(dir, types, template_name, templates_root)` still takes a
`const ComponentReflection&` even though the function no longer uses it --
templates are static files now, not in-process-built scenes. It's kept, unused,
because 26 existing call sites across 7 files construct and pass it for reasons
unrelated to `create_project` itself; removing the parameter would mean touching
all of them for no behavioral gain. A future cleanup pass could remove it.
```

- [ ] **Step 2: Update next-steps.md**

In `docs/project/next-steps.md`, find the "Last updated" line at the top of the
file and prepend a new clause (following the same pattern used for prior entries,
keeping the ` ¶ (` separator convention) describing this work: `--template`
support, the `MOSAIC_TEMPLATES_DIR` mechanism, the `default` template's
snapshot-and-drift trade-off, the retained-but-unused `types` parameter decision,
and that the richer "3rd-person adventure" template remains explicitly future
work (needs a `Health` component and basic combat/AI scripting that don't exist
yet); link this plan's spec
(`docs/superpowers/specs/2026-09-14-project-templates-mechanism-design.md`).

- [ ] **Step 3: Build and run the full suite**

```bash
cmake --build build/debug --target mosaic mosaic_unit_tests -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
```
Expected: clean build; test failures limited to the pre-existing, unrelated
`--host`/`--connect` CLI Catch2 test-name-leading-dash discovery issue (6 cases)
and, if it reproduces this run, the pre-existing flaky `threaded ScriptSystem`
SIGSEGV -- both documented in project memory as predating this work.

- [ ] **Step 4: Manually verify the CLI end to end**

```bash
rm -rf /tmp/mosaic_template_check_default /tmp/mosaic_template_check_named
./build/debug/app/mosaic create /tmp/mosaic_template_check_default
cat /tmp/mosaic_template_check_default/mosaic.toml
diff templates/default/scenes/main.json /tmp/mosaic_template_check_default/scenes/main.json
./build/debug/app/mosaic create /tmp/mosaic_template_check_named --template default
diff templates/default/scenes/main.json /tmp/mosaic_template_check_named/scenes/main.json
./build/debug/app/mosaic create /tmp/mosaic_template_check_named2 --template does-not-exist
echo "exit code: $?"
rm -rf /tmp/mosaic_template_check_default /tmp/mosaic_template_check_named /tmp/mosaic_template_check_named2
```
Expected: the first `create` (no `--template`) succeeds, `mosaic.toml` shows the
project name derived from the directory, and the `diff` against the checked-in
snapshot shows no difference (proving the default path really is now a pure
copy). The explicit `--template default` invocation produces byte-identical
output to the implicit default. The unknown-template invocation fails with a
non-zero exit code and an error naming "default" as the only available template
— no directory left behind.

- [ ] **Step 5: Commit**

```bash
git add docs/design/project-templates.md docs/project/next-steps.md
git commit -m "docs(app): document the project templates mechanism"
```
