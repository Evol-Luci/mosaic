# Plugin SDK — Custom Asset Importers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a project-local plugin register a custom asset importer (a file
extension → Python function mapping), consulted by the `mosaic import` CLI command
when a source file's extension isn't one of the four built-in kinds.

**Architecture:** A new app-tier class, `ImportPluginHost`, discovers `plugins/*.py`
and dispatches to a plugin's `MOSAIC_IMPORTERS` dict — structurally parallel to
`studio::PluginHost` but never binding a `World` (app and studio are sibling tiers;
neither may depend on the other). `import_asset`'s dispatch chain grows one new
branch between its built-in extensions and its PPM fallback; `load_project_assets`
gains an explicit generic-blob path so a plugin-imported asset round-trips instead of
silently mis-decoding as a corrupt image.

**Tech Stack:** C++20, CMake, Catch2, the existing `mosaic::engine::script::ScriptEngine`
interface (`modules/script_python`'s `PythonScriptEngine`), Python (embedded CPython).

**Spec:** `docs/superpowers/specs/2026-09-14-plugin-sdk-custom-importers-design.md`

## Global Constraints

- `app` and `studio` are sibling tiers (`docs/architecture/boundaries.md`) — the new
  `ImportPluginHost` must not depend on `studio::PluginHost` or anything else under
  `studio/`.
- No changes to the `ScriptEngine` interface, `PythonScriptEngine`, or
  `StudioPluginScriptEngine` — the importer-invocation mechanism composes only the
  methods `ScriptEngine` already exposes (`set_global`, `run_string`, `get_global`).
- An `ImportPluginHost`-loaded engine never calls `bind_world`/`bind_commands` — an
  importer plugin has no `get_component`/`set_component`/etc. globals available, by
  design.
- v1 is text-only: an importer function must return a Python `str`; no binary/bytes
  support.
- Register-only: an imported asset is retrievable via
  `AssetRegistry::load<std::string>(id)` (already works today, no engine change) —
  no new gameplay-script getter.
- No Studio UI import trigger this slice — Studio's Assets panel stays read-only;
  importing only happens through the `mosaic import` CLI command.
- `plugins/` resolves as `<project_dir>/plugins` for imports (not CWD-relative) —
  `import_asset` already takes `project_dir` explicitly, unlike the editor's
  documented CWD gap (`docs/design/scripting.md`).
- Same all-or-nothing per-file plugin-load semantics as `studio::PluginHost`: a
  malformed `MOSAIC_IMPORTERS` dict discards that whole plugin file, collected as an
  error string, never blocking sibling files.

---

### Task 1: `ImportPluginHost` — a new, app-tier plugin loader

**Files:**
- Create: `app/asset/import_plugins.hpp`
- Create: `app/asset/import_plugins.cpp`
- Test: `tests/unit/app/import_plugins_test.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `mosaic::engine::script::ScriptEngine` (`run_string`, `get_global`,
  `set_global`, `call_function` — none of which change),
  `mosaic::script::python::create_python_script_engine()` (existing, used by tests
  here and by Task 2's real call site).
- Produces:
  ```cpp
  struct PluginImporter { std::string plugin_name; std::string extension; std::string function_name; };
  class ImportPluginHost {
  public:
      using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;
      explicit ImportPluginHost(EngineFactory factory);
      std::vector<std::string> load(const std::string& directory);
      const std::vector<PluginImporter>& importers() const;
      const PluginImporter* find_importer(const std::string& extension) const;
      struct Result { std::optional<std::string> blob; std::string error; };
      Result invoke(const PluginImporter& item, const std::string& source_path);
  };
  ```
  Task 2 constructs an `ImportPluginHost` with
  `mosaic::script::python::create_python_script_engine`, calls `load()` with
  `<project_dir>/plugins`, and uses `find_importer`/`invoke` with these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/import_plugins_test.cpp`:

```cpp
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/asset/import_plugins.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::ImportPluginHost;
using mosaic::app::PluginImporter;
using mosaic::script::python::create_python_script_engine;

namespace
{
namespace fs = std::filesystem;

// A fresh, uniquely-named temp directory for one test's plugin files.
fs::path make_plugin_dir(const char* tag)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() /
        (std::string("mosaic_import_plugin_test_") + tag + "_" + std::to_string(now));
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}
} // namespace

TEST_CASE("ImportPluginHost loads a plugin importer and invoke returns its string result",
         "[app]")
{
    const fs::path dir = make_plugin_dir("basic");
    write_file(dir / "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");
    const fs::path source = make_plugin_dir("basic_src") / "data.csv";
    write_file(source, "a,b,c\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.empty());
    REQUIRE(host.importers().size() == 1);
    REQUIRE(host.importers()[0].extension == ".csv");

    const auto* found = host.find_importer(".csv");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, source.string());
    REQUIRE(result.blob.has_value());
    REQUIRE(*result.blob == "A,B,C\n");

    fs::remove_all(dir);
    fs::remove_all(source.parent_path());
}

TEST_CASE("ImportPluginHost treats a non-table MOSAIC_IMPORTERS as a load error", "[app]")
{
    const fs::path dir = make_plugin_dir("badtable");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = 5\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost treats a non-string function name as a load error", "[app]")
{
    const fs::path dir = make_plugin_dir("badvalue");
    write_file(dir / "bad.py", "MOSAIC_IMPORTERS = {\".x\": 5}\n");

    ImportPluginHost host(create_python_script_engine);
    const auto errors = host.load(dir.string());
    REQUIRE(errors.size() == 1);
    REQUIRE(host.importers().empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost invoke fails when the function raises", "[app]")
{
    const fs::path dir = make_plugin_dir("raises");
    write_file(dir / "boom.py",
        "MOSAIC_IMPORTERS = {\".boom\": \"import_boom\"}\n\n"
        "def import_boom(source_path):\n"
        "    raise ValueError(\"nope\")\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".boom");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty());

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost invoke fails when the function returns a non-string", "[app]")
{
    const fs::path dir = make_plugin_dir("badreturn");
    write_file(dir / "num.py",
        "MOSAIC_IMPORTERS = {\".num\": \"import_num\"}\n\n"
        "def import_num(source_path):\n"
        "    return 42\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".num");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE(result.error == "'import_num' must return a string");

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost::find_importer returns nullptr for an unknown extension", "[app]")
{
    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.find_importer(".nope") == nullptr);
}

TEST_CASE("ImportPluginHost.load a second time replaces the previous plugin set", "[app]")
{
    const fs::path dir = make_plugin_dir("reload");
    write_file(dir / "a.py", "MOSAIC_IMPORTERS = {\".a\": \"f\"}\ndef f(p):\n    return \"a\"\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    REQUIRE(host.importers().size() == 1);

    fs::remove(dir / "a.py");
    write_file(dir / "b.py", "MOSAIC_IMPORTERS = {\".b\": \"g\"}\ndef g(p):\n    return \"b\"\n");
    REQUIRE(host.load(dir.string()).empty());
    REQUIRE(host.importers().size() == 1);
    REQUIRE(host.importers()[0].extension == ".b");
    REQUIRE(host.find_importer(".a") == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost gives importer plugins no gameplay globals", "[app]")
{
    const fs::path dir = make_plugin_dir("noworld");
    write_file(dir / "touches_world.py",
        "MOSAIC_IMPORTERS = {\".w\": \"import_w\"}\n\n"
        "def import_w(source_path):\n"
        "    return str(get_component(1, \"Name\"))\n");

    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load(dir.string()).empty());
    const auto* found = host.find_importer(".w");
    REQUIRE(found != nullptr);
    const auto result = host.invoke(*found, "/whatever");
    REQUIRE_FALSE(result.blob.has_value());
    REQUIRE_FALSE(result.error.empty()); // NameError: get_component is not defined

    fs::remove_all(dir);
}

TEST_CASE("ImportPluginHost.load on a missing directory returns no errors and no importers",
         "[app]")
{
    ImportPluginHost host(create_python_script_engine);
    REQUIRE(host.load("/no/such/mosaic_import_plugins_dir").empty());
    REQUIRE(host.importers().empty());
}
```

Add `unit/app/import_plugins_test.cpp` to `tests/CMakeLists.txt`, right after the
existing line `unit/app/asset_import_test.cpp`.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build fails — `app/asset/import_plugins.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

Create `app/asset/import_plugins.hpp`:

```cpp
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/script/script_engine.hpp"

namespace mosaic::app
{
// One registered importer, read out of a plugin's MOSAIC_IMPORTERS dict.
struct PluginImporter
{
    std::string plugin_name;   // filename stem, e.g. "csv_importer" -- for error messages
    std::string extension;     // e.g. ".csv" (leading dot, matches
                                // std::filesystem::path::extension())
    std::string function_name; // e.g. "import_csv"
};

// Discovers and runs project-local plugins/*.py files -- the same directory
// convention studio::PluginHost uses, but app-tier: neither depends on the other
// (see docs/architecture/boundaries.md). Unlike PluginHost, an ImportPluginHost's
// engines are never bound to a World/ComponentReflection/CommandStack -- an importer
// has no reason to touch entities or undo/redo, so get_component/set_component/etc.
// simply don't exist as globals here. A plugin registers importers declaratively via
// a module-level MOSAIC_IMPORTERS dict mapping a file extension (leading dot) to a
// one-argument function name; ImportPluginHost reads that dict back via
// ScriptEngine::get_global and invokes the function via set_global + run_string +
// get_global (passing the source path in, reading the returned string back out) --
// no pybind11/Python.h dependency here, or anywhere else in app/.
class ImportPluginHost
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ImportPluginHost(EngineFactory factory) : factory_(std::move(factory)) {}

    // Scans `directory` (non-recursive) for *.py files, running each through a fresh
    // engine. Replaces any previously loaded plugins (this is what a repeated call
    // does). Returns one human-readable error string per plugin that failed to load
    // (empty on full success) -- a broken plugin file never prevents the others in
    // the same directory from loading. A missing or non-directory `directory` is not
    // an error: returns an empty vector, zero importers.
    std::vector<std::string> load(const std::string& directory);

    // Every registered importer across all successfully loaded plugins, in load
    // order then MOSAIC_IMPORTERS declaration order.
    const std::vector<PluginImporter>& importers() const { return importers_; }

    // Finds a registered importer by extension (leading dot, e.g. ".csv"), or
    // nullptr if none matches. The first plugin (by load order) to register a given
    // extension wins if more than one does.
    const PluginImporter* find_importer(const std::string& extension) const;

    // Result of invoking a plugin's importer function.
    struct Result
    {
        std::optional<std::string> blob;  // the function's returned string, on success
        std::string                error; // set (and blob empty) on any failure
    };

    // Invokes item's function with source_path as its one argument, returning its
    // string return value. Fails (blob=nullopt, error set) if the plugin isn't
    // loaded, the function raises, or the function doesn't return a string.
    Result invoke(const PluginImporter& item, const std::string& source_path);

private:
    struct LoadedPlugin
    {
        std::string name;
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine;
    };

    EngineFactory                factory_;
    std::vector<LoadedPlugin>    plugins_;
    std::vector<PluginImporter>  importers_;
};

} // namespace mosaic::app
```

Create `app/asset/import_plugins.cpp`:

```cpp
#include "app/asset/import_plugins.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "foundation/serialization/value.hpp"

namespace mosaic::app
{
namespace
{
namespace fs = std::filesystem;
namespace ser = mosaic::foundation::serialization;

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
} // namespace

std::vector<std::string> ImportPluginHost::load(const std::string& directory)
{
    plugins_.clear();
    importers_.clear();
    std::vector<std::string> errors;

    std::error_code ec;
    if (!fs::exists(directory, ec) || !fs::is_directory(directory, ec)) { return errors; }

    std::error_code it_ec;
    for (auto it = fs::directory_iterator(directory, ec); it != fs::directory_iterator();
        it.increment(it_ec))
    {
        if (it_ec) { break; }
        const auto& entry = *it;
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec || entry.path().extension() != ".py") { continue; }

        const std::string name = entry.path().stem().string();
        if (!factory_)
        {
            errors.push_back(name + ": scripting is not available in this build");
            continue;
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        if (!engine)
        {
            errors.push_back(name + ": failed to create a script engine");
            continue;
        }

        const auto run = engine->run_string(read_file(entry.path()));
        if (!run.ok)
        {
            errors.push_back(name + ": " + run.error);
            continue;
        }

        bool item_error = false;
        std::vector<PluginImporter> plugin_importers;
        const auto importer_dict = engine->get_global("MOSAIC_IMPORTERS");
        if (importer_dict.has_value())
        {
            if (!importer_dict->is<ser::Table>())
            {
                errors.push_back(name + ": MOSAIC_IMPORTERS must be a dict");
                item_error = true;
            }
            else
            {
                for (const auto& [extension, value] : importer_dict->as<ser::Table>())
                {
                    if (!value.is<std::string>())
                    {
                        errors.push_back(name + ": MOSAIC_IMPORTERS['" + extension +
                                         "'] must be a string function name");
                        item_error = true;
                        break;
                    }
                    plugin_importers.push_back(PluginImporter{name, extension, value.as<std::string>()});
                }
            }
        }

        if (item_error) { continue; }

        plugins_.push_back(LoadedPlugin{name, std::move(engine)});
        for (auto& importer : plugin_importers) { importers_.push_back(std::move(importer)); }
    }

    return errors;
}

const PluginImporter* ImportPluginHost::find_importer(const std::string& extension) const
{
    for (const auto& importer : importers_)
    {
        if (importer.extension == extension) { return &importer; }
    }
    return nullptr;
}

ImportPluginHost::Result ImportPluginHost::invoke(const PluginImporter& item,
                                                   const std::string& source_path)
{
    for (auto& plugin : plugins_)
    {
        if (plugin.name != item.plugin_name) { continue; }

        plugin.engine->set_global("__mosaic_import_source__", ser::Value(source_path));
        const auto run = plugin.engine->run_string(
            "__mosaic_import_result__ = " + item.function_name + "(__mosaic_import_source__)\n");
        if (!run.ok) { return Result{std::nullopt, run.error}; }

        const auto result = plugin.engine->get_global("__mosaic_import_result__");
        if (!result.has_value() || !result->is<std::string>())
        {
            return Result{std::nullopt, "'" + item.function_name + "' must return a string"};
        }
        return Result{result->as<std::string>(), ""};
    }
    return Result{std::nullopt, "plugin '" + item.plugin_name + "' is not loaded"};
}

} // namespace mosaic::app
```

In `app/CMakeLists.txt`, find:
```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/build.cpp
  run/import.cpp
  asset/font_cook.cpp
)
```
and add `asset/import_plugins.cpp` after `asset/font_cook.cpp`:
```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/build.cpp
  run/import.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
)
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "ImportPluginHost*"
```
Expected: all 9 cases pass.

- [ ] **Step 5: Commit**

```bash
git add app/asset/import_plugins.hpp app/asset/import_plugins.cpp \
        tests/unit/app/import_plugins_test.cpp app/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(app): add ImportPluginHost (app-tier plugin importer discovery)"
```

---

### Task 2: `import_asset` consults plugin importers before falling back to PPM

**Files:**
- Modify: `app/asset/asset_io.hpp`
- Test: `tests/unit/app/asset_import_test.cpp`

**Interfaces:**
- Consumes: Task 1's `ImportPluginHost`, `PluginImporter`,
  `mosaic::script::python::create_python_script_engine`.
- Produces: no new interface — `import_asset`'s signature and return type
  (`std::optional<AssetID>`) are unchanged; only its internal dispatch changes. Task 3
  and Task 4 rely on the *behavior* (asset_type strings, `.data` blob extension), not
  a new symbol.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/app/asset_import_test.cpp`, a new helper right after the existing
`write_py` helper (inside the same anonymous namespace):

```cpp
void write_plugin(const std::string& project_dir, const std::string& filename,
                  const std::string& source)
{
    const fs::path dir = fs::path(project_dir) / "plugins";
    std::error_code ec;
    fs::create_directories(dir, ec);
    REQUIRE(save_scene_file((dir / filename).string(), source));
}
```

Then append these test cases to the file:

```cpp
TEST_CASE("import_asset uses a plugin importer for an unrecognized extension", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("iproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    write_plugin(proj, "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");

    const std::string src = tmp("isrc") + ".csv";
    REQUIRE(save_scene_file(src, "a,b,c\n"));

    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".data")));
    const auto blob = mosaic::app::load_scene_file(
        (fs::path(proj) / "assets" / (stem + ".data")).string());
    REQUIRE(blob.has_value());
    REQUIRE(*blob == "A,B,C\n");
    const auto meta_text = mosaic::app::load_scene_file(
        (fs::path(proj) / "assets" / (stem + ".toml")).string());
    REQUIRE(meta_text.has_value());
    REQUIRE(meta_text->find("type = \"csv\"") != std::string::npos);

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("import_asset refuses a plugin extension that would produce a reserved asset type",
         "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("rproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    write_plugin(proj, "bad_importer.py",
        "MOSAIC_IMPORTERS = {\".texture\": \"import_texture\"}\n\n"
        "def import_texture(source_path):\n"
        "    return \"whatever\"\n");

    const std::string src = tmp("rsrc") + ".texture";
    REQUIRE(save_scene_file(src, "whatever\n"));

    REQUIRE_FALSE(import_asset(proj, src).has_value());

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("import_asset still falls back to PPM decoding when no plugin claims the extension",
         "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("fproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    write_plugin(proj, "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    return \"unused\"\n");

    const std::string src = tmp("fsrc") + ".ppm";
    write_ppm(src);

    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".image")));

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("import_asset reports a plugin importer's error and writes nothing", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("eproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    write_plugin(proj, "boom_importer.py",
        "MOSAIC_IMPORTERS = {\".boom\": \"import_boom\"}\n\n"
        "def import_boom(source_path):\n"
        "    raise ValueError(\"nope\")\n");

    const std::string src = tmp("esrc") + ".boom";
    REQUIRE(save_scene_file(src, "x\n"));

    REQUIRE_FALSE(import_asset(proj, src).has_value());
    const std::string stem = fs::path(src).stem().string();
    REQUIRE_FALSE(fs::exists(fs::path(proj) / "assets" / (stem + ".data")));
    REQUIRE_FALSE(fs::exists(fs::path(proj) / "assets" / (stem + ".toml")));

    fs::remove_all(proj); fs::remove(src);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "import_asset*"
```
Expected: the 4 new cases FAIL (extensions like `.csv`/`.texture`/`.boom` currently
always attempt a PPM decode and fail with "not a valid PPM", not the new plugin-aware
behavior).

- [ ] **Step 3: Implement**

In `app/asset/asset_io.hpp`, add two new includes right after the existing
`#include "app/asset/font_cook.hpp"` line:

```cpp
#include "app/asset/import_plugins.hpp"
#include "modules/script_python/python_script_engine.hpp"
```

Replace the existing final `else` branch of `import_asset`'s if/else chain:

```cpp
    else
    {
        const std::optional<mosaic::engine::render::Image> img = read_ppm(*src);
        if (!img)
        {
            mosaic::foundation::log::error("import", "not a valid PPM (P6, maxval 255): {}", source_file);
            return std::nullopt;
        }
        blob_bytes = cook_image(*img);
        blob_ext   = ".image";
        asset_type = "texture";
    }
```

with:

```cpp
    else
    {
        ImportPluginHost import_plugins(mosaic::script::python::create_python_script_engine);
        for (const std::string& err : import_plugins.load((fs::path(project_dir) / "plugins").string()))
        {
            mosaic::foundation::log::warn("import", "plugin: {}", err);
        }
        const PluginImporter* importer = import_plugins.find_importer(ext);
        if (importer != nullptr)
        {
            const std::string plugin_asset_type = ext.substr(1); // drop leading '.'
            if (plugin_asset_type == "script" || plugin_asset_type == "audio" ||
                plugin_asset_type == "font" || plugin_asset_type == "texture")
            {
                mosaic::foundation::log::error(
                    "import", "extension '{}' would produce a reserved asset type '{}'; rename the plugin's registered extension",
                    ext, plugin_asset_type);
                return std::nullopt;
            }
            const ImportPluginHost::Result result = import_plugins.invoke(*importer, source_file);
            if (!result.blob.has_value())
            {
                mosaic::foundation::log::error("import", "{}: {}", importer->plugin_name, result.error);
                return std::nullopt;
            }
            blob_bytes = *result.blob;
            blob_ext   = ".data";
            asset_type = plugin_asset_type;
        }
        else
        {
            const std::optional<mosaic::engine::render::Image> img = read_ppm(*src);
            if (!img)
            {
                mosaic::foundation::log::error("import", "not a valid PPM (P6, maxval 255): {}", source_file);
                return std::nullopt;
            }
            blob_bytes = cook_image(*img);
            blob_ext   = ".image";
            asset_type = "texture";
        }
    }
```

Everything downstream of this if/else chain (stem/assets-dir resolution, the
existing stem-collision refusal, writing the blob then the `.toml`) is unchanged —
`ImportPluginHost` is only ever loaded when the extension isn't `.py`/`.wav`/`.ttf`/
`.otf`, so a `.wav`/`.py`/font import never pays the cost of scanning `plugins/`.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "import_asset*"
./build/debug/tests/mosaic_unit_tests "[app]"
```
Expected: all `import_asset*` cases pass (the 4 new ones plus every pre-existing one,
proving `.py`/`.wav`/`.ttf`/`.otf`/PPM-with-no-plugin-match are all unaffected);
`[app]` shows zero regressions elsewhere.

- [ ] **Step 5: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/asset_import_test.cpp
git commit -m "feat(app): consult plugin importers in import_asset before the PPM fallback"
```

---

### Task 3: `load_project_assets` — explicit `texture` branch + generic blob fallback

**Files:**
- Modify: `app/asset/asset_io.hpp`
- Test: `tests/unit/app/asset_import_test.cpp`

**Interfaces:**
- Consumes: nothing new — this task only changes `load_project_assets`'s internal
  dispatch on `meta.type`.
- Produces: nothing new for later tasks — Task 4 is docs/example only and doesn't
  call into this directly (it round-trips through Task 2's `import_asset` +
  `load_project_assets` together in its own manual verification).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/app/asset_import_test.cpp`:

```cpp
TEST_CASE("load_project_assets registers a generic (plugin-imported) asset as a raw string",
         "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("gproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    write_plugin(proj, "csv_importer.py",
        "MOSAIC_IMPORTERS = {\".csv\": \"import_csv\"}\n\n"
        "def import_csv(source_path):\n"
        "    with open(source_path) as f:\n"
        "        return f.read().upper()\n");

    const std::string src = tmp("gsrc") + ".csv";
    REQUIRE(save_scene_file(src, "a,b,c\n"));
    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());

    AssetRegistry registry;
    const int n = load_project_assets(registry, (fs::path(proj) / "assets").string());
    REQUIRE(n == 1);
    REQUIRE(registry.contains(*id));
    auto blob = registry.load<std::string>(*id);
    REQUIRE(blob != nullptr);
    REQUIRE(*blob == "A,B,C\n");

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("load_project_assets skips a generic asset with a missing .data blob", "[app]")
{
    AssetRegistry registry;
    const std::string dir = tmp("gdir");
    fs::remove_all(dir); fs::create_directories(dir);
    REQUIRE(save_scene_file((fs::path(dir) / "orphan2.toml").string(),
                            "uuid = \"00000000-0000-0000-0000-000000000002\"\ntype = \"csv\"\n"
                            "version = 1\nsource = \"x\"\n"));
    REQUIRE(load_project_assets(registry, dir) == 0);

    fs::remove_all(dir);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
```
Expected: build succeeds (these tests don't need new symbols), but running them
fails — `load_project_assets` still treats the `"csv"`-typed `.toml` as an image,
so `registry.load<std::string>(*id)` returns nullptr (or the count is 0 because the
implicit-image branch logs "corrupt cooked image" and skips it).

- [ ] **Step 3: Implement**

In `app/asset/asset_io.hpp`, replace `load_project_assets`'s final, unconditional
image-decoding block:

```cpp
        const fs::path blob_path = entry.path().parent_path() / (entry.path().stem().string() + ".image");
        const std::optional<std::string> blob = load_scene_file(blob_path.string());
        if (!blob) { mosaic::foundation::log::warn("import", "missing cooked image {}", blob_path.string()); continue; }
        const std::optional<mosaic::engine::render::Image> img = uncook_image(*blob);
        if (!img) { mosaic::foundation::log::warn("import", "corrupt cooked image {}", blob_path.string()); continue; }

        registry.register_resource<mosaic::engine::render::Image>(
            meta, std::make_shared<mosaic::engine::render::Image>(*img));
        ++count;
    }
    return count;
}
```

with an explicit `texture` branch plus a generic fallback for everything else:

```cpp
        if (meta.type == "texture")
        {
            const fs::path blob_path = entry.path().parent_path() / (entry.path().stem().string() + ".image");
            const std::optional<std::string> blob = load_scene_file(blob_path.string());
            if (!blob) { mosaic::foundation::log::warn("import", "missing cooked image {}", blob_path.string()); continue; }
            const std::optional<mosaic::engine::render::Image> img = uncook_image(*blob);
            if (!img) { mosaic::foundation::log::warn("import", "corrupt cooked image {}", blob_path.string()); continue; }

            registry.register_resource<mosaic::engine::render::Image>(
                meta, std::make_shared<mosaic::engine::render::Image>(*img));
            ++count;
            continue;
        }

        // Generic opaque blob -- covers every plugin-imported asset type (anything
        // not script/audio/font/texture). Only the plugin that produced it
        // understands the format; register the raw bytes as-is.
        const fs::path blob_path = entry.path().parent_path() / (entry.path().stem().string() + ".data");
        const std::optional<std::string> blob = load_scene_file(blob_path.string());
        if (!blob) { mosaic::foundation::log::warn("import", "missing generic asset blob {}", blob_path.string()); continue; }
        registry.register_resource<std::string>(meta, std::make_shared<std::string>(*blob));
        ++count;
    }
    return count;
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"
./build/debug/tests/mosaic_unit_tests "[app]"
```
Expected: all `[app]` cases pass, including the pre-existing
`"load_project_assets registers imported assets into the registry"` test (proves the
`texture` branch is still exercised correctly, not just theoretically present) and
both new cases from this task.

- [ ] **Step 5: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/asset_import_test.cpp
git commit -m "fix(app): load_project_assets no longer assumes non-built-in types are images"
```

---

### Task 4: Example plugin, docs, full-suite verification

**Files:**
- Create: `tools/example_plugins/csv_importer.py`
- Modify: `tools/example_plugins/README.md`
- Modify: `docs/design/scripting.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `MOSAIC_IMPORTERS` (Task 1/2's dict-registration convention) — this task
  is documentation and a worked example, no new code symbols.
- Produces: nothing consumed elsewhere in this plan — final task.

- [ ] **Step 1: Add the example plugin**

Create `tools/example_plugins/csv_importer.py`:

```python
import csv
import json

MOSAIC_IMPORTERS = {".csv": "import_csv"}


def import_csv(source_path):
    with open(source_path, newline="") as f:
        rows = list(csv.reader(f))
    return json.dumps(rows)
```

- [ ] **Step 2: Update the example plugins README**

In `tools/example_plugins/README.md`, change the intro paragraph's opening line and
the section reference from:

```markdown
Nine small, self-contained Python plugins for Mosaic Studio's Plugin SDK (Phase 5,
slices 1-2 — discovery/lifecycle + custom menu items + custom Studio panels; see
```

to:

```markdown
Ten small, self-contained Python plugins for Mosaic Studio's Plugin SDK (Phase 5,
slices 1-3 — discovery/lifecycle + custom menu items + custom Studio panels + custom
asset importers; see
```

Add this row to the table, right after the `world_inspector_panel.py` row:

```markdown
| `csv_importer.py` | (registers an importer, not a menu item/panel) `.csv` | `MOSAIC_IMPORTERS` -- converts a CSV file into JSON text on import (`mosaic import <project> data.csv`); the CLI-only counterpart to `MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`, see `docs/design/scripting.md` |
```

- [ ] **Step 3: Document the importer surface**

In `docs/design/scripting.md`, find the existing "Custom Studio panels (slice 2)"
section and add a new subsection right after its closing paragraph (the one ending
"...would corrupt ImGui's internal state.") and before the "Where the plugins
directory lives" section:

```markdown
### Custom asset importers (slice 3)

A plugin can also declare `MOSAIC_IMPORTERS`, the import-registration counterpart to
`MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS` -- keyed by file extension instead of a UI label,
since this one has no UI surface (see below):

```python
import csv
import json

MOSAIC_IMPORTERS = {".csv": "import_csv"}

def import_csv(source_path):
    with open(source_path, newline="") as f:
        rows = list(csv.reader(f))
    return json.dumps(rows)
```

`mosaic import <project> <file>` -- the CLI import command, not the editor -- consults
every plugin's `MOSAIC_IMPORTERS` for the source file's extension whenever that
extension isn't one of the four built-in kinds (`.py`/`.wav`/`.ttf`/`.otf`, plus the
PPM image fallback). The matched function receives the source file's path as its one
argument and does its own file I/O (`open()`, and any stdlib module it imports, are
both available -- an importer plugin is regular Python, same as any other plugin
file); its **return value** (a string) becomes the asset's cooked blob, written to
`assets/<stem>.data` alongside the usual `assets/<stem>.toml` metadata.

This is register-only in v1: an imported asset is loaded back as a raw string via
`AssetRegistry::load<std::string>(id)`, with no gameplay-script getter yet -- add one
against real demand, the same "grow later" call slice 2 made for the `imgui_*`
widget set. There's also no Studio UI trigger: Studio's Assets panel stays
read-only, and importing only happens through the `mosaic import` CLI command -- a
project's `plugins/` directory here resolves as `<project_dir>/plugins` (the CLI
already takes an explicit project directory, unlike the editor's CWD-relative gap
documented below).

This needed a new app-tier plugin loader, `ImportPluginHost`
(`app/asset/import_plugins.hpp`/`.cpp`), structurally parallel to `studio::PluginHost`
but never calling `bind_world`/`bind_commands` -- `app` and `studio` are sibling
tiers (neither depends on the other), so this slice could not reuse `PluginHost`
directly. No `ScriptEngine` interface changes were needed either: an importer's
return value is captured by composing `set_global` + `run_string` + `get_global`,
all of which the interface already exposed.
```

In `docs/project/next-steps.md`, find the "Last updated" line at the top of the file
and prepend a new clause (following the same pattern used for the slice-2 entry)
describing this slice. Locate the existing text:

```
**Last updated:** 2026-09-14 (**Plugin SDK slice 2 — custom Studio panels (`MOSAIC_PANELS`)**
```

and prepend a new dated clause immediately before it (keeping the ` ¶ (` separator
convention the file already uses between entries):

```
**Last updated:** 2026-09-14 (**Plugin SDK slice 3 — custom asset importers (`MOSAIC_IMPORTERS`)** — a plugin can now declare `MOSAIC_IMPORTERS = {".ext": "import_fn"}`; `mosaic import <project> <file>` consults every loaded plugin's dict whenever the source extension isn't one of the four built-in kinds (script/audio/font/PPM-image fallback), invokes the matched one-argument function (receiving the source path, doing its own file I/O), and writes its string return value as the asset's cooked blob (`assets/<stem>.data`) -- register-only for now, loaded back via `AssetRegistry::load<std::string>(id)`, no new gameplay-script getter. New app-tier `ImportPluginHost` (`app/asset/import_plugins.hpp`/`.cpp`) mirrors `studio::PluginHost`'s discovery/all-or-nothing-load semantics but never binds a World (app and studio are sibling tiers, per `docs/architecture/boundaries.md` -- this slice couldn't reuse `PluginHost` directly). No `ScriptEngine` interface changes were needed: the return-value capture composes `set_global`/`run_string`/`get_global`, all pre-existing. `load_project_assets`'s previously-implicit "anything not script/audio/font is an image" assumption is now an explicit `texture` check plus a generic opaque-blob fallback, fixing what would otherwise have been a silent mis-decode for any plugin-imported type. A reserved-type collision guard refuses an importer extension that would produce one of the four built-in type names. Full design: `docs/superpowers/specs/2026-09-14-plugin-sdk-custom-importers-design.md`. Phase 5's three planned Plugin SDK extension points (menu items, panels, importers) are now all shipped; native `.so`/`.dll` plugins remain a much larger, still-later sub-project.) ¶ (**Plugin SDK slice 2 — custom Studio panels (`MOSAIC_PANELS`)**
```

(This is a prepend: the existing rest of that sentence and every later `¶`-separated
entry stays exactly as it already is, just pushed after the new clause.)

- [ ] **Step 4: Build and run the full suite**

```bash
cmake --build build/debug --target mosaic mosaic_unit_tests -j"$(nproc)"
ctest --test-dir build/debug --output-on-failure
```
Expected: clean build; test failures limited to the pre-existing, unrelated
`--host`/`--connect` CLI Catch2 test-name-leading-dash discovery issue (6 cases) and,
if it reproduces this run, the pre-existing flaky `threaded ScriptSystem` SIGSEGV --
both documented in project memory as predating this work.

- [ ] **Step 5: Manually verify the CLI end to end**

From the repo root, in a scratch project:

```bash
./build/debug/app/mosaic create /tmp/mosaic_importer_check
printf 'name,age\nAda,36\nGrace,85\n' > /tmp/mosaic_importer_check/people.csv
mkdir -p /tmp/mosaic_importer_check/plugins
cp tools/example_plugins/csv_importer.py /tmp/mosaic_importer_check/plugins/
./build/debug/app/mosaic import /tmp/mosaic_importer_check /tmp/mosaic_importer_check/people.csv
cat /tmp/mosaic_importer_check/assets/people.data
cat /tmp/mosaic_importer_check/assets/people.toml
```
Expected: `people.data` contains
`[["name", "age"], ["Ada", "36"], ["Grace", "85"]]` (JSON), and `people.toml` shows
`type = "csv"`.

- [ ] **Step 6: Commit**

```bash
git add tools/example_plugins/csv_importer.py tools/example_plugins/README.md \
        docs/design/scripting.md docs/project/next-steps.md
git commit -m "docs(scripting): document custom asset importers (Plugin SDK slice 3)"
```
