# Component Reference Docs Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `mosaic docs` CLI subcommand that walks every built-in engine
component via `ComponentReflection` and generates
`docs/reference/components.md`, wiring the new `docs/reference/` directory
into the existing docs-hub nav generator.

**Architecture:** A header-only core function
(`app/docs/component_reference.hpp`) builds a throwaway `World`, walks
`ComponentReflection::registered_names()`, attaches a default instance of
each, and renders its fields via a local `FieldVisitor` implementation into
one combined markdown page. `app/run/docs.cpp` is the thin CLI wrapper
(mirrors `app/run/create.cpp`) that registers built-in components and writes
the result to `MOSAIC_DOCS_DIR/reference/components.md`. `app/cli/options.hpp`
gains `Command::Docs`, `app/main.cpp` dispatches to it. Finally,
`tools/docs/regenerate_nav.py` and `docs/index.html` are extended with a 7th
auto-generated nav group (`reference`), and the tool is run for real to
populate it.

**Tech Stack:** C++20, Catch2 (`tests/unit/app/*_test.cpp`), CLI11, Python 3
(`tools/docs/regenerate_nav.py`, unchanged core logic — only its `GROUPS`
list grows).

**Spec:** `docs/superpowers/specs/2026-09-14-component-reference-docs-design.md`

## Global Constraints

- One combined `docs/reference/components.md` page, not one file per
  component (28 components is small enough for one scannable page, and it
  keeps the nav generator tracking only one file in the new group).
- `FieldVisitor::field_readonly` cannot distinguish `bool`/non-float
  arithmetic/`EntityID`/`Handle<R>` from the callback alone — reference docs
  show field name + rendered default value, never a precise type label, for
  anything routed through it. Do not attempt to work around this by
  inspecting `M` in a caller — the visitor interface itself is out of scope
  for this plan (shared with Studio's Inspector).
- Scripting-globals reference docs (2b) are out of scope entirely. Do not
  touch `modules/script_python/world_globals.cpp` or
  `modules/script_python_studio/studio_plugin_script_engine.cpp`.
- `mosaic docs` takes no arguments — do not add an `Options` field for it.
- Every new/modified compile-time path define
  (`MOSAIC_TEMPLATES_DIR`/`MOSAIC_FONT_DIR` pattern) must be mirrored exactly
  by the new `MOSAIC_DOCS_DIR`.

---

### Task 1: Core generator + its tests

**Files:**
- Create: `app/docs/component_reference.hpp`
- Create: `tests/unit/app/component_reference_test.cpp`
- Modify: `tests/CMakeLists.txt` (add the new test file to the existing
  `unit/app/*.cpp` list)

**Interfaces:**
- Consumes: `mosaic::engine::reflection::ComponentReflection` (existing,
  `engine/reflection/component_reflection.hpp`) — `registered_names()`,
  `add_default(world, entity, name)`, `inspect(world, entity, name,
  FieldVisitor&)`, `remove_value(world, entity, name)`.
  `mosaic::engine::reflection::FieldVisitor` (existing,
  `engine/reflection/field_visitor.hpp`) — `field_float`, `field_readonly`,
  `field_string`, `field_asset`, `begin_group`, `end_group`.
  `mosaic::engine::World` (existing, `engine/world/world.hpp`) —
  `create_entity()`.
  `mosaic::engine::reflection::register_builtin_components` (existing,
  `engine/reflection/builtin_components.hpp`) — used only in the test's
  integration case, and later in Task 3's CLI wrapper.
  `mosaic::foundation::reflection::TypeRegistry` (existing,
  `foundation/reflection/type_registry.hpp`).
- Produces: `mosaic::app::generate_component_reference_markdown(const
  mosaic::engine::reflection::ComponentReflection& types) -> std::string`
  — the function Task 3's `app/run/docs.cpp` calls.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/component_reference_test.cpp`:

```cpp
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/docs/component_reference.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::generate_component_reference_markdown;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::foundation::reflection::TypeRegistry;

namespace
{
// Synthetic components with known field shapes, local to this test file —
// not the real 28-component registry (that's covered by the integration
// case below).

struct FloatOnly
{
    float speed = 3.5f;
};

template <class Visitor>
void reflect(Visitor& v, FloatOnly& c)
{
    v.member("speed", c.speed);
}

struct StringOnly
{
    std::string label = "hello";
};

template <class Visitor>
void reflect(Visitor& v, StringOnly& c)
{
    v.member("label", c.label);
}

struct BoolOnly
{
    bool enabled = true;
};

template <class Visitor>
void reflect(Visitor& v, BoolOnly& c)
{
    v.member("enabled", c.enabled);
}

struct Nested
{
    float x = 1.0f;
    float y = 2.0f;
};

template <class Visitor>
void reflect(Visitor& v, Nested& c)
{
    v.member("x", c.x);
    v.member("y", c.y);
}

struct WithGroup
{
    Nested offset{};
};

template <class Visitor>
void reflect(Visitor& v, WithGroup& c)
{
    v.member("offset", c.offset);
}
} // namespace

TEST_CASE("component reference renders a bare float field", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<FloatOnly>(registry, "FloatOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## FloatOnly") != std::string::npos);
    REQUIRE(markdown.find("`speed`: 3.500000") != std::string::npos);
}

TEST_CASE("component reference renders a string field", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<StringOnly>(registry, "StringOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## StringOnly") != std::string::npos);
    REQUIRE(markdown.find("`label`: hello") != std::string::npos);
}

TEST_CASE("component reference renders a bool field via field_readonly", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<BoolOnly>(registry, "BoolOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## BoolOnly") != std::string::npos);
    REQUIRE(markdown.find("`enabled`: true") != std::string::npos);
}

TEST_CASE("component reference indents a nested group's fields", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<WithGroup>(registry, "WithGroup");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("## WithGroup") != std::string::npos);
    REQUIRE(markdown.find("`offset`") != std::string::npos);
    // Nested fields render indented one level deeper than the group line.
    REQUIRE(markdown.find("  - `x`: 1.000000") != std::string::npos);
    REQUIRE(markdown.find("  - `y`: 2.000000") != std::string::npos);
}

TEST_CASE("component reference opens with an auto-generated banner naming the type-label limitation", "[docs]")
{
    TypeRegistry registry;
    ComponentReflection types;
    types.register_component<FloatOnly>(registry, "FloatOnly");

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE(markdown.find("<!-- AUTO-GENERATED") != std::string::npos);
    REQUIRE(markdown.find("do not edit by hand") != std::string::npos);
}

TEST_CASE("component reference covers every real built-in component", "[docs][integration]")
{
    TypeRegistry registry;
    ComponentReflection types;
    register_builtin_components(registry, types);

    const std::string markdown = generate_component_reference_markdown(types);

    REQUIRE_FALSE(markdown.empty());
    REQUIRE(markdown.find("<!-- AUTO-GENERATED") != std::string::npos);
    for (const std::string& name : types.registered_names())
    {
        const std::string heading = "## " + name;
        REQUIRE(markdown.find(heading) != std::string::npos);
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL — `app/docs/component_reference.hpp` doesn't exist yet
(`fatal error: 'app/docs/component_reference.hpp' file not found`), and the
new test file isn't yet in `tests/CMakeLists.txt`'s source list, so also add
it now (needed before the build can even attempt to compile the new test):

In `tests/CMakeLists.txt`, in the `unit/app/*.cpp` list (the same list
containing `unit/app/options_test.cpp` and `unit/app/project_create_test.cpp`
at lines 57 and 69), add a new line:

```
  unit/app/component_reference_test.cpp
```

The list is not alphabetized (e.g. `options_test.cpp` precedes
`status_reporter_test.cpp` precedes `frame_pump_test.cpp`) — add the new
line directly after `unit/app/options_test.cpp` (the existing line at
index 57) without reordering any existing entry.

Re-run the build. Expected: FAIL with the missing-header compile error.

- [ ] **Step 3: Write the minimal implementation**

Create `app/docs/component_reference.hpp`:

```cpp
#pragma once

#include <sstream>
#include <string>
#include <string_view>

#include "engine/reflection/component_reflection.hpp"
#include "engine/reflection/field_visitor.hpp"
#include "engine/world/world.hpp"

namespace mosaic::app
{

namespace detail
{

// Renders a single component's fields as an indented markdown bullet list.
// Nested groups (Vec3, Quat, ...) indent one level deeper per begin_group.
class MarkdownFieldVisitor final : public mosaic::engine::reflection::FieldVisitor
{
public:
    explicit MarkdownFieldVisitor(std::ostringstream& out) : out_(out) {}

    void field_float(std::string_view name, float& value) override
    {
        emit(name, std::to_string(value));
    }

    void field_readonly(std::string_view name, const std::string& display) override
    {
        emit(name, display);
    }

    void field_string(std::string_view name, std::string& value) override
    {
        emit(name, value);
    }

    void field_asset(std::string_view name,
                     mosaic::foundation::serialization::AssetID& value) override
    {
        emit(name, value.to_string());
    }

    void begin_group(std::string_view name) override
    {
        indent();
        out_ << "- `" << name << "`\n";
        ++depth_;
    }

    void end_group() override
    {
        --depth_;
    }

private:
    void indent()
    {
        for (int i = 0; i < depth_; ++i)
        {
            out_ << "  ";
        }
    }

    void emit(std::string_view name, const std::string& display)
    {
        indent();
        out_ << "- `" << name << "`: " << display << "\n";
    }

    std::ostringstream& out_;
    int depth_ = 0;
};

} // namespace detail

// Renders every registered component type's field shape as one combined
// markdown reference page. Field values shown are those of a freshly
// default-constructed instance (this function attaches, inspects, and
// removes one throwaway component at a time on an internal World; no
// caller-visible state is touched).
inline std::string generate_component_reference_markdown(
    const mosaic::engine::reflection::ComponentReflection& types)
{
    std::ostringstream out;
    out << "<!-- AUTO-GENERATED by `mosaic docs` -- do not edit by hand -->\n\n";
    out << "# Component Reference\n\n";
    out << "Every built-in engine component, with its fields and their "
           "default values. Fields shown as a plain name/value pair "
           "(no type label) come from `FieldVisitor::field_readonly`, which "
           "is used uniformly for `bool`, non-`float` numeric types, "
           "`EntityID`, and `Handle<R>` fields -- the underlying type isn't "
           "distinguishable from the callback alone, so no more precise "
           "label is shown for those fields.\n\n";

    mosaic::engine::World world;
    const mosaic::engine::World::EntityID entity = world.create_entity();

    for (const std::string& name : types.registered_names())
    {
        out << "## " << name << "\n\n";
        types.add_default(world, entity, name);
        std::ostringstream fields;
        detail::MarkdownFieldVisitor visitor(fields);
        types.inspect(world, entity, name, visitor);
        out << fields.str();
        out << "\n";
        types.remove_value(world, entity, name);
    }

    return out.str();
}

} // namespace mosaic::app
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[docs]"`

Expected: PASS — all 6 new `TEST_CASE`s green, no other tags affected.

- [ ] **Step 5: Commit**

```bash
git add app/docs/component_reference.hpp tests/unit/app/component_reference_test.cpp tests/CMakeLists.txt
git commit -m "feat(docs): add component reference markdown generator"
```

---

### Task 2: `Command::Docs` CLI wiring

**Files:**
- Modify: `app/cli/options.hpp`
- Modify: `app/cli/options.cpp`
- Modify: `app/main.cpp`
- Modify: `tests/unit/app/options_test.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `mosaic::app::Command::Docs` (new enumerator) — Task 3's
  `run_docs` dispatch in `app/main.cpp` and its own `run/docs.hpp` depend on
  this name existing.

- [ ] **Step 1: Write the failing test**

In `tests/unit/app/options_test.cpp`, add (after the existing `TEST_CASE`s,
using the same `parse_args` helper already defined in that file):

```cpp
TEST_CASE("docs subcommand parses with no options", "[cli]")
{
    auto result = parse_args({"mosaic", "docs"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.command == mosaic::app::Command::Docs);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests 2>&1 | tail -40`

Expected: FAIL to compile — `mosaic::app::Command::Docs` is not a member of
`Command` (the enum doesn't have that enumerator yet), and separately CLI11
would reject an unknown `docs` subcommand once it did compile.

- [ ] **Step 3: Implement `Command::Docs`**

In `app/cli/options.hpp`, change:

```cpp
enum class Command { Run, Editor, Create, Build, Import };
```

to:

```cpp
enum class Command { Run, Editor, Create, Build, Import, Docs };
```

In `app/cli/options.cpp`, change the `require_subcommand` comment and add
the new subcommand right after the `import` subcommand block:

```cpp
    app.require_subcommand(1); // exactly one subcommand (`run`, `editor`, `create`, `build`, `import`, or `docs`)
```

(this replaces the existing comment on that line only — the
`app.require_subcommand(1);` call itself is unchanged).

After:

```cpp
    CLI::App* import = app.add_subcommand("import", "Import an asset file into a project");
    import->add_option("project", import_project_name, "Project directory")->required();
    import->add_option("file", import_file_name, "Source asset file (PPM image)")->required();
```

add:

```cpp

    CLI::App* docs = app.add_subcommand("docs", "Generate the built-in component reference (docs/reference/components.md)");
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
    if (docs->parsed())
    {
        result.options.command = Command::Docs;
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests && ./build/debug/tests/mosaic_unit_tests "[cli]"`

Expected: PASS — the new case plus all existing `[cli]` cases still green.

- [ ] **Step 5: Wire the dispatch in `app/main.cpp`**

In `app/main.cpp`, after the existing:

```cpp
    if (pr.options.command == Command::Import)
    {
```

find that whole block (it calls `run_import(pr.options)` and returns its
result — read the surrounding 3-4 lines to match the exact shape) and add,
directly after its closing `}`:

```cpp
    if (pr.options.command == Command::Docs)
    {
        return run_docs(pr.options);
    }
```

Add the include, alongside the other `app/run/*.hpp` includes (next to
`#include "app/run/create.hpp"`):

```cpp
#include "app/run/docs.hpp"
```

This will not yet compile (`run_docs`/`app/run/docs.hpp` don't exist) —
that's expected; Task 3 creates them. Do not attempt to build
`app/main.cpp` standalone in this task; Task 3's step 2 is the first point
the full binary is expected to build.

- [ ] **Step 6: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp app/main.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): add Command::Docs subcommand"
```

---

### Task 3: `app/run/docs.cpp` CLI wrapper + generate the real reference page

**Files:**
- Create: `app/run/docs.hpp`
- Create: `app/run/docs.cpp`
- Modify: `app/CMakeLists.txt`
- Create (generated, then committed): `docs/reference/components.md`

**Interfaces:**
- Consumes: `mosaic::app::generate_component_reference_markdown` (Task 1,
  `app/docs/component_reference.hpp`). `mosaic::app::Command::Docs`,
  `mosaic::app::Options` (Task 2, `app/cli/options.hpp`).
  `mosaic::engine::reflection::register_builtin_components` (existing).
- Produces: `mosaic::app::run_docs(const Options&) -> int` — the function
  `app/main.cpp`'s Task-2-added dispatch block calls.

- [ ] **Step 1: Write `app/run/docs.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic docs`: generate docs/reference/components.md from the
// built-in component registry. Returns a process exit code.
int run_docs(const Options& options);
} // namespace mosaic::app
```

- [ ] **Step 2: Write `app/run/docs.cpp`**

```cpp
#include "app/run/docs.hpp"

#include <filesystem>
#include <fstream>

#include "app/docs/component_reference.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::app
{
int run_docs(const Options&)
{
    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  types;
    mosaic::engine::reflection::register_builtin_components(registry, types);

    const std::string markdown = generate_component_reference_markdown(types);

    const std::filesystem::path out_dir  = std::filesystem::path(MOSAIC_DOCS_DIR) / "reference";
    const std::filesystem::path out_file = out_dir / "components.md";
    std::filesystem::create_directories(out_dir);

    std::ofstream out(out_file, std::ios::trunc);
    if (!out)
    {
        mosaic::foundation::log::error("docs", "docs: failed to open {} for writing", out_file.string());
        return 1;
    }
    out << markdown;
    mosaic::foundation::log::info("docs", "docs: wrote {}", out_file.string());
    return 0;
}
} // namespace mosaic::app
```

- [ ] **Step 3: Wire `app/CMakeLists.txt`**

In `app/CMakeLists.txt`, in the `add_library(mosaic_app STATIC ...)` source
list, add `run/docs.cpp` right after `run/create.cpp`:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
)
```

In the same file, add `MOSAIC_DOCS_DIR` to the existing
`target_compile_definitions` block:

```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts"
  MOSAIC_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates"
  MOSAIC_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs")
```

- [ ] **Step 4: Build the full binary and unit tests**

Run: `cmake --build build/debug --target mosaic --target mosaic_unit_tests 2>&1 | tail -60`

Expected: PASS — clean build, no errors. (If the generator target names
differ from `mosaic`/`mosaic_unit_tests` in this tree, use
`cmake --build build/debug 2>&1 | tail -60` to build everything instead —
check `build/debug` exists first; if it doesn't, run the project's
documented configure step first.)

- [ ] **Step 5: Run the full unit test suite**

Run: `./build/debug/tests/mosaic_unit_tests`

Expected: PASS — all tests green, including Task 1's and Task 2's new cases.

- [ ] **Step 6: Generate the real reference page**

Run: `./build/debug/app/mosaic docs`

Expected output: `[INFO] docs: wrote <repo>/docs/reference/components.md`
(exit code 0). Verify the file now exists and is non-empty:

Run: `wc -l docs/reference/components.md`

Expected: a few hundred lines (28 `## ` headings plus their fields).

- [ ] **Step 7: Commit**

```bash
git add app/run/docs.hpp app/run/docs.cpp app/CMakeLists.txt docs/reference/components.md
git commit -m "feat(app): add mosaic docs subcommand, generate components.md"
```

---

### Task 4: Wire `docs/reference/` into the nav generator + next-steps entry

**Files:**
- Modify: `tools/docs/regenerate_nav.py`
- Modify: `docs/index.html`
- Modify: `docs/project/next-steps.md`

**Interfaces:**
- Consumes: `docs/reference/components.md` (Task 3's generated output) as
  the sole file the new `reference` group will list.
- Produces: nothing further downstream — this is the final task.

- [ ] **Step 1: Add the `reference` group to `regenerate_nav.py`**

In `tools/docs/regenerate_nav.py`, change:

```python
GROUPS = [
    ("updates", "updates", "newest-first"),
    ("architecture", "architecture", "alphabetical"),
    ("design", "design", "alphabetical"),
    ("rfcs", "rfcs", "alphabetical"),
    ("specs", "superpowers/specs", "oldest-first"),
    ("plans", "superpowers/plans", "oldest-first"),
]
```

to:

```python
GROUPS = [
    ("updates", "updates", "newest-first"),
    ("architecture", "architecture", "alphabetical"),
    ("design", "design", "alphabetical"),
    ("reference", "reference", "alphabetical"),
    ("rfcs", "rfcs", "alphabetical"),
    ("specs", "superpowers/specs", "oldest-first"),
    ("plans", "superpowers/plans", "oldest-first"),
]
```

(Placed right after `design`, matching the spec's stated nav position —
"right after Design, before Project & Legal".)

- [ ] **Step 2: Add the `Reference` nav-group markers to `docs/index.html`**

In `docs/index.html`, right after the closing `</div>` of the existing
`Design` nav-group (the block containing
`<!-- AUTO-GENERATED:design -->` ... `<!-- END-AUTO-GENERATED -->`, ending
at its own `</div>`), insert a new nav-group block, before the
`Project & Legal` block:

```html
        <div class="nav-group">
            <h3>Reference</h3>
            <!-- AUTO-GENERATED:reference -->
            <!-- END-AUTO-GENERATED -->
        </div>

```

- [ ] **Step 3: Run the generator for real**

Run: `python3 tools/docs/regenerate_nav.py`

Expected output: `regenerated docs/index.html`. The new `Reference`
nav-group now contains one `<a class="nav-link" onclick="loadFile(...)">`
line for `docs/reference/components.md`, titled from its `# Component
Reference` heading.

- [ ] **Step 4: Verify with the existing generator test suite**

Run: `python3 -m unittest tools.docs.test_regenerate_nav -v`

Expected: all existing tests still PASS (the generator's own test suite is
unchanged by this plan — it already exercises the `GROUPS`-driven splice
logic generically, so a new group needs no new generator tests).

- [ ] **Step 5: Spot-check the rendered page**

Run: `python3 -m http.server 8000 --directory docs &` then fetch
`http://localhost:8000/index.html` (or open it in a browser) and confirm the
new "Reference" nav group appears between "Design" and "Project & Legal",
with a working link to the component reference page. Stop the server
afterward (`kill %1` or the equivalent job-control command for the
background server started above).

- [ ] **Step 6: Add the `docs/project/next-steps.md` entry**

At the top of `docs/project/next-steps.md`'s `**Last updated:**` line
(the file's existing convention: each new entry is prepended, separated by
` ¶ ` from the previous one — follow the exact style already visible at the
top of that line), add a new entry describing this work: `mosaic docs`
generates `docs/reference/components.md` from `ComponentReflection`'s 28
built-in components; `docs/reference/` is now the nav generator's 7th
auto-generated group; the accepted type-label limitation
(`field_readonly` can't distinguish `bool`/int/`EntityID`/`Handle<R>`) is
documented in the generated page's own intro; scripting-globals reference
docs (2b) remain deferred, gated on a future pybind11 docstring-annotation
pass. Link the spec:
`docs/superpowers/specs/2026-09-14-component-reference-docs-design.md`.
Update the date to today's date if it has changed since the file was last
touched.

- [ ] **Step 7: Commit**

```bash
git add tools/docs/regenerate_nav.py docs/index.html docs/project/next-steps.md
git commit -m "feat(docs): wire component reference into the docs hub nav"
```
