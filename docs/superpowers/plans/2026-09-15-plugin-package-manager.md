# Plugin Package Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `mosaic plugin {list, install, remove}`, a local-only package
manager for reusable plugin bundles, sourced from a directory of packages
(default: a new bundled `packages/` directory) and tracked per-project in
`mosaic.toml`.

**Architecture:** Two new header-only cores mirror the existing
`app/project/project_io.hpp` split: `app/plugin/package_source.hpp`
discovers packages in a local source directory (`list_packages`,
`find_package`), and `app/plugin/package_install.hpp` installs/removes a
package into a project's `plugins/` directory and its manifest
(`install_package`, `remove_package`). `ProjectManifest` (existing,
`app/project/project.hpp`) gains an `installed_plugins` field tracking what's
installed. `app/run/plugin.cpp` is the thin CLI wrapper (three functions,
one per sub-subcommand), wired through three new `Command` enumerators.

**Tech Stack:** C++20, Catch2 (`tests/unit/app/*_test.cpp`), CLI11,
`std::filesystem`, the existing TOML/`Value` serialization stack (no new
serialization-layer code needed).

**Spec:** `docs/superpowers/specs/2026-09-15-plugin-package-manager-design.md`

## Global Constraints

- Local-only: `--source` is always a filesystem path in this slice, never a
  URL. No network code anywhere in this plan.
- The CLI verb is `mosaic plugin`, not `mosaic package` — `mosaic build`
  already owns "package" as a verb ("Package a project into a standalone
  bundle"); do not introduce a second, conflicting meaning.
- A package is a flat directory of `*.py` files plus `package.toml`
  (`name`, `version`, `description`) — no recursion into subdirectories, no
  dependency-of-a-dependency concept.
- `install`/`remove` must never touch a `.py` file in a project's
  `plugins/` directory that the manifest doesn't attribute to the package
  being installed/removed (never clobber a hand-written or
  differently-owned file).
- No version-constraint solver: a package has one version string; installing
  an already-installed package at a different version overwrites its own
  files and updates the recorded version, nothing more.

---

### Task 1: Package source discovery

**Files:**
- Create: `app/plugin/package_source.hpp`
- Create: `packages/hello-plugin/package.toml`, `packages/hello-plugin/hello.py`
- Create: `tests/unit/app/package_source_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `app/CMakeLists.txt` (add `MOSAIC_PACKAGES_DIR` — this task's own
  test, "the bundled hello-plugin example package is discoverable", calls
  `default_package_source()`, so the define must exist before this task's
  tests can even compile; Task 4 does NOT re-add it)

**Interfaces:**
- Consumes: `foundation::serialization::from_toml`/`to_toml`,
  `from_value`/`to_value` (existing, `foundation/serialization/toml_backend.hpp`,
  `foundation/serialization/reflect_value.hpp`). `load_scene_file` (existing,
  `app/run/scene_io.hpp`) for generic text reads.
- Produces: `mosaic::app::PackageInfo { std::string name, version, description;
  std::filesystem::path dir; }`. `mosaic::app::default_package_source() ->
  std::string`. `mosaic::app::list_packages(const std::string& source_dir) ->
  std::vector<PackageInfo>` (sorted alphabetically by name). `mosaic::app::
  find_package(const std::string& source_dir, const std::string& name) ->
  std::optional<PackageInfo>`. Task 3 (`package_install.hpp`) and Task 4
  (`app/run/plugin.cpp`) consume all four names above verbatim.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/package_source_test.cpp`:

```cpp
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_source.hpp"

using mosaic::app::find_package;
using mosaic::app::list_packages;
using mosaic::app::PackageInfo;

namespace
{
// A throwaway source directory under the test binary's CWD, unique per test run
// via Catch2's TEST_CASE name being distinct -- reused verbatim from this file's
// existing sibling tests' throwaway-directory convention (see project_create_test.cpp).
std::filesystem::path make_fixture_source(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkgsrc_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << contents;
}
} // namespace

TEST_CASE("list_packages returns only valid packages, sorted by name", "[app]")
{
    const std::filesystem::path root = make_fixture_source("list_valid");

    std::filesystem::create_directories(root / "zeta");
    write_file(root / "zeta" / "package.toml",
              "name = \"zeta\"\nversion = \"1.0.0\"\ndescription = \"Z package\"\n");

    std::filesystem::create_directories(root / "alpha");
    write_file(root / "alpha" / "package.toml",
              "name = \"alpha\"\nversion = \"2.0.0\"\ndescription = \"A package\"\n");

    // Missing package.toml entirely -- skipped, not an error.
    std::filesystem::create_directories(root / "no-manifest");

    // Unparseable package.toml -- skipped, not an error.
    std::filesystem::create_directories(root / "broken");
    write_file(root / "broken" / "package.toml", "this is not valid toml {{{");

    const std::vector<PackageInfo> packages = list_packages(root.string());

    REQUIRE(packages.size() == 2);
    REQUIRE(packages[0].name == "alpha");
    REQUIRE(packages[0].version == "2.0.0");
    REQUIRE(packages[0].description == "A package");
    REQUIRE(packages[1].name == "zeta");
}

TEST_CASE("list_packages on a nonexistent source directory returns empty", "[app]")
{
    const std::vector<PackageInfo> packages = list_packages("/nonexistent/mosaic/packages/dir");
    REQUIRE(packages.empty());
}

TEST_CASE("find_package finds an existing package and misses a nonexistent one", "[app]")
{
    const std::filesystem::path root = make_fixture_source("find");
    std::filesystem::create_directories(root / "widget");
    write_file(root / "widget" / "package.toml",
              "name = \"widget\"\nversion = \"0.1.0\"\ndescription = \"A widget\"\n");

    const std::optional<PackageInfo> found = find_package(root.string(), "widget");
    REQUIRE(found.has_value());
    REQUIRE(found->name == "widget");

    const std::optional<PackageInfo> missing = find_package(root.string(), "does-not-exist");
    REQUIRE_FALSE(missing.has_value());
}

TEST_CASE("the bundled hello-plugin example package is discoverable", "[app]")
{
    const std::optional<PackageInfo> found =
        find_package(mosaic::app::default_package_source(), "hello-plugin");
    REQUIRE(found.has_value());
    REQUIRE(found->version == "0.1.0");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL — `app/plugin/package_source.hpp` doesn't exist, and the new
test file isn't yet in `tests/CMakeLists.txt`'s source list. Add it now,
directly after `unit/app/import_plugins_test.cpp` (the list's existing
order is not alphabetical throughout — see the surrounding entries at
lines 57-74 of `tests/CMakeLists.txt` — append after the last `unit/app/`
entry without reordering any existing line):

```
  unit/app/package_source_test.cpp
```

Re-run the build. Expected: FAIL with a missing-header compile error.

- [ ] **Step 3: Write the minimal implementation**

Create `app/plugin/package_source.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/run/scene_io.hpp" // load_scene_file
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // SerializationError
#include "foundation/serialization/reflect_value.hpp" // from_value
#include "foundation/serialization/toml_backend.hpp"  // from_toml

namespace mosaic::app
{

// A package's own package.toml contents.
struct PackageManifest
{
    std::string name;
    std::string version;
    std::string description;
};

template <class V>
void reflect(V& v, PackageManifest& m)
{
    v.member("name", m.name);
    v.member("version", m.version);
    v.member("description", m.description);
}

// One discovered package: its manifest fields plus the directory it lives in
// (holding package.toml and its *.py files).
struct PackageInfo
{
    std::string           name;
    std::string           version;
    std::string           description;
    std::filesystem::path dir;
};

// The compile-time packages/ root (see app/CMakeLists.txt's MOSAIC_PACKAGES_DIR).
// A separate function (not the raw macro), mirroring default_templates_root() /
// default_docs_dir()-style helpers elsewhere in app/ -- lets tests point a source
// directory at a fixture instead of overriding a macro.
inline std::string default_package_source() { return MOSAIC_PACKAGES_DIR; }

// Parse `dir`/package.toml into a PackageManifest. nullopt if the file is missing
// or unparseable (the caller logs and skips).
inline std::optional<PackageManifest> load_package_manifest(const std::filesystem::path& dir)
{
    const std::optional<std::string> text = load_scene_file((dir / "package.toml").string());
    if (!text)
    {
        return std::nullopt;
    }
    try
    {
        PackageManifest m;
        mosaic::foundation::serialization::from_value(
            mosaic::foundation::serialization::from_toml(*text), m);
        return m;
    }
    catch (const mosaic::foundation::serialization::SerializationError&)
    {
        return std::nullopt;
    }
}

// Every valid package directly under `source_dir` (immediate subdirectories with a
// parseable package.toml), sorted alphabetically by name for deterministic output.
// A missing source_dir, or one with no valid packages, returns an empty vector --
// not an error (mirrors create_project's "no plugins/ folder is the common case,
// not an error" precedent). A subdirectory with a missing or unparseable
// package.toml is skipped with a logged warning, never treated as a fatal error
// for the whole listing.
inline std::vector<PackageInfo> list_packages(const std::string& source_dir)
{
    namespace fs = std::filesystem;
    std::vector<PackageInfo> packages;
    std::error_code dir_ec;
    if (!fs::is_directory(source_dir, dir_ec))
    {
        return packages;
    }
    std::error_code iter_ec;
    for (const auto& entry : fs::directory_iterator(source_dir, iter_ec))
    {
        if (iter_ec)
        {
            break;
        }
        if (!entry.is_directory())
        {
            continue;
        }
        const std::optional<PackageManifest> m = load_package_manifest(entry.path());
        if (!m)
        {
            mosaic::foundation::log::warn(
                "plugin", "skipping {} (missing or unparseable package.toml)",
                entry.path().string());
            continue;
        }
        packages.push_back(PackageInfo{m->name, m->version, m->description, entry.path()});
    }
    std::sort(packages.begin(), packages.end(),
             [](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; });
    return packages;
}

// The single package named `name` under `source_dir`, or nullopt if not found.
inline std::optional<PackageInfo> find_package(const std::string& source_dir, const std::string& name)
{
    for (const PackageInfo& p : list_packages(source_dir))
    {
        if (p.name == name)
        {
            return p;
        }
    }
    return std::nullopt;
}

} // namespace mosaic::app
```

Create `packages/hello-plugin/package.toml`:

```toml
name = "hello-plugin"
version = "0.1.0"
description = "A minimal example plugin package for mosaic plugin install."
```

Create `packages/hello-plugin/hello.py`:

```python
"""A minimal example plugin: one menu item that logs a message."""

MOSAIC_MENU_ITEMS = {"Say Hello (from hello-plugin)": "on_say_hello"}


def on_say_hello():
    print("Hello from the hello-plugin package!")
```

Add `MOSAIC_PACKAGES_DIR` to `app/CMakeLists.txt`'s existing
`target_compile_definitions` block (this task's own
`default_package_source()`/its test need this define to exist to compile at
all — it cannot wait for Task 4):

```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts"
  MOSAIC_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates"
  MOSAIC_PACKAGES_DIR="${CMAKE_SOURCE_DIR}/packages")
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[app]" -c "list_packages*"`

If that filter syntax doesn't select by name in this Catch2 version, instead
run: `./build/debug/tests/mosaic_unit_tests "[app]"` and confirm the 4 new
`TEST_CASE`s from Step 1 are present and green among the output (they will
be interleaved with every other `[app]`-tagged test already in the suite).

Expected: PASS — all 4 new cases green.

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_source.hpp packages/hello-plugin/package.toml packages/hello-plugin/hello.py tests/unit/app/package_source_test.cpp tests/CMakeLists.txt app/CMakeLists.txt
git commit -m "feat(app): add package source discovery (list_packages, find_package)"
```

---

### Task 2: Manifest extension (`installed_plugins`)

**Files:**
- Modify: `app/project/project.hpp`
- Modify: `tests/unit/app/project_manifest_test.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `mosaic::app::InstalledPlugin { std::string name, version;
  std::vector<std::string> files; }` with its `reflect()`. `ProjectManifest`
  gains `std::vector<InstalledPlugin> installed_plugins;`.
  `mosaic::app::kPluginsDir` (a new `inline constexpr const char*`, value
  `"plugins"`). Task 3 (`package_install.hpp`) consumes `InstalledPlugin`,
  `ProjectManifest::installed_plugins`, and `kPluginsDir` verbatim.

- [ ] **Step 1: Write the failing test**

In `tests/unit/app/project_manifest_test.cpp`, add (after the existing
`TEST_CASE`, reusing its includes/usings already in the file):

```cpp
TEST_CASE("ProjectManifest round-trips installed_plugins through TOML", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";
    m.installed_plugins.push_back(
        mosaic::app::InstalledPlugin{"hello-plugin", "0.1.0", {"hello.py"}});

    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.installed_plugins.size() == 1);
    REQUIRE(back.installed_plugins[0].name == "hello-plugin");
    REQUIRE(back.installed_plugins[0].version == "0.1.0");
    REQUIRE(back.installed_plugins[0].files == std::vector<std::string>{"hello.py"});
}

TEST_CASE("ProjectManifest with no installed_plugins key deserializes to an empty vector", "[app]")
{
    ProjectManifest m;
    m.name = "mygame";
    m.default_scene = "scenes/main.json";
    // installed_plugins left default-constructed (empty) -- simulates a project
    // manifest written before this feature existed.
    const std::string toml = to_toml(to_value(m));

    ProjectManifest back;
    from_value(from_toml(toml), back);
    REQUIRE(back.installed_plugins.empty());
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL to compile — `mosaic::app::InstalledPlugin` doesn't exist,
and `ProjectManifest` has no `installed_plugins` member.

- [ ] **Step 3: Extend `app/project/project.hpp`**

Change the file's full contents to:

```cpp
#pragma once

#include <string>
#include <vector>

namespace mosaic::app
{
// Well-known names inside a project directory.
inline constexpr const char* kManifestName = "mosaic.toml";
inline constexpr const char* kScenesDir    = "scenes";
inline constexpr const char* kAssetsDir    = "assets";
inline constexpr const char* kPluginsDir   = "plugins";
inline constexpr const char* kStarterScene = "scenes/main.json"; // manifest default_scene

// One installed plugin package, recorded in ProjectManifest.installed_plugins.
// `files` is the exact set of *.py filenames this package placed in plugins/ at
// install time -- stored explicitly (not re-derived from the source package at
// removal time) so `mosaic plugin remove` knows precisely what to delete even if
// the source package's own file list has since changed.
struct InstalledPlugin
{
    std::string              name;    // package name
    std::string              version; // package version at install time
    std::vector<std::string> files;   // filenames this package owns in plugins/
};

template <class V>
void reflect(V& v, InstalledPlugin& p)
{
    v.member("name", p.name);
    v.member("version", p.version);
    v.member("files", p.files);
}

// The project manifest (mosaic.toml), reflected so it round-trips through the #10 TOML backend
// exactly like AssetMetadata. Minimal on purpose (version/author would be speculative).
struct ProjectManifest
{
    std::string name;          // project name (defaults to the directory basename)
    std::string default_scene; // project-relative path to the scene opened by default
    std::vector<InstalledPlugin> installed_plugins; // packages installed via `mosaic plugin install`
};

template <class V>
void reflect(V& v, ProjectManifest& m)
{
    v.member("name", m.name);
    v.member("default_scene", m.default_scene);
    v.member("installed_plugins", m.installed_plugins);
}
} // namespace mosaic::app
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[app]"`

Expected: PASS — the 2 new cases plus every existing `[app]`-tagged test
still green (in particular the pre-existing "ProjectManifest round-trips
through TOML" case, which does not set `installed_plugins` and must still
pass with it defaulting to empty).

- [ ] **Step 5: Commit**

```bash
git add app/project/project.hpp tests/unit/app/project_manifest_test.cpp
git commit -m "feat(app): add InstalledPlugin + ProjectManifest.installed_plugins"
```

---

### Task 3: Install / remove

**Files:**
- Create: `app/plugin/package_install.hpp`
- Create: `tests/unit/app/package_install_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::app::PackageInfo` (Task 1, `app/plugin/package_source.hpp`).
  `mosaic::app::InstalledPlugin`, `mosaic::app::ProjectManifest::installed_plugins`,
  `mosaic::app::kPluginsDir`, `mosaic::app::kManifestName` (Task 2,
  `app/project/project.hpp`). `mosaic::app::load_manifest` (existing,
  `app/project/project_io.hpp`). `load_scene_file`/`save_scene_file` (existing,
  `app/run/scene_io.hpp`).
- Produces: `mosaic::app::save_manifest(const std::string& project_dir, const
  ProjectManifest&) -> bool`. `mosaic::app::install_package(const std::string&
  project_dir, const PackageInfo& pkg) -> bool`. `mosaic::app::remove_package(const
  std::string& project_dir, const std::string& name) -> bool`. Task 4
  (`app/run/plugin.cpp`) consumes `install_package`/`remove_package` verbatim.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/package_install_test.cpp`:

```cpp
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_install.hpp"
#include "app/project/project_io.hpp"
#include "engine/reflection/component_reflection.hpp"

using mosaic::app::InstalledPlugin;
using mosaic::app::install_package;
using mosaic::app::load_manifest;
using mosaic::app::PackageInfo;
using mosaic::app::remove_package;

namespace
{
std::filesystem::path make_fixture_project(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkginstall_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::ofstream manifest(root / "mosaic.toml", std::ios::binary | std::ios::trunc);
    manifest << "name = \"testproj\"\ndefault_scene = \"scenes/main.json\"\n";
    return root;
}

std::filesystem::path make_fixture_package(const std::string& tag, const std::string& name,
                                           const std::string& version)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkg_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::ofstream py(root / "widget.py", std::ios::binary | std::ios::trunc);
    py << "MOSAIC_MENU_ITEMS = {}\n";
    return root;
}
} // namespace

TEST_CASE("install_package copies files and records the manifest entry", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_basic");
    const std::filesystem::path pkg_dir = make_fixture_package("install_basic", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE(install_package(project.string(), pkg));

    REQUIRE(std::filesystem::exists(project / "plugins" / "widget.py"));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->installed_plugins.size() == 1);
    REQUIRE(manifest->installed_plugins[0].name == "widget");
    REQUIRE(manifest->installed_plugins[0].version == "1.0.0");
    REQUIRE(manifest->installed_plugins[0].files == std::vector<std::string>{"widget.py"});
}

TEST_CASE("install_package refuses to overwrite an untracked file", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_conflict");
    std::filesystem::create_directories(project / "plugins");
    std::ofstream hand_written(project / "plugins" / "widget.py", std::ios::binary | std::ios::trunc);
    hand_written << "# hand-written, not from any package\n";
    hand_written.close();

    const std::filesystem::path pkg_dir = make_fixture_package("install_conflict", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE_FALSE(install_package(project.string(), pkg));

    // The hand-written file must survive untouched.
    std::ifstream check(project / "plugins" / "widget.py");
    std::string first_line;
    std::getline(check, first_line);
    REQUIRE(first_line == "# hand-written, not from any package");

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->installed_plugins.empty());
}

TEST_CASE("installing the same package at the same version is a no-op success", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_idempotent");
    const std::filesystem::path pkg_dir = make_fixture_package("install_idempotent", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE(install_package(project.string(), pkg));
    REQUIRE(install_package(project.string(), pkg)); // second call, same version

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.size() == 1); // not duplicated
}

TEST_CASE("installing a newer version overwrites the package's own files and updates the record", "[app]")
{
    const std::filesystem::path project = make_fixture_project("install_upgrade");
    const std::filesystem::path pkg_dir = make_fixture_package("install_upgrade", "widget", "1.0.0");
    const PackageInfo v1{"widget", "1.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), v1));

    const PackageInfo v2{"widget", "2.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), v2));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.size() == 1);
    REQUIRE(manifest->installed_plugins[0].version == "2.0.0");
}

TEST_CASE("install_package fails on a non-project directory", "[app]")
{
    namespace fs = std::filesystem;
    const fs::path not_a_project = fs::temp_directory_path() / "mosaic_pkginstall_test_not_a_project";
    std::error_code ec;
    fs::remove_all(not_a_project, ec);
    fs::create_directories(not_a_project, ec); // exists, but has no mosaic.toml

    const std::filesystem::path pkg_dir = make_fixture_package("not_a_project", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};

    REQUIRE_FALSE(install_package(not_a_project.string(), pkg));
}

TEST_CASE("remove_package deletes owned files and the manifest entry, leaving unrelated files alone", "[app]")
{
    const std::filesystem::path project = make_fixture_project("remove_basic");
    const std::filesystem::path pkg_dir = make_fixture_package("remove_basic", "widget", "1.0.0");
    const PackageInfo pkg{"widget", "1.0.0", "A widget", pkg_dir};
    REQUIRE(install_package(project.string(), pkg));

    std::ofstream unrelated(project / "plugins" / "unrelated.py", std::ios::binary | std::ios::trunc);
    unrelated << "# not part of any package\n";
    unrelated.close();

    REQUIRE(remove_package(project.string(), "widget"));

    REQUIRE_FALSE(std::filesystem::exists(project / "plugins" / "widget.py"));
    REQUIRE(std::filesystem::exists(project / "plugins" / "unrelated.py"));

    const auto manifest = load_manifest(project.string());
    REQUIRE(manifest->installed_plugins.empty());
}

TEST_CASE("remove_package on a package that was never installed succeeds as a no-op", "[app]")
{
    const std::filesystem::path project = make_fixture_project("remove_noop");
    REQUIRE(remove_package(project.string(), "never-installed"));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL — `app/plugin/package_install.hpp` doesn't exist yet. Add the
new test file to `tests/CMakeLists.txt`, directly after
`unit/app/package_source_test.cpp` (added in Task 1):

```
  unit/app/package_install_test.cpp
```

Re-run the build. Expected: FAIL with a missing-header compile error.

- [ ] **Step 3: Write the minimal implementation**

Create `app/plugin/package_install.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/plugin/package_source.hpp" // PackageInfo
#include "app/project/project.hpp"       // ProjectManifest, InstalledPlugin, kManifestName, kPluginsDir
#include "app/project/project_io.hpp"    // load_manifest
#include "app/run/scene_io.hpp"          // save_scene_file
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reflect_value.hpp" // to_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml

namespace mosaic::app
{

// Write `manifest` to <project_dir>/mosaic.toml. Returns false (and logs) on
// write failure. Takes a non-const reference because to_value()/reflect() are
// generically non-const throughout this codebase's serialization stack (matching
// every other to_value(manifest) call site, e.g. create_project in project_io.hpp).
inline bool save_manifest(const std::string& project_dir, ProjectManifest& manifest)
{
    namespace fs = std::filesystem;
    const std::string manifest_toml =
        mosaic::foundation::serialization::to_toml(
            mosaic::foundation::serialization::to_value(manifest));
    if (!save_scene_file((fs::path(project_dir) / kManifestName).string(), manifest_toml))
    {
        mosaic::foundation::log::error("plugin", "could not write manifest for {}", project_dir);
        return false;
    }
    return true;
}

// Install `pkg` into `project_dir`: copies every *.py file directly under pkg.dir
// into <project_dir>/plugins/ (created if missing) and records the install in
// mosaic.toml's installed_plugins. Fails (nothing written) if project_dir isn't a
// Mosaic project, or any of the package's filenames already exists in plugins/ and
// isn't already tracked as belonging to this same package. Reinstalling the same
// package at the same version is a no-op success; at a different version, the
// package's own files are overwritten and the manifest entry's version updated --
// never touches a file this package doesn't own.
inline bool install_package(const std::string& project_dir, const PackageInfo& pkg)
{
    namespace fs = std::filesystem;

    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("plugin", "not a Mosaic project: {}", project_dir);
        return false;
    }

    std::vector<std::string> files;
    std::error_code list_ec;
    for (const auto& entry : fs::directory_iterator(pkg.dir, list_ec))
    {
        if (list_ec)
        {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".py")
        {
            files.push_back(entry.path().filename().string());
        }
    }
    std::sort(files.begin(), files.end());

    auto existing = std::find_if(
        manifest->installed_plugins.begin(), manifest->installed_plugins.end(),
        [&](const InstalledPlugin& p) { return p.name == pkg.name; });
    const bool already_installed = existing != manifest->installed_plugins.end();
    if (already_installed && existing->version == pkg.version)
    {
        mosaic::foundation::log::info(
            "plugin", "{} {} already installed in {}", pkg.name, pkg.version, project_dir);
        return true;
    }

    const fs::path plugins_dir = fs::path(project_dir) / kPluginsDir;
    std::error_code mkdir_ec;
    fs::create_directories(plugins_dir, mkdir_ec);
    if (mkdir_ec)
    {
        mosaic::foundation::log::error(
            "plugin", "could not create {}: {}", plugins_dir.string(), mkdir_ec.message());
        return false;
    }

    // Guard: refuse to clobber any file this package doesn't already own.
    for (const std::string& file : files)
    {
        const bool owned_by_this_package =
            already_installed &&
            std::find(existing->files.begin(), existing->files.end(), file) != existing->files.end();
        std::error_code exists_ec;
        if (fs::exists(plugins_dir / file, exists_ec) && !owned_by_this_package)
        {
            mosaic::foundation::log::error(
                "plugin", "refusing to overwrite untracked file {} while installing {}",
                (plugins_dir / file).string(), pkg.name);
            return false;
        }
    }

    for (const std::string& file : files)
    {
        std::error_code copy_ec;
        fs::copy_file(pkg.dir / file, plugins_dir / file, fs::copy_options::overwrite_existing, copy_ec);
        if (copy_ec)
        {
            mosaic::foundation::log::error(
                "plugin", "could not copy {}: {}", file, copy_ec.message());
            return false;
        }
    }

    if (already_installed)
    {
        existing->version = pkg.version;
        existing->files   = files;
    }
    else
    {
        manifest->installed_plugins.push_back(InstalledPlugin{pkg.name, pkg.version, files});
    }

    if (!save_manifest(project_dir, *manifest))
    {
        return false;
    }
    mosaic::foundation::log::info("plugin", "installed {} {} into {}", pkg.name, pkg.version, project_dir);
    return true;
}

// Remove package `name` from `project_dir`: deletes the *.py files its manifest
// entry says it owns from plugins/, then removes the manifest entry. Not
// installed is not an error -- returns true, a no-op.
inline bool remove_package(const std::string& project_dir, const std::string& name)
{
    namespace fs = std::filesystem;

    std::optional<ProjectManifest> manifest = load_manifest(project_dir);
    if (!manifest)
    {
        mosaic::foundation::log::error("plugin", "not a Mosaic project: {}", project_dir);
        return false;
    }

    auto it = std::find_if(
        manifest->installed_plugins.begin(), manifest->installed_plugins.end(),
        [&](const InstalledPlugin& p) { return p.name == name; });
    if (it == manifest->installed_plugins.end())
    {
        mosaic::foundation::log::info("plugin", "{} is not installed in {}", name, project_dir);
        return true;
    }

    const fs::path plugins_dir = fs::path(project_dir) / kPluginsDir;
    for (const std::string& file : it->files)
    {
        std::error_code rm_ec;
        fs::remove(plugins_dir / file, rm_ec);
    }

    manifest->installed_plugins.erase(it);
    if (!save_manifest(project_dir, *manifest))
    {
        return false;
    }
    mosaic::foundation::log::info("plugin", "removed {} from {}", name, project_dir);
    return true;
}

} // namespace mosaic::app
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[app]"`

Expected: PASS — the 7 new cases plus every existing `[app]`-tagged test
still green.

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_install.hpp tests/unit/app/package_install_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): add install_package/remove_package"
```

---

### Task 4: CLI wiring + docs

**Files:**
- Modify: `app/cli/options.hpp`
- Modify: `app/cli/options.cpp`
- Create: `app/run/plugin.hpp`, `app/run/plugin.cpp`
- Modify: `app/main.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/unit/app/options_test.cpp`
- Create: `docs/design/plugin-packages.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `mosaic::app::default_package_source`, `find_package` (Task 1).
  `mosaic::app::install_package`, `remove_package` (Task 3).
- Produces: nothing further downstream — this is the final task.

- [ ] **Step 1: Write the failing test**

In `tests/unit/app/options_test.cpp`, add (after the existing `TEST_CASE`s,
using the file's existing `parse_args` helper):

```cpp
TEST_CASE("plugin list subcommand parses with an optional --source", "[cli]")
{
    auto result = parse_args({"mosaic", "plugin", "list"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.command == mosaic::app::Command::PluginList);
    REQUIRE_FALSE(result.options.package_source.has_value());

    auto with_source = parse_args({"mosaic", "plugin", "list", "--source", "/tmp/mypkgs"});
    REQUIRE(with_source.options.command == mosaic::app::Command::PluginList);
    REQUIRE(with_source.options.package_source == "/tmp/mypkgs");
}

TEST_CASE("plugin install subcommand parses name, project, and optional --source", "[cli]")
{
    auto result = parse_args({"mosaic", "plugin", "install", "widget", "/tmp/myproj"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.command == mosaic::app::Command::PluginInstall);
    REQUIRE(result.options.plugin_name == "widget");
    REQUIRE(result.options.plugin_project == "/tmp/myproj");
    REQUIRE_FALSE(result.options.package_source.has_value());
}

TEST_CASE("plugin remove subcommand parses name and project", "[cli]")
{
    auto result = parse_args({"mosaic", "plugin", "remove", "widget", "/tmp/myproj"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.command == mosaic::app::Command::PluginRemove);
    REQUIRE(result.options.plugin_name == "widget");
    REQUIRE(result.options.plugin_project == "/tmp/myproj");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL to compile — `Command::PluginList`/`PluginInstall`/`PluginRemove`
and `Options::plugin_name`/`plugin_project`/`package_source` don't exist yet,
and `plugin` isn't a recognized CLI11 subcommand.

- [ ] **Step 3: Extend `app/cli/options.hpp`**

Change:

```cpp
enum class Command { Run, Editor, Create, Build, Import };
```

to:

```cpp
enum class Command { Run, Editor, Create, Build, Import, PluginList, PluginInstall, PluginRemove };
```

In the `Options` struct, after the existing `import_file` field, add:

```cpp
    std::optional<std::string>   plugin_name;    // positional: package name (plugin install/remove)
    std::optional<std::string>   plugin_project; // positional: project dir (plugin install/remove)
    std::optional<std::string>   package_source; // --source (plugin list/install)
```

- [ ] **Step 4: Extend `app/cli/options.cpp`**

Update the comment on `app.require_subcommand(1)`:

```cpp
    app.require_subcommand(1); // exactly one subcommand (`run`, `editor`, `create`, `build`, `import`, or `plugin`)
```

After the existing:

```cpp
    CLI::App* import = app.add_subcommand("import", "Import an asset file into a project");
    import->add_option("project", import_project_name, "Project directory")->required();
    import->add_option("file", import_file_name, "Source asset file (PPM image)")->required();
```

add:

```cpp

    CLI::App* plugin = app.add_subcommand("plugin", "Manage plugin packages in a project");
    plugin->require_subcommand(1);

    CLI::App* plugin_list = plugin->add_subcommand("list", "List packages available from a source");
    std::string plugin_list_source;
    plugin_list->add_option("--source", plugin_list_source,
                            "Package source directory (default: bundled packages/)");

    CLI::App* plugin_install = plugin->add_subcommand("install", "Install a package into a project");
    std::string plugin_install_name;
    std::string plugin_install_project;
    std::string plugin_install_source;
    plugin_install->add_option("name", plugin_install_name, "Package name")->required();
    plugin_install->add_option("project", plugin_install_project, "Project directory")->required();
    plugin_install->add_option("--source", plugin_install_source,
                               "Package source directory (default: bundled packages/)");

    CLI::App* plugin_remove = plugin->add_subcommand("remove", "Remove an installed package from a project");
    std::string plugin_remove_name;
    std::string plugin_remove_project;
    plugin_remove->add_option("name", plugin_remove_name, "Package name")->required();
    plugin_remove->add_option("project", plugin_remove_project, "Project directory")->required();
```

Then, after the existing:

```cpp
    if (import->parsed())
    {
        result.options.command = Command::Import;
    }
```

add:

```cpp
    if (plugin_list->parsed())
    {
        result.options.command = Command::PluginList;
    }
    if (plugin_install->parsed())
    {
        result.options.command = Command::PluginInstall;
    }
    if (plugin_remove->parsed())
    {
        result.options.command = Command::PluginRemove;
    }
    if (!plugin_list_source.empty())
    {
        result.options.package_source = plugin_list_source;
    }
    if (!plugin_install_source.empty())
    {
        result.options.package_source = plugin_install_source;
    }
    if (!plugin_install_name.empty())
    {
        result.options.plugin_name = plugin_install_name;
    }
    if (!plugin_remove_name.empty())
    {
        result.options.plugin_name = plugin_remove_name;
    }
    if (!plugin_install_project.empty())
    {
        result.options.plugin_project = plugin_install_project;
    }
    if (!plugin_remove_project.empty())
    {
        result.options.plugin_project = plugin_remove_project;
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[cli]"`

Expected: PASS — the 3 new cases plus every existing `[cli]`-tagged test
still green.

- [ ] **Step 6: Write `app/run/plugin.hpp`/`.cpp`**

Create `app/run/plugin.hpp`:

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic plugin list`. Returns a process exit code.
int run_plugin_list(const Options& options);
// Handle `mosaic plugin install <name> <project>`. Returns a process exit code.
int run_plugin_install(const Options& options);
// Handle `mosaic plugin remove <name> <project>`. Returns a process exit code.
int run_plugin_remove(const Options& options);
} // namespace mosaic::app
```

Create `app/run/plugin.cpp`:

```cpp
#include "app/run/plugin.hpp"

#include <iostream>

#include "app/plugin/package_install.hpp"
#include "app/plugin/package_source.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
int run_plugin_list(const Options& options)
{
    const std::string source = options.package_source.value_or(default_package_source());
    const std::vector<PackageInfo> packages = list_packages(source);
    if (packages.empty())
    {
        mosaic::foundation::log::info("plugin", "no packages found in {}", source);
        return 0;
    }
    for (const PackageInfo& p : packages)
    {
        std::cout << p.name << " " << p.version << " - " << p.description << "\n";
    }
    return 0;
}

int run_plugin_install(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin install <name> <project>");
        return 1;
    }
    const std::string source = options.package_source.value_or(default_package_source());
    const std::optional<PackageInfo> pkg = find_package(source, *options.plugin_name);
    if (!pkg)
    {
        mosaic::foundation::log::error(
            "plugin", "package '{}' not found in {}", *options.plugin_name, source);
        return 1;
    }
    return install_package(*options.plugin_project, *pkg) ? 0 : 1;
}

int run_plugin_remove(const Options& options)
{
    if (!options.plugin_name || !options.plugin_project)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin remove <name> <project>");
        return 1;
    }
    return remove_package(*options.plugin_project, *options.plugin_name) ? 0 : 1;
}
} // namespace mosaic::app
```

- [ ] **Step 7: Wire `app/main.cpp`**

Add the include, alongside the other `app/run/*.hpp` includes (next to
`#include "app/run/import.hpp"`):

```cpp
#include "app/run/plugin.hpp"
```

After the existing `Command::Import` dispatch block (`if (pr.options.command
== Command::Import) { return run_import(pr.options); }`), add:

```cpp
    if (pr.options.command == Command::PluginList)
    {
        return run_plugin_list(pr.options);
    }

    if (pr.options.command == Command::PluginInstall)
    {
        return run_plugin_install(pr.options);
    }

    if (pr.options.command == Command::PluginRemove)
    {
        return run_plugin_remove(pr.options);
    }
```

- [ ] **Step 8: Wire `app/CMakeLists.txt`**

In the `add_library(mosaic_app STATIC ...)` source list, add `run/plugin.cpp`
right after `run/import.cpp`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
)
```

`MOSAIC_PACKAGES_DIR` was already added to `target_compile_definitions` in
Task 1 (needed there for that task's own tests to compile) — nothing further
to do to that block in this task.

- [ ] **Step 9: Build the full binary and unit tests**

Run: `cmake --preset debug 2>&1 | tail -10 && cmake --build build/debug --target mosaic --target mosaic_unit_tests -- -j2 2>&1 | tail -80`

Run this in the foreground (not a background job) — background C++ builds
in this environment have intermittently been killed by an over-eager
memory guard even with ample real memory free; foreground builds have not
shown this problem.

Expected: PASS — clean build of both `mosaic` and `mosaic_unit_tests`.

- [ ] **Step 10: Run the full unit test suite**

Run: `./build/debug/tests/mosaic_unit_tests`

Expected: PASS — all tests green, except possibly the pre-existing, already
documented flaky `threaded ScriptSystem applies script writes one tick
later` SIGSEGV (unrelated to this branch) if it reproduces — do not treat
that one specific case as a regression.

- [ ] **Step 11: Exercise the CLI for real**

Run, in order, against a throwaway project directory (e.g. `/tmp/mosaic_pkgtest_manual`):

```bash
rm -rf /tmp/mosaic_pkgtest_manual
./build/debug/app/mosaic create /tmp/mosaic_pkgtest_manual
./build/debug/app/mosaic plugin list
./build/debug/app/mosaic plugin install hello-plugin /tmp/mosaic_pkgtest_manual
cat /tmp/mosaic_pkgtest_manual/mosaic.toml
ls /tmp/mosaic_pkgtest_manual/plugins
./build/debug/app/mosaic plugin remove hello-plugin /tmp/mosaic_pkgtest_manual
cat /tmp/mosaic_pkgtest_manual/mosaic.toml
ls /tmp/mosaic_pkgtest_manual/plugins
```

Expected: `plugin list` prints `hello-plugin 0.1.0 - A minimal example
plugin package for mosaic plugin install.`; after `install`, `mosaic.toml`
shows an `[[installed_plugins]]` table with `name = "hello-plugin"` and
`plugins/hello.py` exists; after `remove`, the `installed_plugins` table is
gone from `mosaic.toml` and `plugins/hello.py` no longer exists.

- [ ] **Step 12: Write `docs/design/plugin-packages.md`**

Create `docs/design/plugin-packages.md`:

```markdown
# Plugin packages

`mosaic plugin {list, install, remove}` manages reusable plugin bundles —
one or more `.py` files following the existing `MOSAIC_MENU_ITEMS`/
`MOSAIC_PANELS`/`MOSAIC_IMPORTERS` conventions — sourced from a local
directory and tracked per-project.

## Package format

A package is a directory containing `package.toml` (`name`, `version`,
`description`) plus one or more `*.py` files at its top level (not
recursive). The package manager never parses or validates the Python
content — `PluginHost`/`ImportPluginHost` do that, at load time, same as
any other file that ends up in a project's `plugins/`.

## Source resolution

A source is any local directory whose immediate subdirectories are
packages. The default source is the compile-time `MOSAIC_PACKAGES_DIR`
(`packages/` at the repo root, mirroring `MOSAIC_TEMPLATES_DIR`); `--source
<dir>` overrides it with any other local directory. This repo ships one
real example package, `packages/hello-plugin/`.

**Local only, for now:** `--source` is always a filesystem path in this
slice — no URL, no network fetch. This is a deliberate seam for later, not
a limitation baked in forever: a future slice could accept a URL in
`--source`, resolve it by downloading to a local cache directory, and hand
that path straight to the same local-source code described here, with no
redesign of `list_packages`/`install_package`/`remove_package` needed.

## Tracking installs

`mosaic.toml` gains an `installed_plugins` array of tables, one per
installed package: `name`, `version`, and `files` (the exact `.py`
filenames this package placed in `plugins/`). `install` refuses to
overwrite a `plugins/*.py` file it doesn't already own (per this record) —
a hand-written plugin file, or a different package's file, is never
clobbered. `remove` deletes only the files its own record lists.
Reinstalling the same package at the same version is a no-op; at a
different version, the package's own files are overwritten and the
recorded version updated — there's no dependency-constraint solver here,
just a record of what's currently present.

## CLI naming: `mosaic plugin`, not `mosaic package`

`mosaic build`'s help text already reads "Package a project into a
standalone bundle" — `build` is the existing owner of "package" as a verb
in this CLI. The new subcommand is `mosaic plugin` instead, matching this
repo's existing term for the artifact being managed (`PluginHost`,
`plugins/`, `MOSAIC_MENU_ITEMS`).

## Non-goals

- Network fetch, a remote/community registry — future work; see the design
  spec's Non-goals for the full list.
- Version-constraint resolution ("package A requires package B >= 1.2") —
  not modeled; a package has a single version string.
- Templates as packages — templates keep their own, separate `--template`
  mechanism (`docs/design/project-templates.md`).

Full design: `docs/superpowers/specs/2026-09-15-plugin-package-manager-design.md`.
```

- [ ] **Step 13: Add the `docs/project/next-steps.md` entry**

Prepend a new entry to the `**Last updated:**` line (following the file's
existing convention: each entry is `(...)` joined by ` ¶ ` to the previous
one), dated today, summarizing: `mosaic plugin {list, install, remove}`
manages local plugin packages; the package format (`package.toml` + `.py`
files); `MOSAIC_PACKAGES_DIR`/`packages/hello-plugin` as the bundled
example; `installed_plugins` in `mosaic.toml`; the local-only scoping and
the seam left for a future network-fetch source; the `mosaic plugin` (not
`mosaic package`) naming decision and why. Link the spec:
`docs/superpowers/specs/2026-09-15-plugin-package-manager-design.md`.

- [ ] **Step 14: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp app/run/plugin.hpp app/run/plugin.cpp app/main.cpp app/CMakeLists.txt tests/unit/app/options_test.cpp docs/design/plugin-packages.md docs/project/next-steps.md
git commit -m "feat(cli): add mosaic plugin {list,install,remove}"
```
