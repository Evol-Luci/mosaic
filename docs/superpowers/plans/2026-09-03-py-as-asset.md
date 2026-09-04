# `.py`-as-asset — Implementation Plan

> **For agentic workers:** implement task-by-task, TDD, commit after each task.

**Goal:** Import a `.py` into a project as a `type="script"` asset, load it into
the `AssetRegistry`, and let a `ScriptComponent` reference it by `AssetID`;
`ScriptSystem` resolves the id → source and runs it.

**Architecture:** `ScriptSource` value type held in the registry; `ScriptSystem`
(engine/script) resolves script ids through the `AssetRegistry` (engine/assets,
same tier — direct dependency, no seam). Import/load pipeline in
`app/asset/asset_io.hpp` gains a script branch alongside the existing image one.

**Tech Stack:** C++20, Catch2, existing asset/serialization/script infra.

**Spec:** `docs/superpowers/specs/2026-09-03-py-as-asset-design.md`

## Global Constraints

- Tier wall: `foundation/` → `engine/` → `modules/` → `app/`. `ScriptSystem`
  depending on `AssetRegistry` is intra-`engine/` (allowed). No backend type
  crosses interfaces.
- New ctor/function params are **defaulted**; old scenes (nil `script`) behave
  exactly as before — the field is additive.
- Catch2 `TEST_CASE` names must not start with `--`.
- No new dependencies.

---

### Task 1: `ScriptSource` resource + `ScriptComponent.script` field

**Files:**
- Create: `engine/script/script_source.hpp`
- Modify: `engine/script/script_component.hpp` (add reflected `AssetID script`)
- Create: `tests/unit/script/script_asset_test.cpp` (reflection round-trip)
- Modify: `tests/CMakeLists.txt` (add after `unit/script/script_host_test.cpp`)

**Interfaces produced:**
- `mosaic::engine::script::ScriptSource { std::string text; }`
- `ScriptComponent` gains `mosaic::foundation::serialization::AssetID script;` (reflected as `"script"`).

- [ ] **Step 1: Write `engine/script/script_source.hpp`:**

```cpp
#pragma once

#include <string>

namespace mosaic::engine::script
{
// A script asset's payload: the Python source text. Registered in the
// AssetRegistry under a script AssetID; ScriptSystem resolves + runs it.
struct ScriptSource
{
    std::string text;
};
} // namespace mosaic::engine::script
```

- [ ] **Step 2: Modify `engine/script/script_component.hpp`** — add the include and field + reflect:

```cpp
#include "foundation/serialization/reference.hpp"
```
```cpp
struct ScriptComponent
{
    std::string                                 source; // inline Python (fallback)
    std::string                                 path;   // optional .py file (fallback)
    mosaic::foundation::serialization::AssetID  script; // script asset (nil = unset), takes precedence
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
    visitor.member("path", c.path);
    visitor.member("script", c.script);
}
```

- [ ] **Step 3: Write the reflection test** `tests/unit/script/script_asset_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_component.hpp"
#include "engine/script/script_source.hpp"
#include "foundation/serialization/reference.hpp"
#include "foundation/serialization/reflect_value.hpp"

using mosaic::engine::script::ScriptComponent;
using mosaic::foundation::serialization::AssetID;

TEST_CASE("ScriptComponent round-trips its script AssetID through reflection", "[script]")
{
    ScriptComponent c;
    c.source = "inline";
    c.script = AssetID::from_string("00000000-0000-0000-0000-0000000000aa");

    const auto value = mosaic::foundation::serialization::to_value(c);
    ScriptComponent back;
    mosaic::foundation::serialization::from_value(value, back);

    REQUIRE(back.source == "inline");
    REQUIRE(back.script == c.script);
    REQUIRE_FALSE(back.script.is_nil());
}
```

Add to `tests/CMakeLists.txt` after `unit/script/script_host_test.cpp`:
```
  unit/script/script_asset_test.cpp
```

> NOTE: confirm the `to_value`/`from_value` free functions come from
> `foundation/serialization/reflect_value.hpp` (asset_io.hpp uses them) and that
> `AssetID::from_string` accepts the canonical hyphenated form (the image tests
> and `orphan.toml` use it).

- [ ] **Step 4: Build + run** `cmake --build build/debug -j` then
  `ctest --test-dir build/debug -R "round-trips its script AssetID"` → PASS.

- [ ] **Step 5: Commit** `git add engine/script/script_source.hpp engine/script/script_component.hpp tests/unit/script/script_asset_test.cpp tests/CMakeLists.txt && git commit -m "feat(script): ScriptSource resource + ScriptComponent.script AssetID reference"`

---

### Task 2: `ScriptSystem` resolves a script asset via the registry

**Files:**
- Modify: `engine/script/script_system.hpp` (registry ctor param + resolution branch)
- Modify: `tests/unit/script/script_system_test.cpp` (add asset-resolution test)

**Interfaces consumed:** Task 1's `ScriptSource`, `ScriptComponent.script`;
`mosaic::engine::assets::AssetRegistry` (`engine/assets/asset_registry.hpp`),
`AssetMetadata` (`engine/assets/asset_metadata.hpp`).
**Interfaces produced:** `ScriptSystem(const ComponentReflection&, EngineFactory, ScriptHost = {}, mosaic::engine::assets::AssetRegistry* registry = nullptr)`.

- [ ] **Step 1: Write the failing test** in `tests/unit/script/script_system_test.cpp`
  (new TEST_CASE; add includes for `asset_registry.hpp`, `asset_metadata.hpp`,
  `script_source.hpp` if absent):

```cpp
TEST_CASE("ScriptSystem resolves a script asset by AssetID and runs it", "[script]")
{
    using mosaic::engine::assets::AssetMetadata;
    using mosaic::engine::assets::AssetRegistry;
    using mosaic::engine::script::ScriptSource;
    using mosaic::foundation::serialization::AssetID;

    mosaic::foundation::reflection::TypeRegistry reg;
    mosaic::engine::reflection::ComponentReflection types;
    mosaic::engine::reflection::register_builtin_components(reg, types);

    AssetRegistry registry;
    const AssetID id = AssetID::from_string("00000000-0000-0000-0000-0000000000bb");
    AssetMetadata meta; meta.uuid = id; meta.type = "script"; meta.version = 1;
    registry.register_resource<ScriptSource>(
        meta, std::make_shared<ScriptSource>(ScriptSource{
            "def on_update(dt):\n"
            "    t = get_component(entity, 'Transform')\n"
            "    t['position']['x'] += dt\n"
            "    set_component(entity, 'Transform', t)\n"}));

    mosaic::engine::World world;
    world.add_system<mosaic::engine::script::ScriptSystem>(
        types, [] { return mosaic::script::python::create_python_script_engine(); },
        mosaic::engine::script::ScriptHost{}, &registry);

    const auto e = world.create_entity();
    world.add_component<mosaic::engine::transform::Transform>(
        e, mosaic::engine::transform::Transform{});
    mosaic::engine::script::ScriptComponent sc; sc.script = id; // no inline/path
    world.add_component<mosaic::engine::script::ScriptComponent>(e, sc);

    world.tick(0.5f);
    REQUIRE(world.get_component<mosaic::engine::transform::Transform>(e)->position.x == 0.5f);
}
```

> NOTE: reuse the file's existing includes/usings. `std::make_shared` needs
> `<memory>`. If `ScriptHost` isn't included there, add `engine/script/script_host.hpp`.

- [ ] **Step 2: Run to verify FAIL** — build (compile error: ctor takes ≤3 args → then assertion). `ctest --test-dir build/debug -R "resolves a script asset"`.

- [ ] **Step 3: Implement in `engine/script/script_system.hpp`.** Add includes:
```cpp
#include "engine/assets/asset_registry.hpp"
#include "engine/script/script_source.hpp"
```
  Extend the ctor (add a 4th defaulted param + member init):
```cpp
    ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory,
                 ScriptHost host = {}, mosaic::engine::assets::AssetRegistry* registry = nullptr)
        : types_(types), factory_(std::move(factory)), host_(std::move(host)), registry_(registry)
    {
    }
```
  Add the member (near `host_`): `mosaic::engine::assets::AssetRegistry* registry_ = nullptr;`

  In `update()`, replace the `if (sc.path.empty()) { ... } else { ... }`
  resolution with an **asset → path → inline** cascade:
```cpp
                std::string desired;
                if (!sc.script.is_nil() && registry_ != nullptr)
                {
                    entry.path.clear();
                    const auto res = registry_->load<ScriptSource>(sc.script);
                    desired = res ? res->text : entry.source; // missing asset -> keep last-good
                }
                else if (sc.path.empty())
                {
                    entry.path.clear();
                    desired = sc.source;
                }
                else
                {
                    // ...existing file/mtime branch verbatim...
                }
```
  (Keep the existing file/mtime block unchanged as the final `else`.)

- [ ] **Step 4: Run to verify PASS** — `ctest --test-dir build/debug -R "resolves a script asset"` → PASS.

- [ ] **Step 5: Full suite** `ctest --test-dir build/debug` → all pass (existing script tests, including the host + hotreload ones, must stay green — the path/inline branches are unchanged).

- [ ] **Step 6: Commit** `git add engine/script/script_system.hpp tests/unit/script/script_system_test.cpp && git commit -m "feat(script): ScriptSystem resolves a script asset (AssetID) via the registry"`

---

### Task 3: Import + load a `.py` as a `type="script"` asset

**Files:**
- Modify: `app/asset/asset_io.hpp` (`import_asset` script branch; `load_project_assets` script branch)
- Modify: `tests/unit/app/asset_import_test.cpp` (add script import + load tests)

**Interfaces consumed:** Task 1's `ScriptSource`.

- [ ] **Step 1: Write failing tests** — add to `tests/unit/app/asset_import_test.cpp`
  (add includes `engine/script/script_source.hpp`; a small `.py` writer):

```cpp
// near write_ppm:
void write_py(const std::string& path)
{
    REQUIRE(save_scene_file(path, "def on_update(dt):\n    pass\n"));
}
```

```cpp
TEST_CASE("import_asset cooks a .py into a script asset + metadata", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("sproj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("ssrc") + ".py";
    write_py(src);

    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());
    REQUIRE_FALSE(id->is_nil());
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".script")));
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".toml")));

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("load_project_assets registers a script asset as ScriptSource", "[app]")
{
    using mosaic::engine::script::ScriptSource;
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("sproj2");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("ssrc2") + ".py";
    write_py(src);
    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());

    AssetRegistry registry;
    const int n = load_project_assets(registry, (fs::path(proj) / "assets").string());
    REQUIRE(n == 1);
    REQUIRE(registry.contains(*id));
    auto res = registry.load<ScriptSource>(*id);
    REQUIRE(res != nullptr);
    REQUIRE(res->text == "def on_update(dt):\n    pass\n");

    fs::remove_all(proj); fs::remove(src);
}
```

- [ ] **Step 2: Run to verify FAIL** (`.script` not written; `load` returns wrong/none).

- [ ] **Step 3: Implement in `app/asset/asset_io.hpp`.** Add include:
  `#include "engine/script/script_source.hpp"`.

  Rework `import_asset` so the blob content/extension/type are chosen by the
  source extension, then shared write logic runs. Replace the image-specific
  middle of the function with:

```cpp
    const std::string ext = fs::path(source_file).extension().string();
    std::string blob_bytes;
    std::string blob_ext;
    std::string asset_type;
    if (ext == ".py")
    {
        blob_bytes = *src;       // script blob is the source text verbatim
        blob_ext   = ".script";
        asset_type = "script";
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

    const std::string stem = fs::path(source_file).stem().string();
    const fs::path assets = fs::path(project_dir) / "assets";
    std::error_code ec;
    fs::create_directories(assets, ec);
    if (ec) { mosaic::foundation::log::error("import", "could not create assets/ in {}", project_dir); return std::nullopt; }

    const fs::path blob_path = assets / (stem + blob_ext);
    const fs::path meta_path = assets / (stem + ".toml");
    if (fs::exists(blob_path) || fs::exists(meta_path))
    {
        mosaic::foundation::log::error(
            "import", "asset '{}' already exists in {} (would overwrite); rename the source or remove the existing asset",
            stem, assets.string());
        return std::nullopt;
    }
    if (!save_scene_file(blob_path.string(), blob_bytes))
    {
        mosaic::foundation::log::error("import", "could not write cooked asset: {}", blob_path.string());
        return std::nullopt;
    }

    const mosaic::foundation::serialization::AssetID id = random_asset_id();
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = id;
    meta.type = asset_type;
    meta.version = 1;
    meta.source = source_file;
```
  (The metadata write + cleanup-on-failure + success log below are unchanged.)

  In `load_project_assets`, after parsing `meta`, branch on `meta.type` before
  the image-specific load. Insert:
```cpp
        if (meta.type == "script")
        {
            const fs::path script_path = entry.path().parent_path() / (entry.path().stem().string() + ".script");
            const std::optional<std::string> text = load_scene_file(script_path.string());
            if (!text) { mosaic::foundation::log::warn("import", "missing script blob {}", script_path.string()); continue; }
            registry.register_resource<mosaic::engine::script::ScriptSource>(
                meta, std::make_shared<mosaic::engine::script::ScriptSource>(
                          mosaic::engine::script::ScriptSource{*text}));
            ++count;
            continue;
        }
```
  (The existing `.image` load path remains for `type=="texture"`/default.)

- [ ] **Step 4: Run to verify PASS** — `ctest --test-dir build/debug -R "script asset"` → both new tests pass; the existing PPM import/load/collision tests still pass (image path unchanged).

- [ ] **Step 5: Full suite** `ctest --test-dir build/debug` → all pass.

- [ ] **Step 6: Commit** `git add app/asset/asset_io.hpp tests/unit/app/asset_import_test.cpp && git commit -m "feat(app): import + load a .py as a type=script asset (ScriptSource)"`

---

### Task 4: Wire the registry into `ScriptSystem` via `setup_runtime`

**Files:**
- Modify: `app/run/demo_scene.hpp` (pass `&registry` to `add_system<ScriptSystem>`)
- Modify: `tests/unit/app/script_runtime_test.cpp` (add an asset-driven runtime test)

- [ ] **Step 1: Write the failing runtime test** in `tests/unit/app/script_runtime_test.cpp`
  (add includes for `asset_metadata.hpp`, `script_source.hpp`, `<memory>`):

```cpp
TEST_CASE("setup_runtime lets a scene reference a script by AssetID", "[app]")
{
    using mosaic::engine::assets::AssetMetadata;
    using mosaic::engine::script::ScriptSource;
    using mosaic::foundation::serialization::AssetID;

    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);
    World world;
    AssetRegistry registry;

    const AssetID id = AssetID::from_string("00000000-0000-0000-0000-0000000000cc");
    AssetMetadata meta; meta.uuid = id; meta.type = "script"; meta.version = 1;
    registry.register_resource<ScriptSource>(meta, std::make_shared<ScriptSource>(ScriptSource{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"}));

    auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    ScriptComponent sc; sc.script = id;
    world.add_component<ScriptComponent>(e, sc);

    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    for (int i = 0; i < 4; ++i) { world.tick(0.25f); }
    REQUIRE(world.get_component<Transform>(e)->position.x == 1.0f);
}
```

- [ ] **Step 2: Run to verify FAIL** — the script won't run because
  `setup_runtime` doesn't yet pass the registry to `ScriptSystem`, so
  `registry_` is null and the asset id never resolves. Expected FAIL on
  `position.x == 1.0f` (stays 0).

- [ ] **Step 3: Implement in `app/run/demo_scene.hpp`.** In `setup_runtime`,
  change the `add_system<ScriptSystem>` call to pass the registry:
```cpp
    world.add_system<mosaic::engine::script::ScriptSystem>(
        reflection, std::move(script_factory), std::move(host), &registry);
```

- [ ] **Step 4: Run to verify PASS** — `ctest --test-dir build/debug -R "reference a script by AssetID"` → PASS.

- [ ] **Step 5: Full suite** `ctest --test-dir build/debug` → all pass.

- [ ] **Step 6: Commit** `git add app/run/demo_scene.hpp tests/unit/app/script_runtime_test.cpp && git commit -m "feat(app): setup_runtime wires the AssetRegistry into ScriptSystem for script assets"`

---

### Task 5: next-steps doc update (rides in this branch)

**Files:** Modify `docs/project/next-steps.md`.

- [ ] In the **Scripting** bullet, mark **`.py`-as-asset — DONE** (import a `.py`
  as a `type="script"` asset; `ScriptComponent.script` references it by
  `AssetID`; `ScriptSystem` resolves via the registry; link this spec/plan).
  Note the **OS file-watcher** (live re-import) and the **script worker thread**
  as the remaining scripting items. Commit:
  `docs: mark .py-as-asset done in next-steps`.
