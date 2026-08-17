# `mosaic run` App / CLI Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `mosaic run` process entry point that drives a `World` in real time via `RuntimeLoop`, with a testable frame pump, CLI11 argument parsing, JSON status output, and optional scene loading (issue [#35](https://github.com/Evol-Luci/mosaic/issues/35)).

**Architecture:** A new top-level `app/` tier with two CMake targets — a `mosaic_app` static library holding all logic (CLI parsing, frame pump, status reporting) behind test seams, and a thin `mosaic` executable (`main.cpp`) that wires the real `std::chrono` clock, SIGINT handler, and stdout. The frame pump takes injected clock/sleep/stop dependencies so tests run in zero real time.

**Tech Stack:** C++20, CLI11 (header-only, FetchContent), nlohmann/json (already vendored), Catch2 v3.

## Global Constraints

- **PREFLIGHT — rebase first:** This branch is off `main`, which does **not** yet contain logging (#33, [PR #39](https://github.com/Evol-Luci/mosaic/pull/39)). `app/run/status_reporter.cpp` and `app/main.cpp` `#include "foundation/logging/log.hpp"`. **Before implementing, rebase this branch onto a `main` that includes the merged #39**, or the build will fail to find the logging header. Verify with: `test -f foundation/logging/log.hpp && echo OK`.
- **New `app/` tier:** may depend on `engine/`, `foundation/`, and app-only third-party libs (CLI11, nlohmann). Nothing in `engine/`/`foundation/` may depend on `app/`.
- **Namespaces:** app code in `mosaic::app`; the one engine addition in `mosaic::engine::reflection`.
- **CLI11 is confined to `app/cli/options.cpp`** — parsing yields a plain `Options` struct; no downstream unit sees CLI11.
- **Frame-pump time seam is plain `double` seconds** (not `std::chrono`) so tests need no clock library; `main` adapts `steady_clock` into it.
- **No real sleeps in tests** — the injected `sleep` is a no-op/counting stub.
- **Test tags:** `[cli]` for parsing, `[app]` for pump/reporter, `[reflection]` for the component helper.
- **C++ standard:** `cxx_std_20`, no extensions.
- **Exit codes:** `main` returns 0 on clean stop (signal/duration/ticks); non-zero on arg error, unreadable scene, or `SerializationError`.
- **`--json` output:** periodic `{"event":"progress",...}` lines on a fixed cadence + one final `{"event":"summary",...}` line to stdout; an `{"event":"error",...}` line on failure.

**Exact engine interfaces this plan calls (verified against the tree):**
- `mosaic::engine::World` — `template<class S,class...A> S& add_system(A&&...)`, `void tick(float)`.
- `mosaic::engine::runtime::RuntimeLoop` — `explicit RuntimeLoop(float fixed_dt, int max_steps_per_advance = 8)`, `int advance(World&, float real_dt)`, `void start()`, `void stop()`, `std::uint64_t tick_count() const`, `float fixed_dt() const`.
- `mosaic::engine::transform::TransformSystem` — default-constructible `System`.
- `mosaic::engine::transform::Transform` — reflected component struct.
- `mosaic::engine::reflection::ComponentReflection` — `template<class T> void register_component(mosaic::foundation::reflection::TypeRegistry&, std::string name)`, `bool has(std::string_view) const`.
- `mosaic::foundation::reflection::TypeRegistry` — default-constructible.
- `mosaic::engine::scene::load_from_json(const std::string& json, World&, const ComponentReflection&)` — throws `mosaic::foundation::serialization::SerializationError` on bad JSON / unknown type.
- `mosaic::foundation::log::info/warn/error(std::string_view category, fmt, args...)` (from #33).

**Build & test commands (used throughout):**
- Configure (first time): `cmake --preset debug`
- Build: `cmake --build --preset debug`
- Run app/cli tests: `./build/debug/tests/mosaic_unit_tests "[cli],[app]"`
- Run the component-helper test: `./build/debug/tests/mosaic_unit_tests "[reflection]"`
- Full suite: `ctest --preset debug`
- The built executable: `./build/debug/app/mosaic`

---

### Task 1: App scaffolding + CLI options parsing

Stand up the `app/` tier (CMake, CLI11, `mosaic_app` library, test wiring) and the argument parser. No `mosaic` executable yet — that arrives in Task 5 with `main.cpp`.

**Files:**
- Modify: `CMakeLists.txt` (FetchContent CLI11; `add_subdirectory(app)`)
- Create: `app/CMakeLists.txt`
- Create: `app/cli/options.hpp`
- Create: `app/cli/options.cpp`
- Modify: `tests/CMakeLists.txt` (link `mosaic_app`; add `unit/app/options_test.cpp`)
- Create: `tests/unit/app/options_test.cpp`

**Interfaces:**
- Consumes: nothing (first task).
- Produces:
  - `struct mosaic::app::Options { std::optional<std::string> scene; std::optional<double> duration_s; std::optional<std::uint64_t> ticks; bool json = false; double fps = 60.0; double fixed_hz = 60.0; };`
  - `struct mosaic::app::ParseResult { Options options; std::optional<int> exit_code; };`
  - `ParseResult mosaic::app::parse(int argc, char** argv);` — sets `exit_code` when CLI11 handled `--help`/error (caller returns it); otherwise `options` is populated from the `run` subcommand.

- [ ] **Step 1: Add CLI11 to the root CMake and register the app subdirectory**

In `CMakeLists.txt`, add a CLI11 `FetchContent_Declare` next to the others and include it in the existing `FetchContent_MakeAvailable`, then add the subdirectory. Concretely, after the `nlohmann_json` declare block and before `FetchContent_MakeAvailable(tomlplusplus nlohmann_json)`:

```cmake
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
)
```

Change the existing line:

```cmake
FetchContent_MakeAvailable(tomlplusplus nlohmann_json)
```

to:

```cmake
FetchContent_MakeAvailable(tomlplusplus nlohmann_json CLI11)
```

And add `add_subdirectory(app)` immediately after `add_subdirectory(engine)`:

```cmake
add_subdirectory(foundation)
add_subdirectory(engine)
add_subdirectory(app)
add_subdirectory(tests)
```

- [ ] **Step 2: Create the app CMake target**

Create `app/CMakeLists.txt` (the executable is added in Task 5):

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
)
target_link_libraries(mosaic_app PUBLIC
  mosaic_engine
  mosaic_foundation
  mosaic_serialization
  CLI11::CLI11
  nlohmann_json::nlohmann_json
)
target_include_directories(mosaic_app PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(mosaic_app PUBLIC cxx_std_20)
```

- [ ] **Step 3: Write the failing test**

Create `tests/unit/app/options_test.cpp`:

```cpp
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"

using mosaic::app::Options;
using mosaic::app::parse;

namespace
{
// Builds an argv from a vector of args (argv[0] is the program name) and calls
// parse. The char pointers are valid for the duration of the parse call.
mosaic::app::ParseResult parse_args(std::vector<std::string> args)
{
    std::vector<char*> argv;
    for (auto& a : args)
    {
        argv.push_back(a.data());
    }
    return parse(static_cast<int>(argv.size()), argv.data());
}
} // namespace

TEST_CASE("run subcommand with no flags yields defaults", "[cli]")
{
    auto result = parse_args({"mosaic", "run"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE_FALSE(result.options.scene.has_value());
    REQUIRE_FALSE(result.options.duration_s.has_value());
    REQUIRE_FALSE(result.options.ticks.has_value());
    REQUIRE(result.options.json == false);
    REQUIRE(result.options.fps == 60.0);
    REQUIRE(result.options.fixed_hz == 60.0);
}

TEST_CASE("run flags populate the Options struct", "[cli]")
{
    auto result = parse_args(
        {"mosaic", "run", "--scene", "demo.json", "--duration", "2.5",
         "--ticks", "120", "--fps", "30", "--fixed-hz", "50", "--json"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.scene == "demo.json");
    REQUIRE(result.options.duration_s == 2.5);
    REQUIRE(result.options.ticks == std::uint64_t{120});
    REQUIRE(result.options.fps == 30.0);
    REQUIRE(result.options.fixed_hz == 50.0);
    REQUIRE(result.options.json == true);
}

TEST_CASE("an unknown flag sets a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic", "run", "--bogus"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}

TEST_CASE("missing subcommand sets a non-zero exit code", "[cli]")
{
    auto result = parse_args({"mosaic"});
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code != 0);
}
```

- [ ] **Step 4: Wire the test into CMake**

In `tests/CMakeLists.txt`, add `mosaic_app` to the `target_link_libraries(mosaic_unit_tests PRIVATE ...)` list (before `Catch2::Catch2WithMain`), and add the source to the `add_executable(mosaic_unit_tests ...)` list:

```cmake
  unit/app/options_test.cpp
```

- [ ] **Step 5: Run the test to verify it fails**

Run: `cmake --preset debug && cmake --build --preset debug`
Expected: FAIL — `app/cli/options.hpp` not found (header does not exist yet).

- [ ] **Step 6: Write the Options header**

Create `app/cli/options.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mosaic::app
{

// Parsed `mosaic run` arguments. A plain value type: no CLI11 in this header.
struct Options
{
    std::optional<std::string>   scene;        // --scene <file>
    std::optional<double>        duration_s;   // --duration <seconds>
    std::optional<std::uint64_t> ticks;        // --ticks <n>
    bool                         json = false; // --json
    double                       fps = 60.0;      // --fps  (pump wake target)
    double                       fixed_hz = 60.0; // --fixed-hz (sim timestep)
};

// Result of parse(): either a populated Options, or an exit_code to return from
// main (set when CLI11 handled --help or reported an arg error).
struct ParseResult
{
    Options              options;
    std::optional<int>   exit_code;
};

// Parse argv for the `mosaic run [flags]` command line using CLI11.
ParseResult parse(int argc, char** argv);

} // namespace mosaic::app
```

- [ ] **Step 7: Write the Options implementation**

Create `app/cli/options.cpp`:

```cpp
#include "app/cli/options.hpp"

#include <CLI/CLI.hpp>

namespace mosaic::app
{

ParseResult parse(int argc, char** argv)
{
    ParseResult result;

    CLI::App app{"Mosaic engine runner"};
    app.require_subcommand(1); // exactly one subcommand (only `run` for now)

    CLI::App* run = app.add_subcommand("run", "Run a world in real time");

    std::string  scene;
    double       duration_s = 0.0;
    std::uint64_t ticks = 0;
    run->add_option("--scene", scene, "Scene JSON file to load before running");
    CLI::Option* duration_opt =
        run->add_option("--duration", duration_s, "Stop after N seconds");
    CLI::Option* ticks_opt =
        run->add_option("--ticks", ticks, "Stop after N fixed ticks");
    run->add_flag("--json", result.options.json, "Emit machine-readable status");
    run->add_option("--fps", result.options.fps, "Target wake frame rate")
        ->capture_default_str();
    run->add_option("--fixed-hz", result.options.fixed_hz, "Simulation timestep rate")
        ->capture_default_str();

    try
    {
        app.parse(argc, argv);
    }
    catch (const CLI::ParseError& e)
    {
        result.exit_code = app.exit(e); // prints help/error, yields exit code
        return result;
    }

    if (!scene.empty())
    {
        result.options.scene = scene;
    }
    if (*duration_opt)
    {
        result.options.duration_s = duration_s;
    }
    if (*ticks_opt)
    {
        result.options.ticks = ticks;
    }
    return result;
}

} // namespace mosaic::app
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: PASS — all 4 `[cli]` cases green.

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt app/CMakeLists.txt app/cli/options.hpp app/cli/options.cpp tests/CMakeLists.txt tests/unit/app/options_test.cpp
git commit -m "app: CLI11 argument parsing + app/ scaffolding (#35)"
```

---

### Task 2: `register_builtin_components` engine helper

A shared engine-tier helper that registers the engine's built-in components by name, so the app and the future editor register an identical set.

**Files:**
- Create: `engine/reflection/builtin_components.hpp`
- Create: `tests/unit/reflection/builtin_components_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `mosaic::engine::reflection::ComponentReflection`, `mosaic::foundation::reflection::TypeRegistry`, `mosaic::engine::transform::Transform`.
- Produces: `void mosaic::engine::reflection::register_builtin_components(mosaic::foundation::reflection::TypeRegistry&, ComponentReflection&);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/reflection/builtin_components_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::engine::World;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

TEST_CASE("register_builtin_components registers Transform by name", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    REQUIRE(types.has("Transform"));
    REQUIRE_FALSE(types.has("NotAComponent"));
}

TEST_CASE("a Transform scene round-trips through the builtin registration", "[reflection]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    // A minimal scene: one entity carrying a default Transform component.
    const std::string json =
        R"({"entities":[{"id":"1","components":{"Transform":{)"
        R"("position":{"x":1.0,"y":2.0,"z":3.0},)"
        R"("rotation":{"x":0.0,"y":0.0,"z":0.0,"w":1.0},)"
        R"("scale":{"x":1.0,"y":1.0,"z":1.0},)"
        R"("parent":"0"}}}]})";

    World world;
    REQUIRE_NOTHROW(mosaic::engine::scene::load_from_json(json, world, types));

    int transform_count = 0;
    world.for_each<mosaic::engine::transform::Transform>(
        [&](World::EntityID, mosaic::engine::transform::Transform&) { ++transform_count; });
    REQUIRE(transform_count == 1);
}
```

> Note: the exact JSON shape mirrors what `Scene::save_to_json` emits for a
> `Transform` (reflected fields `position`/`rotation`/`scale`/`parent`). If the
> reflected field names differ, generate the canonical form first by saving a
> world containing one default `Transform` and copy the emitted JSON — do not
> guess. Adjust the literal to match before running Step 3.

- [ ] **Step 2: Wire the test into CMake**

In `tests/CMakeLists.txt`, add to the `add_executable(mosaic_unit_tests ...)` list:

```cmake
  unit/reflection/builtin_components_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `engine/reflection/builtin_components.hpp` not found.

- [ ] **Step 4: Write the header**

Create `engine/reflection/builtin_components.hpp`:

```cpp
#pragma once

#include <string>

#include "engine/reflection/component_reflection.hpp"
#include "engine/transform/transform.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::engine::reflection
{

// Registers every built-in engine component (by stable name) into `types`,
// recording type metadata in `registry`. The app and the future editor call
// this so they share one component set and cannot drift. The caller owns both
// `registry` and `types` and keeps them alive for as long as `types` is used.
inline void register_builtin_components(
    mosaic::foundation::reflection::TypeRegistry& registry, ComponentReflection& types)
{
    types.register_component<mosaic::engine::transform::Transform>(registry, "Transform");
    // future: Camera, Mesh, ...
}

} // namespace mosaic::engine::reflection
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[reflection]"`
Expected: PASS — the two new cases plus the existing `[reflection]` cases green.

- [ ] **Step 6: Commit**

```bash
git add engine/reflection/builtin_components.hpp tests/unit/reflection/builtin_components_test.cpp tests/CMakeLists.txt
git commit -m "engine: register_builtin_components helper (Transform) (#35)"
```

---

### Task 3: Status reporter (RunSummary + interface + JSON reporter)

The status seam the frame pump emits through. An interface plus a JSON reporter (stdout lines + human-readable logging) and a text reporter (logging only).

**Files:**
- Create: `app/run/status_reporter.hpp`
- Create: `app/run/status_reporter.cpp`
- Modify: `app/CMakeLists.txt` (add `run/status_reporter.cpp` to `mosaic_app`)
- Create: `tests/unit/app/status_reporter_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `mosaic::foundation::log` (from #33).
- Produces:
  - `struct mosaic::app::RunSummary { std::uint64_t ticks; double elapsed_s; double fixed_dt; std::string scene; std::string stopped_by; };`
  - `class mosaic::app::StatusReporter { virtual void progress(std::uint64_t tick, double elapsed_s) = 0; virtual void summary(const RunSummary&) = 0; virtual void error(const std::string&) = 0; virtual ~StatusReporter() = default; };`
  - `class mosaic::app::JsonStatusReporter : public StatusReporter;` (ctor takes `std::ostream&`, default `std::cout`)
  - `class mosaic::app::TextStatusReporter : public StatusReporter;`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/app/status_reporter_test.cpp`:

```cpp
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "app/run/status_reporter.hpp"

using mosaic::app::JsonStatusReporter;
using mosaic::app::RunSummary;

TEST_CASE("JsonStatusReporter emits one progress JSON object per line", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.progress(300, 5.0);

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "progress");
    REQUIRE(parsed.at("tick") == 300);
    REQUIRE(parsed.at("elapsed_s") == 5.0);
}

TEST_CASE("JsonStatusReporter emits a summary object with all fields", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.summary(RunSummary{600, 10.0, 1.0 / 60.0, "demo.json", "ticks"});

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "summary");
    REQUIRE(parsed.at("ticks") == 600);
    REQUIRE(parsed.at("elapsed_s") == 10.0);
    REQUIRE(parsed.at("scene") == "demo.json");
    REQUIRE(parsed.at("stopped_by") == "ticks");
}

TEST_CASE("JsonStatusReporter emits an error object", "[app]")
{
    std::ostringstream out;
    JsonStatusReporter reporter(out);

    reporter.error("scene not found: demo.json");

    auto parsed = nlohmann::json::parse(out.str());
    REQUIRE(parsed.at("event") == "error");
    REQUIRE(parsed.at("message") == "scene not found: demo.json");
}
```

- [ ] **Step 2: Wire the test + source into CMake**

In `app/CMakeLists.txt`, add `run/status_reporter.cpp` to the `mosaic_app` source list. In `tests/CMakeLists.txt`, add to the `mosaic_unit_tests` sources:

```cmake
  unit/app/status_reporter_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `app/run/status_reporter.hpp` not found.

- [ ] **Step 4: Write the header**

Create `app/run/status_reporter.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace mosaic::app
{

// Outcome of a run, reported once at the end.
struct RunSummary
{
    std::uint64_t ticks;      // total fixed ticks executed
    double        elapsed_s;  // wall-clock (or scripted) seconds elapsed
    double        fixed_dt;   // the RuntimeLoop fixed timestep
    std::string   scene;      // scene file loaded, or "" if none
    std::string   stopped_by; // "signal" | "duration" | "ticks"
};

// Sink for run status. The frame pump calls progress() periodically and
// summary() once; main calls error() on a startup failure.
class StatusReporter
{
public:
    virtual ~StatusReporter() = default;
    virtual void progress(std::uint64_t tick, double elapsed_s) = 0;
    virtual void summary(const RunSummary&) = 0;
    virtual void error(const std::string& message) = 0;
};

// Writes one JSON object per line to the given stream (default std::cout) and
// mirrors a human-readable line through the logging facility.
class JsonStatusReporter : public StatusReporter
{
public:
    explicit JsonStatusReporter(std::ostream& out);
    JsonStatusReporter();
    void progress(std::uint64_t tick, double elapsed_s) override;
    void summary(const RunSummary&) override;
    void error(const std::string& message) override;

private:
    std::ostream& out_;
};

// Human-readable only: routes everything through the logging facility, no JSON.
class TextStatusReporter : public StatusReporter
{
public:
    void progress(std::uint64_t tick, double elapsed_s) override;
    void summary(const RunSummary&) override;
    void error(const std::string& message) override;
};

} // namespace mosaic::app
```

- [ ] **Step 5: Write the implementation**

Create `app/run/status_reporter.cpp`:

```cpp
#include "app/run/status_reporter.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "foundation/logging/log.hpp"

namespace mosaic::app
{
namespace log = mosaic::foundation::log;

JsonStatusReporter::JsonStatusReporter(std::ostream& out) : out_(out) {}
JsonStatusReporter::JsonStatusReporter() : out_(std::cout) {}

void JsonStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    out_ << nlohmann::json{{"event", "progress"}, {"tick", tick},
                           {"elapsed_s", elapsed_s}}
                .dump()
         << '\n';
    log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void JsonStatusReporter::summary(const RunSummary& s)
{
    out_ << nlohmann::json{{"event", "summary"}, {"ticks", s.ticks},
                           {"elapsed_s", s.elapsed_s}, {"fixed_dt", s.fixed_dt},
                           {"scene", s.scene}, {"stopped_by", s.stopped_by}}
                .dump()
         << '\n';
    log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void JsonStatusReporter::error(const std::string& message)
{
    out_ << nlohmann::json{{"event", "error"}, {"message", message}}.dump() << '\n';
    log::error("run", "{}", message);
}

void TextStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void TextStatusReporter::summary(const RunSummary& s)
{
    log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void TextStatusReporter::error(const std::string& message)
{
    log::error("run", "{}", message);
}

} // namespace mosaic::app
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS — the 3 reporter cases green (logging output is a side effect, not asserted).

- [ ] **Step 7: Commit**

```bash
git add app/run/status_reporter.hpp app/run/status_reporter.cpp app/CMakeLists.txt tests/CMakeLists.txt tests/unit/app/status_reporter_test.cpp
git commit -m "app: StatusReporter interface + JSON/text reporters (#35)"
```

---

### Task 4: FramePump (the testable real-time loop)

The heart of the shell: measure elapsed time, drive `RuntimeLoop::advance`, emit periodic progress, sleep to a target frame rate, and stop on signal/duration/ticks — all through injected dependencies so tests run with a fake clock and no real sleeps.

**Files:**
- Create: `app/run/frame_pump.hpp` (header-only)
- Create: `tests/unit/app/frame_pump_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `mosaic::app::Options` (Task 1), `mosaic::app::StatusReporter` + `RunSummary` (Task 3), `mosaic::engine::World`, `mosaic::engine::runtime::RuntimeLoop`.
- Produces:
  - `struct mosaic::app::PumpDeps { std::function<double()> now; std::function<void(double)> sleep; std::function<bool()> stop_requested; };`
  - `class mosaic::app::FramePump { FramePump(engine::runtime::RuntimeLoop&, PumpDeps, StatusReporter&, Options); RunSummary run(engine::World&); };`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/app/frame_pump_test.cpp`:

```cpp
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

using mosaic::app::FramePump;
using mosaic::app::Options;
using mosaic::app::PumpDeps;
using mosaic::app::RunSummary;
using mosaic::app::StatusReporter;
using mosaic::engine::World;
using mosaic::engine::runtime::RuntimeLoop;

namespace
{
// Records reporter calls for assertions; no logging, no stdout.
class CapturingReporter : public StatusReporter
{
public:
    std::vector<std::uint64_t> progress_ticks;
    std::optional<RunSummary>  final_summary;
    void progress(std::uint64_t tick, double) override { progress_ticks.push_back(tick); }
    void summary(const RunSummary& s) override { final_summary = s; }
    void error(const std::string&) override {}
};

// A monotonic clock that advances a fixed step per call and never stalls (so a
// long run never sees real_dt == 0). Each call returns the current time, then
// advances. step >= fixed_dt guarantees advance() banks at least one tick.
struct StepClock
{
    double t = 0.0;
    double step;
    double operator()()
    {
        double now = t;
        t += step;
        return now;
    }
};
} // namespace

TEST_CASE("--ticks bounds the run to exactly N ticks", "[app]")
{
    World world;
    // max_steps_per_advance = 1: each advance runs at most one fixed tick, so a
    // large real_dt cannot overshoot the ticks bound. The count is exact.
    RuntimeLoop loop(1.0F / 60.0F, 1);

    Options options;
    options.ticks = 5;
    options.fixed_hz = 60.0;

    StepClock clock{0.0, 1.0}; // 1s per call: always >= fixed_dt, so 1 tick/iter
    int sleep_calls = 0;
    PumpDeps deps{std::ref(clock), [&](double) { ++sleep_calls; }, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(loop.tick_count() == 5);
    REQUIRE(summary.ticks == 5);
    REQUIRE(summary.stopped_by == "ticks");
}

TEST_CASE("--duration stops once elapsed crosses the bound", "[app]")
{
    World world;
    RuntimeLoop loop(1.0F / 60.0F);

    Options options;
    options.duration_s = 2.0;

    StepClock clock{0.0, 1.0}; // elapsed crosses 2.0 within a few iterations
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "duration");
    REQUIRE(summary.elapsed_s >= 2.0);
}

TEST_CASE("a stop request ends the run with stopped_by == signal", "[app]")
{
    World world;
    RuntimeLoop loop(1.0F / 60.0F);
    Options options; // no bounds

    StepClock clock{0.0, 0.1};
    bool stop = true; // already requested: the top-of-loop check fires first
    PumpDeps deps{std::ref(clock), [](double) {}, [&] { return stop; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "signal");
    REQUIRE(loop.tick_count() == 0); // stop checked before the first advance
}

TEST_CASE("progress fires on the sim-second cadence", "[app]")
{
    World world;
    // One tick per advance so the tick_count-driven cadence is exact: progress
    // fires at ticks 60, 120, 180 (1s, 2s, 3s at 60 Hz).
    RuntimeLoop loop(1.0F / 60.0F, 1);

    Options options;
    options.ticks = 180;
    options.fixed_hz = 60.0;

    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};

    CapturingReporter reporter;
    FramePump pump(loop, deps, reporter, options);
    pump.run(world);

    REQUIRE(reporter.progress_ticks.size() == 3);
    REQUIRE(reporter.progress_ticks.front() == 60);
    REQUIRE(reporter.progress_ticks.back() == 180);
}
```

- [ ] **Step 2: Wire the test into CMake**

In `tests/CMakeLists.txt`, add to the `mosaic_unit_tests` sources:

```cmake
  unit/app/frame_pump_test.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build --preset debug`
Expected: FAIL — `app/run/frame_pump.hpp` not found.

- [ ] **Step 4: Write the header**

Create `app/run/frame_pump.hpp`:

```cpp
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>

#include "app/cli/options.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/world/world.hpp"

namespace mosaic::app
{

// Injected time/stop dependencies. Seconds are plain doubles so tests need no
// chrono. `now` is a monotonic clock; `sleep` sleeps that many seconds (a no-op
// in tests); `stop_requested` reports an external stop (SIGINT) — false headless.
struct PumpDeps
{
    std::function<double()>     now;
    std::function<void(double)> sleep;
    std::function<bool()>       stop_requested;
};

// Drives a World in real time over a RuntimeLoop until a stop source fires.
// Owns pacing and status emission; the per-tick order stays in World::tick.
class FramePump
{
public:
    FramePump(engine::runtime::RuntimeLoop& loop, PumpDeps deps,
              StatusReporter& reporter, Options options)
        : loop_(loop), deps_(std::move(deps)), reporter_(reporter),
          options_(std::move(options))
    {
    }

    RunSummary run(engine::World& world)
    {
        const double target_frame_period = options_.fps > 0.0 ? 1.0 / options_.fps : 0.0;
        const double progress_interval = 1.0; // sim seconds between progress lines

        loop_.start();
        const double t0 = deps_.now();
        double       t_prev = t0;
        double       elapsed = 0.0;
        std::uint64_t next_progress_tick =
            static_cast<std::uint64_t>(progress_interval * options_.fixed_hz);
        std::string stopped_by;

        while (true)
        {
            // An external stop (SIGINT) ends the run before doing more work, so a
            // stop requested before the first advance yields zero ticks.
            if (deps_.stop_requested())
            {
                stopped_by = "signal";
                break;
            }

            const double t = deps_.now();
            elapsed = t - t0;
            double real_dt = t - t_prev;
            if (real_dt < 0.0)
            {
                real_dt = 0.0;
            }
            t_prev = t;

            loop_.advance(world, static_cast<float>(real_dt));

            // Periodic progress, keyed on tick count (deterministic).
            while (loop_.tick_count() >= next_progress_tick)
            {
                reporter_.progress(loop_.tick_count(), elapsed);
                next_progress_tick +=
                    static_cast<std::uint64_t>(progress_interval * options_.fixed_hz);
            }

            // Tick / duration bounds are evaluated after advancing.
            if (options_.ticks && loop_.tick_count() >= *options_.ticks)
            {
                stopped_by = "ticks";
                break;
            }
            if (options_.duration_s && elapsed >= *options_.duration_s)
            {
                stopped_by = "duration";
                break;
            }

            // Pace to the target frame rate: sleep the remainder of the budget.
            const double spent = deps_.now() - t;
            const double remaining = target_frame_period - spent;
            if (remaining > 0.0)
            {
                deps_.sleep(remaining);
            }
        }

        loop_.stop();
        return RunSummary{loop_.tick_count(), elapsed,
                          static_cast<double>(loop_.fixed_dt()),
                          options_.scene.value_or(std::string{}), stopped_by};
    }

private:
    engine::runtime::RuntimeLoop& loop_;
    PumpDeps                      deps_;
    StatusReporter&               reporter_;
    Options                       options_;
};

} // namespace mosaic::app
```

> Note on determinism in tests: `stop_requested` is checked at the *top* of the
> loop (before `advance`), so a pre-set stop yields `tick_count() == 0`. The
> `ticks`/`duration` bounds are checked after `advance`. The exact-count tests
> construct `RuntimeLoop(fixed_dt, 1)` so each `advance` runs at most one fixed
> tick — a single `advance` would otherwise run up to `max_steps_per_advance`
> (default 8) ticks and overshoot a bound. The `StepClock` advances a fixed step
> (≥ `fixed_dt`) every call and never stalls, so every iteration banks exactly
> one tick.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset debug && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS — the 4 pump cases plus the 3 reporter cases green.

- [ ] **Step 6: Commit**

```bash
git add app/run/frame_pump.hpp tests/CMakeLists.txt tests/unit/app/frame_pump_test.cpp
git commit -m "app: FramePump real-time loop with injected clock/sleep/stop (#35)"
```

---

### Task 5: `main.cpp` + `mosaic` executable

Wire everything into a runnable process: parse, build the world, load a scene, install a SIGINT handler, run the pump with a real `steady_clock`, print the summary, and return an exit code.

**Files:**
- Create: `app/main.cpp`
- Modify: `app/CMakeLists.txt` (add the `mosaic` executable)

**Interfaces:**
- Consumes: everything from Tasks 1–4, plus `mosaic::engine::transform::TransformSystem`, `mosaic::engine::reflection::register_builtin_components`, `mosaic::engine::scene::load_from_json`, `mosaic::foundation::reflection::TypeRegistry`.
- Produces: the `mosaic` executable. Not unit-tested (thin wiring); verified by a manual smoke run.

- [ ] **Step 1: Write main.cpp**

Create `app/main.cpp`:

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "app/cli/options.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/value.hpp" // SerializationError

namespace
{
std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }
} // namespace

int main(int argc, char** argv)
{
    using namespace mosaic::app;
    namespace engine = mosaic::engine;

    ParseResult pr = parse(argc, argv);
    if (pr.exit_code)
    {
        return *pr.exit_code;
    }
    const Options& options = pr.options;

    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    engine::World world;
    world.add_system<engine::transform::TransformSystem>();

    mosaic::foundation::reflection::TypeRegistry registry;
    engine::reflection::ComponentReflection types;
    engine::reflection::register_builtin_components(registry, types);

    if (options.scene)
    {
        std::ifstream file(*options.scene);
        if (!file)
        {
            reporter->error("scene not found: " + *options.scene);
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        try
        {
            engine::scene::load_from_json(buffer.str(), world, types);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            reporter->error(std::string("failed to load scene: ") + e.what());
            return 1;
        }
    }

    engine::runtime::RuntimeLoop loop(1.0F / static_cast<float>(options.fixed_hz));

    std::signal(SIGINT, on_sigint);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    PumpDeps deps{
        [start] {
            return std::chrono::duration<double>(clock::now() - start).count();
        },
        [](double seconds) {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        },
        [] { return g_stop.load(); }};

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);
    return 0;
}
```

- [ ] **Step 2: Add the executable target**

In `app/CMakeLists.txt`, after the `mosaic_app` library definition, add:

```cmake
add_executable(mosaic main.cpp)
target_link_libraries(mosaic PRIVATE mosaic_app)
target_compile_features(mosaic PRIVATE cxx_std_20)
```

- [ ] **Step 3: Build and run the full suite**

Run: `cmake --build --preset debug && ctest --preset debug`
Expected: PASS — the whole suite green, including all `[cli]`, `[app]`, and `[reflection]` cases.

- [ ] **Step 4: Smoke-test the executable (manual verification)**

Run: `./build/debug/app/mosaic run --ticks 10 --fixed-hz 60 --json`
Expected: the process runs, prints a final `{"event":"summary","ticks":10,...,"stopped_by":"ticks"}` line to stdout, and exits 0. Confirm with:

```bash
./build/debug/app/mosaic run --ticks 10 --json | tail -1
echo "exit: $?"
```

Expected: a summary JSON line with `"ticks":10` and `"stopped_by":"ticks"`, then `exit: 0`.

- [ ] **Step 5: Commit**

```bash
git add app/main.cpp app/CMakeLists.txt
git commit -m "app: mosaic run entry point with real clock + SIGINT (#35)"
```

---

## Self-Review

**Spec coverage** (against `docs/superpowers/specs/2026-08-17-app-cli-shell-design.md`):

| Spec item | Task |
| --- | --- |
| `mosaic run` builds World, registers TransformSystem, ticks via RuntimeLoop | Task 5 (main) + Task 4 (pump) |
| `--scene` loads a graph before running | Task 5 (file read + load_from_json), Task 2 (component registration) |
| `--duration` / `--ticks` bound the run, clean exit | Task 4 (stop sources), Task 5 (exit codes) |
| Frame pump unit-tested with injected clock, no real sleeps | Task 4 (FakeClock + no-op sleep) |
| `app/` target wired into CMake | Task 1 (lib + root), Task 5 (executable) |
| CLI11 arg parsing, confined to options.cpp | Task 1 |
| `--json` periodic progress + final summary + error | Task 3 (reporter), Task 4 (progress cadence) |
| Shared `register_builtin_components` (TypeRegistry&, ComponentReflection&) | Task 2 |
| Injected clock/sleep/stop/reporter seam | Task 4 (PumpDeps + StatusReporter) |
| Error handling → exit codes (args, unreadable scene, SerializationError) | Task 1 (arg exit_code), Task 5 (scene/catch) |
| SIGINT → clean stop, `stopped_by == "signal"` | Task 4 (stop check), Task 5 (handler) |
| Tier contract (app depends down only; one engine addition adds no dep) | Task 1 (link dirs), Task 2 (header-only) |

No spec item is unmapped.

**Placeholder scan:** No TBD/TODO/"handle edge cases". Every code step carries complete code and exact commands. The two `> Note` blocks (Transform JSON shape in Task 2; tick-overshoot in Task 4) give concrete fallback instructions, not deferrals.

**Type consistency:** `Options` fields (`scene`/`duration_s`/`ticks`/`json`/`fps`/`fixed_hz`), `ParseResult{options, exit_code}`, `RunSummary{ticks, elapsed_s, fixed_dt, scene, stopped_by}`, `StatusReporter::{progress,summary,error}`, `PumpDeps{now, sleep, stop_requested}`, and `FramePump(loop, deps, reporter, options).run(world)` are named identically across Tasks 1–5 and their tests. `register_builtin_components(TypeRegistry&, ComponentReflection&)` matches the verified engine signature. `stopped_by` values (`"signal"`/`"duration"`/`"ticks"`) are consistent between the pump and its tests.
