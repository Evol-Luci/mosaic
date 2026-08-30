# Tools T4 — `mosaic build` (standalone bundle) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mosaic build <project> [-o <out>]` packages a project into a self-contained bundle (copied `mosaic` binary + default scene + manifest + `run.sh`) that runs via its own binary through T3's existing run path.

**Architecture:** A `build_bundle(project_dir, out_dir, runtime_binary, types)` packager in `app/project/bundle.hpp` (runtime binary path injected for headless testability); a `run_build` that resolves the running exe via `/proc/self/exe`; a `build` subcommand dispatched in `main()`. The bundle is project-format, so the copied binary runs it through T3's `resolve_startup_scene` — no new run-side code.

**Tech Stack:** C++20 (`<filesystem>`), CMake, Catch2, CLI11, the #10 TOML backend. No new third-party deps.

**Spec:** `docs/superpowers/specs/2026-08-29-tools-t4-build-design.md`

## Global Constraints

- **Tier:** build/CLI code lives in `app/`; `engine/`/`foundation/` unchanged. Filesystem via `<filesystem>` + T2's `scene_io`, confined to `app/`.
- **Serialization is #10:** the bundle manifest is the reflected `ProjectManifest` via `to_toml(to_value(m))`; the scene is #19 JSON copied verbatim.
- **Reuse T2/T3:** `load_manifest` (T3), `save_scene_file`/`load_scene_file` (T2); the bundle *runs* through T3's `resolve_startup_scene` — no duplicated run logic.
- **Overwrite rule:** regenerate a prior bundle (has `mosaic.toml`); refuse a non-empty non-bundle `<out>` (leave it untouched); roll back a bundle-owned `<out>` on any write failure.
- **Build:** `cmake --preset debug` (once) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Exe: `build/debug/app/mosaic`.
- New test sources → `tests/CMakeLists.txt`; new compiled app sources → `app/CMakeLists.txt`. Tests link `mosaic_app`/`mosaic_serialization`.
- Namespaces: `mosaic::app`; TOML/reflect API is `mosaic::foundation::serialization::{to_value,to_toml}`. Catch2 names must not start with `--`.

---

### Task 1: `build_bundle` packager

**Files:**
- Create: `app/project/bundle.hpp`
- Test: `tests/unit/app/bundle_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ProjectManifest`/`kManifestName` (T3, `app/project/project.hpp`); `load_manifest` (T3, `app/project/project_io.hpp`); `save_scene_file`/`load_scene_file` (T2, `app/run/scene_io.hpp`); `create_project` + `populate_world` (used by the tests); `to_value`/`to_toml`.
- Produces:
  - `bool mosaic::app::looks_like_bundle(const std::filesystem::path& dir);` — has a `mosaic.toml`.
  - `bool mosaic::app::build_bundle(const std::string& project_dir, const std::string& out_dir, const std::string& runtime_binary, const mosaic::engine::reflection::ComponentReflection& types);`

- [ ] **Step 1: Write the failing test `tests/unit/app/bundle_test.cpp`**

```cpp
#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs)

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/project/bundle.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/scene_load.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::build_bundle;
using mosaic::app::create_project;
using mosaic::app::load_manifest;
using mosaic::app::Options;
using mosaic::app::populate_world;
using mosaic::app::save_scene_file;
using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::transform::Transform;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string tmp(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t4_") + tag + std::to_string(::getpid()))).string();
}
ComponentReflection make_types(TypeRegistry& reg)
{
    ComponentReflection t; register_builtin_components(reg, t); return t;
}
std::string make_dummy_binary(const std::string& path)
{
    const std::string bytes = "#!/bin/sh\necho fake-mosaic\n";
    REQUIRE(save_scene_file(path, bytes));
    return bytes;
}
bool is_executable(const fs::path& p)
{
    return (fs::status(p).permissions() & fs::perms::owner_exec) != fs::perms::none;
}
int transform_count(const std::string& scene_path, const ComponentReflection& types)
{
    Options o; o.scene = scene_path;
    World w;
    if (!populate_world(w, o, types)) return -1;
    int n = 0; w.for_each<Transform>([&](World::EntityID, Transform&) { ++n; });
    return n;
}
} // namespace

TEST_CASE("build_bundle produces a runnable self-contained bundle", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj");
    const std::string out  = tmp("out");
    const std::string bin  = tmp("bin") + ".exe";
    fs::remove_all(proj); fs::remove_all(out);
    REQUIRE(create_project(proj, types));
    const std::string bin_bytes = make_dummy_binary(bin);

    REQUIRE(build_bundle(proj, out, bin, types));

    // Manifest copied + parses, same default scene.
    auto m = load_manifest(out);
    REQUIRE(m.has_value());
    REQUIRE(m->default_scene == "scenes/main.json");
    // Scene copied + loads to the 7-entity demo.
    REQUIRE(fs::exists(fs::path(out) / "scenes" / "main.json"));
    REQUIRE(transform_count((fs::path(out) / "scenes" / "main.json").string(), types) == 7);
    // Runtime binary copied verbatim + executable.
    REQUIRE(fs::exists(fs::path(out) / "mosaic"));
    REQUIRE(mosaic::app::load_scene_file((fs::path(out) / "mosaic").string()).value() == bin_bytes);
    REQUIRE(is_executable(fs::path(out) / "mosaic"));
    // run.sh present + executable.
    REQUIRE(fs::exists(fs::path(out) / "run.sh"));
    REQUIRE(is_executable(fs::path(out) / "run.sh"));

    fs::remove_all(proj); fs::remove_all(out); fs::remove(bin);
}

TEST_CASE("build_bundle regenerates a prior bundle but refuses a non-bundle dir", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj2");
    const std::string out  = tmp("out2");
    const std::string bin  = tmp("bin2") + ".exe";
    fs::remove_all(proj); fs::remove_all(out);
    REQUIRE(create_project(proj, types));
    make_dummy_binary(bin);

    REQUIRE(build_bundle(proj, out, bin, types));
    REQUIRE(build_bundle(proj, out, bin, types)); // rebuild over the prior bundle: OK

    // A non-empty, non-bundle dir is refused and left untouched.
    const std::string junk = tmp("junk");
    fs::remove_all(junk); fs::create_directories(junk);
    REQUIRE(save_scene_file((fs::path(junk) / "keepme.txt").string(), "precious"));
    REQUIRE_FALSE(build_bundle(proj, junk, bin, types));
    REQUIRE(fs::exists(fs::path(junk) / "keepme.txt")); // untouched

    fs::remove_all(proj); fs::remove_all(out); fs::remove(bin); fs::remove_all(junk);
}

TEST_CASE("build_bundle fails on a non-project or a missing scene, rolling back", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string bin = tmp("bin3") + ".exe";
    make_dummy_binary(bin);

    // Non-project input.
    const std::string notproj = tmp("notproj");
    fs::remove_all(notproj); fs::create_directories(notproj);
    const std::string out1 = tmp("out3");
    fs::remove_all(out1);
    REQUIRE_FALSE(build_bundle(notproj, out1, bin, types));

    // Project whose default scene was deleted -> copy fails -> out rolled back.
    const std::string proj = tmp("proj3");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    fs::remove(fs::path(proj) / "scenes" / "main.json");
    const std::string out2 = tmp("out3b");
    fs::remove_all(out2);
    REQUIRE_FALSE(build_bundle(proj, out2, bin, types));
    REQUIRE_FALSE(fs::exists(out2)); // rolled back

    fs::remove_all(notproj); fs::remove_all(proj); fs::remove(bin);
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/project_resolve_test.cpp`:
```cmake
  unit/app/bundle_test.cpp
```
`cmake --build build/debug -j 2>&1 | tail -15` → FAIL (`bundle.hpp` missing).

- [ ] **Step 3: Write `app/project/bundle.hpp`**

```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "app/project/project.hpp"    // ProjectManifest, kManifestName
#include "app/project/project_io.hpp" // load_manifest
#include "app/run/scene_io.hpp"       // save_scene_file
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml

namespace mosaic::app
{
// A directory that already holds a mosaic.toml is a prior bundle / project — safe to regenerate.
inline bool looks_like_bundle(const std::filesystem::path& dir)
{
    std::error_code ec;
    return std::filesystem::exists(dir / kManifestName, ec);
}

// Package `project_dir` into a self-contained bundle at `out_dir`, copying `runtime_binary` (the
// mosaic executable) in. Returns false (and logs) on failure; rolls back a bundle-owned out_dir.
// `types` is threaded for a future re-serialize step; v0.1 copies the scene verbatim.
inline bool build_bundle(const std::string& project_dir, const std::string& out_dir,
                         const std::string& runtime_binary,
                         const mosaic::engine::reflection::ComponentReflection& /*types*/)
{
    namespace fs = std::filesystem;

    // 1. Validate the project.
    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("build", "not a Mosaic project: {}", project_dir);
        return false;
    }

    // 2. Overwrite rule: regenerate a prior bundle; refuse a non-bundle non-empty dir.
    const fs::path out(out_dir);
    std::error_code ec;
    if (fs::exists(out, ec) && !fs::is_empty(out, ec))
    {
        if (looks_like_bundle(out))
        {
            fs::remove_all(out, ec);
        }
        else
        {
            mosaic::foundation::log::error(
                "build", "refusing to overwrite non-bundle directory: {}", out_dir);
            return false; // leave it untouched
        }
    }

    // On any failure after this point, roll back the bundle dir we own.
    auto fail = [&](const std::string& msg) -> bool {
        mosaic::foundation::log::error("build", "{}", msg);
        std::error_code rmec;
        fs::remove_all(out, rmec);
        return false;
    };

    // 3. Copy the default scene (creating its parent dirs under the bundle).
    const fs::path scene_dst = out / manifest->default_scene;
    fs::create_directories(scene_dst.parent_path(), ec);
    if (ec) { return fail("could not create bundle directories: " + out_dir); }
    fs::copy_file(fs::path(project_dir) / manifest->default_scene, scene_dst,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) { return fail("could not copy scene: " + manifest->default_scene); }

    // 4. Write the manifest.
    const std::string manifest_toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(*manifest));
    if (!save_scene_file((out / kManifestName).string(), manifest_toml))
    {
        return fail("could not write bundle manifest: " + out_dir);
    }

    // 5. Copy the runtime binary + set the executable bit.
    fs::copy_file(runtime_binary, out / "mosaic", fs::copy_options::overwrite_existing, ec);
    if (ec) { return fail("could not copy runtime binary: " + runtime_binary); }
    fs::permissions(out / "mosaic",
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) { return fail("could not set executable bit on the runtime binary"); }

    // 6. Write run.sh + set the executable bit.
    const std::string run_sh =
        "#!/bin/sh\n"
        "exec \"$(dirname \"$0\")/mosaic\" run \"$(dirname \"$0\")\" --window \"$@\"\n";
    if (!save_scene_file((out / "run.sh").string(), run_sh))
    {
        return fail("could not write run.sh");
    }
    fs::permissions(out / "run.sh",
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    if (ec) { return fail("could not set executable bit on run.sh"); }

    mosaic::foundation::log::info("build", "built bundle at {}", out_dir);
    return true;
}
} // namespace mosaic::app
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "build_bundle" --output-on-failure
```
Expected: all three `build_bundle` cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/project/bundle.hpp tests/unit/app/bundle_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): build_bundle packager (scene + manifest + runtime binary + run.sh)"
```

---

### Task 2: CLI — `build` subcommand

**Files:**
- Modify: `app/cli/options.hpp` (Command::Build; `build_project`, `build_out`)
- Modify: `app/cli/options.cpp` (build subcommand)
- Test: extend `tests/unit/app/options_test.cpp`

**Interfaces:**
- Produces: `Command::Build`; `Options::build_project`, `Options::build_out` (both `std::optional<std::string>`).

- [ ] **Step 1: Extend `app/cli/options.hpp`**

```cpp
enum class Command { Run, Editor, Create, Build };
```
In `struct Options`, after `create_path`:
```cpp
    std::optional<std::string>   build_project; // positional: project dir to package (build)
    std::optional<std::string>   build_out;     // -o/--output: bundle output dir (build)
```

- [ ] **Step 2: Write the failing options tests — extend `tests/unit/app/options_test.cpp`**

Add (uses the file's existing `parse_args({...})` helper):
```cpp
TEST_CASE("build subcommand captures project and output", "[cli]")
{
    auto pr = parse_args({"mosaic", "build", "mygame", "-o", "dist"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Build);
    REQUIRE(pr.options.build_project.has_value());
    REQUIRE(*pr.options.build_project == "mygame");
    REQUIRE(pr.options.build_out.has_value());
    REQUIRE(*pr.options.build_out == "dist");
}

TEST_CASE("build without -o leaves the output unset", "[cli]")
{
    auto pr = parse_args({"mosaic", "build", "mygame"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Build);
    REQUIRE(*pr.options.build_project == "mygame");
    REQUIRE_FALSE(pr.options.build_out.has_value());
}
```
Build + run these → FAIL (`build` subcommand not implemented).

- [ ] **Step 3: Implement in `app/cli/options.cpp`**

Add the shared locals (near `std::string create_name;`):
```cpp
    std::string build_project_name;
    std::string build_out_dir;
```
Add the `build` subcommand after the `create` subcommand block (after line ~46):
```cpp
    CLI::App* build = app.add_subcommand("build", "Package a project into a standalone bundle");
    build->add_option("project", build_project_name, "Project directory to package")->required();
    build->add_option("-o,--output", build_out_dir, "Output bundle directory (default <project>/dist)");
```
Update the `require_subcommand` comment (line ~13) to mention `build`.

After parse, alongside the other command/field captures:
```cpp
    if (build->parsed())
    {
        result.options.command = Command::Build;
    }
    if (!build_project_name.empty())
    {
        result.options.build_project = build_project_name;
    }
    if (!build_out_dir.empty())
    {
        result.options.build_out = build_out_dir;
    }
```

- [ ] **Step 4: Build + run the CLI tests, then the full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "build subcommand|build without" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the two new cases PASS; full suite green (existing subcommands unaffected).

- [ ] **Step 5: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): build subcommand (project positional + -o output)"
```

---

### Task 3: Entry wiring — `run_build` + `main` dispatch (+ functional)

**Files:**
- Create: `app/run/build.hpp`, `app/run/build.cpp`
- Modify: `app/CMakeLists.txt` (add `run/build.cpp`)
- Modify: `app/main.cpp` (dispatch `Command::Build`)

**Interfaces:**
- Consumes: `build_bundle` (Task 1); `Options` (Task 2).
- Produces: `int mosaic::app::run_build(const Options& options);`

- [ ] **Step 1: Write `app/run/build.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic build <project> [-o <out>]`: package a project into a standalone bundle,
// embedding this running executable as the bundle's runtime. Returns a process exit code.
int run_build(const Options& options);
} // namespace mosaic::app
```

- [ ] **Step 2: Write `app/run/build.cpp`**

```cpp
#include "app/run/build.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include "app/project/bundle.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_build(const Options& options)
{
    if (!options.build_project)
    {
        mosaic::foundation::log::error("build", "build: no project given");
        return 1;
    }
    const std::string project = *options.build_project;
    // Default output is <project>/dist.
    const std::string out =
        options.build_out.value_or((std::filesystem::path(project) / "dist").string());

    // Embed this running executable as the bundle's runtime.
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
    {
        mosaic::foundation::log::error("build", "could not resolve the running executable path");
        return 1;
    }

    // register_builtin_components fills `types` (registry is a required side-effect out-param).
    mosaic::foundation::reflection::TypeRegistry    registry;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    return build_bundle(project, out, exe.string(), types) ? 0 : 1;
}
} // namespace mosaic::app
```

- [ ] **Step 3: Wire the source into `app/CMakeLists.txt`**

Add `run/build.cpp` to the `mosaic_app` source list (next to `run/create.cpp`):
```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/build.cpp
)
```

- [ ] **Step 4: Dispatch in `app/main.cpp`**

Add the include with the other app includes:
```cpp
#include "app/run/build.hpp"
```
Right after the `Command::Create` dispatch block (the `if (pr.options.command == Command::Create) { return run_create(pr.options); }`), add:
```cpp
    if (pr.options.command == Command::Build)
    {
        return run_build(pr.options);
    }
```

- [ ] **Step 5: Build, full suite, and the functional payoff**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
P="$(mktemp -d)/game"; ./build/debug/app/mosaic create "$P"; echo "create=$?"
B="$(mktemp -d)/bundle"; ./build/debug/app/mosaic build "$P" -o "$B"; echo "build=$?"
echo "=== bundle contents ==="; ls -R "$B"
# Run the bundle via its OWN copied binary, no source tree:
timeout 30 "$B/mosaic" run "$B" --window --ticks 3 2>/tmp/t4_bundle.err; echo "bundle-run=$?"; grep -iE "error|validation|VUID" /tmp/t4_bundle.err || echo "(clean)"
# The convenience launcher:
timeout 30 "$B/run.sh" --ticks 3 2>/tmp/t4_runsh.err; echo "run.sh=$?"; grep -iE "error|validation|VUID" /tmp/t4_runsh.err || echo "(clean)"
# Default output location:
./build/debug/app/mosaic build "$P"; echo "build-default=$?"; ls "$P/dist" >/dev/null && echo "dist created"
```
Expected: suite green; `create`/`build` exit 0; the bundle has `mosaic` + `mosaic.toml` + `scenes/main.json` + `run.sh`; `"$B/mosaic" run "$B" --window` and `"$B/run.sh"` both exit 0 with no validation errors (the bundle runs via its own copied binary); `<project>/dist` is created by the default-output build.

- [ ] **Step 6: Commit**

```bash
git add app/run/build.hpp app/run/build.cpp app/CMakeLists.txt app/main.cpp
git commit -m "feat(app): mosaic build dispatch + run_build (embed /proc/self/exe)"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 3, mark T4 complete and point to T5 (asset importer/cooker). Commit on this branch (rides in the T4 PR — no separate PR).
- **Finishing:** use `superpowers:finishing-a-development-branch` (present the merge/PR menu; base branch is `main`).
- **Visual check to request from the user:** `mosaic create g && mosaic build g -o dist && cd dist && ./run.sh` opens the demo from the bundle — no source tree, its own binary.
