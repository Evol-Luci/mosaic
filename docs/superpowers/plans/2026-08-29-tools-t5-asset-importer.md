# Tools T5 — asset importer/cooker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `mosaic import <project> <file>` cooks an external PPM image into a project asset (a binary `Image` blob + `AssetMetadata` TOML in `assets/`); `run`/`editor` load a project's `assets/` into the `AssetRegistry` at startup; `build_bundle` copies `assets/` into bundles.

**Architecture:** A new `app/asset/` module: a PPM↔`Image` codec (`image_cook.hpp`), `import_asset` + `load_project_assets` (`asset_io.hpp`), a `mosaic import` subcommand, startup asset-loading wired after `setup_runtime`, and a bundle `assets/` copy. All app-tier; reuses the #20 `Image`/`AssetMetadata`/registry and T2's `scene_io`.

**Tech Stack:** C++20 (`<filesystem>`, `<random>`), CMake, Catch2, CLI11, #10 TOML. No new third-party deps.

**Spec:** `docs/superpowers/specs/2026-08-29-tools-t5-asset-importer-design.md`

## Global Constraints

- **Tier:** all importer/CLI/IO code in `app/` (new `app/asset/`); `engine/`/`foundation/` unchanged.
- **Assets go through #20:** imported assets are `register_resource<Image>` entries keyed by `AssetID`; `AssetMetadata` round-trips via `to_toml(to_value(m))` / `from_value(from_toml(text), m)` (the `asset_metadata_test` pattern). `AssetID` is the on-disk reference.
- **Cooked image format:** `"MOSAIMG1"` (8-byte magic) + `u32 width` + `u32 height` (little-endian) + `width*height*4` RGBA bytes. Source format is binary PPM (P6), `maxval` 255.
- **Reuse:** `Image` (`engine/render/image.hpp`), `AssetMetadata` (`engine/assets/asset_metadata.hpp`), `AssetRegistry` (`engine/assets/asset_registry.hpp`), `save_scene_file`/`load_scene_file` (`app/run/scene_io.hpp`). No new serialization mechanism.
- **Build:** `cmake --preset debug` (once) then `cmake --build build/debug -j`. **Test:** `ctest --test-dir build/debug`. Exe: `build/debug/app/mosaic`.
- New test sources → `tests/CMakeLists.txt`; new compiled app sources → `app/CMakeLists.txt`.
- Namespaces: `mosaic::app`; `Image` is `mosaic::engine::render::Image`; `AssetID` is `mosaic::foundation::serialization::AssetID`; `AssetMetadata`/`AssetRegistry` are `mosaic::engine::assets::*`. Catch2 names must not start with `--`. Temp-dir tests include `<unistd.h>` for `::getpid`.

---

### Task 1: PPM↔Image codec

**Files:**
- Create: `app/asset/image_cook.hpp`
- Test: `tests/unit/app/image_cook_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces (in `mosaic::app`):
  - `std::optional<mosaic::engine::render::Image> read_ppm(const std::string& bytes);`
  - `std::string cook_image(const mosaic::engine::render::Image& image);`
  - `std::optional<mosaic::engine::render::Image> uncook_image(const std::string& bytes);`

- [ ] **Step 1: Write the failing test `tests/unit/app/image_cook_test.cpp`**

```cpp
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/asset/image_cook.hpp"
#include "engine/render/image.hpp"

using mosaic::app::cook_image;
using mosaic::app::read_ppm;
using mosaic::app::uncook_image;
using mosaic::engine::render::Image;

namespace
{
// A 2x1 binary PPM: header "P6 2 1 255\n" then 2 RGB pixels (red, green).
std::string tiny_ppm()
{
    std::string s = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    s.append(reinterpret_cast<const char*>(px), sizeof(px));
    return s;
}
} // namespace

TEST_CASE("read_ppm parses a binary P6 image to RGBA", "[app]")
{
    auto img = read_ppm(tiny_ppm());
    REQUIRE(img.has_value());
    REQUIRE(img->width == 2);
    REQUIRE(img->height == 1);
    REQUIRE(img->pixels.size() == 2 * 1 * 4);
    // pixel 0 = red, opaque
    REQUIRE(img->pixels[0] == 255); REQUIRE(img->pixels[1] == 0);
    REQUIRE(img->pixels[2] == 0);   REQUIRE(img->pixels[3] == 255);
    // pixel 1 = green, opaque
    REQUIRE(img->pixels[4] == 0);   REQUIRE(img->pixels[5] == 255);
    REQUIRE(img->pixels[6] == 0);   REQUIRE(img->pixels[7] == 255);
}

TEST_CASE("read_ppm rejects malformed input", "[app]")
{
    REQUIRE_FALSE(read_ppm("not a ppm").has_value());
    REQUIRE_FALSE(read_ppm("P6\n2 2\n255\nXYZ").has_value()); // too few pixel bytes
    REQUIRE_FALSE(read_ppm("P6\n2 2\n128\n").has_value());     // maxval != 255
}

TEST_CASE("cook_image and uncook_image round-trip an Image", "[app]")
{
    Image src; src.width = 2; src.height = 1;
    src.pixels = {255, 0, 0, 255,  0, 255, 0, 255};
    const std::string blob = cook_image(src);
    REQUIRE(blob.compare(0, 8, "MOSAIMG1") == 0);

    auto back = uncook_image(blob);
    REQUIRE(back.has_value());
    REQUIRE(back->width == 2);
    REQUIRE(back->height == 1);
    REQUIRE(back->pixels == src.pixels);

    REQUIRE_FALSE(uncook_image("MOSAIMG1short").has_value()); // truncated
    REQUIRE_FALSE(uncook_image("BADMAGIC01234567").has_value());
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/bundle_test.cpp`:
```cmake
  unit/app/image_cook_test.cpp
```
`cmake --build build/debug -j 2>&1 | tail -15` → FAIL (`image_cook.hpp` missing).

- [ ] **Step 3: Write `app/asset/image_cook.hpp`**

```cpp
#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>

#include "engine/render/image.hpp"

namespace mosaic::app
{
// Parse a binary PPM (P6): "P6" <ws> width <ws> height <ws> maxval <single-ws> <RGB bytes>.
// Produces an RGBA Image (alpha 255). nullopt on malformed input or maxval != 255. Comments
// (# lines) are not supported (v0.1). Little dependency: only <cctype>.
inline std::optional<mosaic::engine::render::Image> read_ppm(const std::string& bytes)
{
    std::size_t pos = 0;
    auto skip_ws = [&] {
        while (pos < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[pos]))) { ++pos; }
    };
    auto token = [&]() -> std::string {
        skip_ws();
        const std::size_t start = pos;
        while (pos < bytes.size() && !std::isspace(static_cast<unsigned char>(bytes[pos]))) { ++pos; }
        return bytes.substr(start, pos - start);
    };
    auto to_uint = [](const std::string& s, std::uint32_t& out) -> bool {
        if (s.empty()) { return false; }
        std::uint32_t v = 0;
        for (char c : s) { if (c < '0' || c > '9') { return false; } v = v * 10 + static_cast<std::uint32_t>(c - '0'); }
        out = v; return true;
    };

    if (token() != "P6") { return std::nullopt; }
    std::uint32_t w = 0, h = 0, maxval = 0;
    if (!to_uint(token(), w) || !to_uint(token(), h) || !to_uint(token(), maxval)) { return std::nullopt; }
    if (maxval != 255 || w == 0 || h == 0) { return std::nullopt; }
    // Exactly one whitespace byte separates the header from the pixel data.
    if (pos >= bytes.size() || !std::isspace(static_cast<unsigned char>(bytes[pos]))) { return std::nullopt; }
    ++pos;

    const std::size_t need = static_cast<std::size_t>(w) * h * 3;
    if (bytes.size() - pos < need) { return std::nullopt; }

    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i)
    {
        img.pixels[i * 4 + 0] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 0]);
        img.pixels[i * 4 + 1] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 1]);
        img.pixels[i * 4 + 2] = static_cast<std::uint8_t>(bytes[pos + i * 3 + 2]);
        img.pixels[i * 4 + 3] = 255;
    }
    return img;
}

// The cooked on-disk form: "MOSAIMG1" + u32 width + u32 height (LE) + RGBA bytes.
inline std::string cook_image(const mosaic::engine::render::Image& image)
{
    std::string out;
    out.reserve(16 + image.pixels.size());
    out += "MOSAIMG1";
    auto put_u32 = [&](std::uint32_t v) {
        out.push_back(static_cast<char>(v & 0xFF));
        out.push_back(static_cast<char>((v >> 8) & 0xFF));
        out.push_back(static_cast<char>((v >> 16) & 0xFF));
        out.push_back(static_cast<char>((v >> 24) & 0xFF));
    };
    put_u32(image.width);
    put_u32(image.height);
    out.append(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
    return out;
}

// Inverse of cook_image. nullopt on a bad magic, truncation, or a size mismatch.
inline std::optional<mosaic::engine::render::Image> uncook_image(const std::string& bytes)
{
    if (bytes.size() < 16 || bytes.compare(0, 8, "MOSAIMG1") != 0) { return std::nullopt; }
    auto get_u32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off])) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 1])) << 8) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 2])) << 16) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[off + 3])) << 24);
    };
    const std::uint32_t w = get_u32(8);
    const std::uint32_t h = get_u32(12);
    const std::size_t need = static_cast<std::size_t>(w) * h * 4;
    if (bytes.size() - 16 != need) { return std::nullopt; }
    mosaic::engine::render::Image img;
    img.width = w;
    img.height = h;
    img.pixels.assign(bytes.begin() + 16, bytes.end());
    return img;
}
} // namespace mosaic::app
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "read_ppm|cook_image" --output-on-failure
```
Expected: the three codec cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/asset/image_cook.hpp tests/unit/app/image_cook_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): PPM->Image codec + cooked-image blob format"
```

---

### Task 2: `import_asset` + `load_project_assets`

**Files:**
- Create: `app/asset/asset_io.hpp`
- Test: `tests/unit/app/asset_import_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: codec (Task 1); `save_scene_file`/`load_scene_file` (T2, `app/run/scene_io.hpp`); `load_manifest` (T3, `app/project/project_io.hpp`); `AssetMetadata` (`engine/assets/asset_metadata.hpp`); `AssetRegistry` (`engine/assets/asset_registry.hpp`); `to_value`/`to_toml`/`from_value`/`from_toml`; `AssetID`.
- Produces (in `mosaic::app`):
  - `std::optional<mosaic::foundation::serialization::AssetID> import_asset(const std::string& project_dir, const std::string& source_file);`
  - `int load_project_assets(mosaic::engine::assets::AssetRegistry& registry, const std::string& assets_dir);`

- [ ] **Step 1: Write the failing test `tests/unit/app/asset_import_test.cpp`**

```cpp
#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid

#include <catch2/catch_test_macros.hpp>

#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/scene_io.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/render/image.hpp"
#include "foundation/reflection/type_registry.hpp"

using mosaic::app::create_project;
using mosaic::app::import_asset;
using mosaic::app::load_project_assets;
using mosaic::app::save_scene_file;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::render::Image;
using mosaic::foundation::reflection::TypeRegistry;

namespace fs = std::filesystem;

namespace
{
std::string tmp(const char* tag)
{
    return (fs::temp_directory_path() / (std::string("mosaic_t5_") + tag + std::to_string(::getpid()))).string();
}
ComponentReflection make_types(TypeRegistry& r) { ComponentReflection t; register_builtin_components(r, t); return t; }
// Write a 2x1 red/green P6 PPM to `path`.
void write_ppm(const std::string& path)
{
    std::string s = "P6\n2 1\n255\n";
    const unsigned char px[] = {255, 0, 0, 0, 255, 0};
    s.append(reinterpret_cast<const char*>(px), sizeof(px));
    REQUIRE(save_scene_file(path, s));
}
} // namespace

TEST_CASE("import_asset cooks a PPM into a project asset + metadata", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src") + ".ppm";
    write_ppm(src);

    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());
    REQUIRE_FALSE(id->is_nil());
    REQUIRE(fs::exists(fs::path(proj) / "assets" / "src.image"));
    REQUIRE(fs::exists(fs::path(proj) / "assets" / "src.toml"));

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("load_project_assets registers imported assets into the registry", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj2");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));
    const std::string src = tmp("src2") + ".ppm";
    write_ppm(src);
    auto id = import_asset(proj, src);
    REQUIRE(id.has_value());

    AssetRegistry registry;
    const int n = load_project_assets(registry, (fs::path(proj) / "assets").string());
    REQUIRE(n == 1);
    REQUIRE(registry.contains(*id));
    auto img = registry.load<Image>(*id);
    REQUIRE(img != nullptr);
    REQUIRE(img->width == 2);
    REQUIRE(img->height == 1);
    REQUIRE(img->pixels.size() == 2 * 1 * 4);
    REQUIRE(img->pixels[0] == 255); // red channel of pixel 0

    fs::remove_all(proj); fs::remove(src);
}

TEST_CASE("load_project_assets: empty/missing dir is a no-op; a bad asset is skipped", "[app]")
{
    AssetRegistry registry;
    REQUIRE(load_project_assets(registry, "/no/such/mosaic_assets_dir") == 0);

    const std::string dir = tmp("assets");
    fs::remove_all(dir); fs::create_directories(dir);
    REQUIRE(load_project_assets(registry, dir) == 0); // empty

    // A .toml with no sibling .image -> skipped, not a throw.
    REQUIRE(save_scene_file((fs::path(dir) / "orphan.toml").string(),
                            "uuid = \"00000000-0000-0000-0000-000000000001\"\ntype = \"texture\"\n"
                            "version = 1\nsource = \"x\"\n"));
    REQUIRE(load_project_assets(registry, dir) == 0); // orphan skipped

    fs::remove_all(dir);
}

TEST_CASE("import_asset fails on a non-project or a bad source", "[app]")
{
    const std::string notproj = tmp("notproj");
    fs::remove_all(notproj); fs::create_directories(notproj);
    REQUIRE_FALSE(import_asset(notproj, "whatever.ppm").has_value()); // not a project

    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj3");
    fs::remove_all(proj); REQUIRE(create_project(proj, types));
    REQUIRE_FALSE(import_asset(proj, "/no/such/file.ppm").has_value()); // missing source

    fs::remove_all(notproj); fs::remove_all(proj);
}
```

- [ ] **Step 2: Register + run to fail**

Add to `tests/CMakeLists.txt` after `unit/app/image_cook_test.cpp`:
```cmake
  unit/app/asset_import_test.cpp
```
Build → FAIL (`asset_io.hpp` missing).

- [ ] **Step 3: Write `app/asset/asset_io.hpp`**

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>

#include "app/asset/image_cook.hpp"
#include "app/project/project_io.hpp" // load_manifest
#include "app/run/scene_io.hpp"       // save_scene_file / load_scene_file
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/render/image.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"     // AssetID, SerializationError
#include "foundation/serialization/reflect_value.hpp" // to_value / from_value
#include "foundation/serialization/toml_backend.hpp"  // to_toml / from_toml

namespace mosaic::app
{
// A fresh random 16-byte AssetID (no external UUID dependency).
inline mosaic::foundation::serialization::AssetID random_asset_id()
{
    std::random_device rd;
    std::array<std::uint8_t, 16> b{};
    for (auto& byte : b) { byte = static_cast<std::uint8_t>(rd() & 0xFF); }
    return mosaic::foundation::serialization::AssetID(b);
}

// Import an external PPM image into <project_dir>/assets/: cook it and write
// <assets>/<stem>.image + <assets>/<stem>.toml (AssetMetadata). Returns the new AssetID, or
// nullopt (and logs) on failure. Never leaves a half-written asset (removes the blob if the
// metadata write fails).
inline std::optional<mosaic::foundation::serialization::AssetID>
import_asset(const std::string& project_dir, const std::string& source_file)
{
    namespace fs = std::filesystem;

    if (!load_manifest(project_dir))
    {
        mosaic::foundation::log::error("import", "not a Mosaic project: {}", project_dir);
        return std::nullopt;
    }
    const std::optional<std::string> src = load_scene_file(source_file);
    if (!src)
    {
        mosaic::foundation::log::error("import", "source not found: {}", source_file);
        return std::nullopt;
    }
    const std::optional<mosaic::engine::render::Image> img = read_ppm(*src);
    if (!img)
    {
        mosaic::foundation::log::error("import", "not a valid PPM (P6, maxval 255): {}", source_file);
        return std::nullopt;
    }

    const std::string stem = fs::path(source_file).stem().string();
    const fs::path assets = fs::path(project_dir) / "assets";
    std::error_code ec;
    fs::create_directories(assets, ec);
    if (ec) { mosaic::foundation::log::error("import", "could not create assets/ in {}", project_dir); return std::nullopt; }

    const fs::path blob_path = assets / (stem + ".image");
    const fs::path meta_path = assets / (stem + ".toml");
    if (!save_scene_file(blob_path.string(), cook_image(*img)))
    {
        mosaic::foundation::log::error("import", "could not write cooked image: {}", blob_path.string());
        return std::nullopt;
    }

    const mosaic::foundation::serialization::AssetID id = random_asset_id();
    mosaic::engine::assets::AssetMetadata meta;
    meta.uuid = id;
    meta.type = "texture";
    meta.version = 1;
    meta.source = source_file;
    const std::string meta_toml = mosaic::foundation::serialization::to_toml(
        mosaic::foundation::serialization::to_value(meta));
    if (!save_scene_file(meta_path.string(), meta_toml))
    {
        std::error_code rmec; fs::remove(blob_path, rmec); // no half-written asset
        mosaic::foundation::log::error("import", "could not write asset metadata: {}", meta_path.string());
        return std::nullopt;
    }

    mosaic::foundation::log::info("import", "imported {} as {}", source_file, id.to_string());
    return id;
}

// Scan <assets_dir>/*.toml, register each cooked Image into `registry` under its metadata uuid.
// Missing/empty dir -> 0. A malformed metadata, or a missing/corrupt sibling .image, logs a
// warning and is skipped (one bad asset must not abort startup). Returns the count registered.
inline int load_project_assets(mosaic::engine::assets::AssetRegistry& registry,
                               const std::string& assets_dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(assets_dir, ec)) { return 0; }

    int count = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(assets_dir, ec))
    {
        if (entry.path().extension() != ".toml") { continue; }
        const std::optional<std::string> meta_text = load_scene_file(entry.path().string());
        if (!meta_text) { continue; }

        mosaic::engine::assets::AssetMetadata meta;
        try
        {
            mosaic::foundation::serialization::from_value(
                mosaic::foundation::serialization::from_toml(*meta_text), meta);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            mosaic::foundation::log::warn("import", "skipping bad asset metadata {}: {}",
                                          entry.path().string(), e.what());
            continue;
        }

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
} // namespace mosaic::app
```

Note: confirm `register_resource<Image>(AssetMetadata, shared_ptr<Image>)` takes the metadata by value (it does — copy it in). If `AssetMetadata` needs `#include`, it is already included.

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "import_asset|load_project_assets" --output-on-failure
```
Expected: all four cases PASS.

- [ ] **Step 5: Commit**

```bash
git add app/asset/asset_io.hpp tests/unit/app/asset_import_test.cpp tests/CMakeLists.txt
git commit -m "feat(app): import_asset (cook PPM -> project asset) + load_project_assets"
```

---

### Task 3: CLI — `import` subcommand

**Files:**
- Modify: `app/cli/options.hpp` (Command::Import; `import_project`, `import_file`)
- Modify: `app/cli/options.cpp` (import subcommand)
- Test: extend `tests/unit/app/options_test.cpp`

**Interfaces:**
- Produces: `Command::Import`; `Options::import_project`, `Options::import_file` (both `std::optional<std::string>`).

- [ ] **Step 1: Extend `app/cli/options.hpp`**

```cpp
enum class Command { Run, Editor, Create, Build, Import };
```
In `struct Options`, after `build_out`:
```cpp
    std::optional<std::string>   import_project; // positional 1: project dir (import)
    std::optional<std::string>   import_file;    // positional 2: source asset file (import)
    std::optional<std::string>   assets_dir;     // computed: <project>/assets to load at startup
```

- [ ] **Step 2: Write the failing options test — extend `tests/unit/app/options_test.cpp`**

```cpp
TEST_CASE("import subcommand captures project and file", "[cli]")
{
    auto pr = parse_args({"mosaic", "import", "mygame", "tex.ppm"});
    REQUIRE_FALSE(pr.exit_code.has_value());
    REQUIRE(pr.options.command == mosaic::app::Command::Import);
    REQUIRE(pr.options.import_project.has_value());
    REQUIRE(*pr.options.import_project == "mygame");
    REQUIRE(pr.options.import_file.has_value());
    REQUIRE(*pr.options.import_file == "tex.ppm");
}
```
Build + run → FAIL (import subcommand not implemented).

- [ ] **Step 3: Implement in `app/cli/options.cpp`**

Add the shared locals (near `build_project_name`):
```cpp
    std::string import_project_name;
    std::string import_file_name;
```
Add the `import` subcommand after the `build` subcommand block:
```cpp
    CLI::App* import = app.add_subcommand("import", "Import an asset file into a project");
    import->add_option("project", import_project_name, "Project directory")->required();
    import->add_option("file", import_file_name, "Source asset file (PPM image)")->required();
```
Update the `require_subcommand` comment to mention `import`.

After parse, alongside the other captures:
```cpp
    if (import->parsed())
    {
        result.options.command = Command::Import;
    }
    if (!import_project_name.empty())
    {
        result.options.import_project = import_project_name;
    }
    if (!import_file_name.empty())
    {
        result.options.import_file = import_file_name;
    }
```

- [ ] **Step 4: Build + test**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "import subcommand" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the new case PASSES; full suite green.

- [ ] **Step 5: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): import subcommand (project + source file positionals)"
```

---

### Task 4: `run_import` + `main` dispatch

**Files:**
- Create: `app/run/import.hpp`, `app/run/import.cpp`
- Modify: `app/CMakeLists.txt` (add `run/import.cpp`)
- Modify: `app/main.cpp` (dispatch `Command::Import`)

**Interfaces:**
- Consumes: `import_asset` (Task 2); `Options` (Task 3).
- Produces: `int mosaic::app::run_import(const Options& options);`

- [ ] **Step 1: Write `app/run/import.hpp`**

```cpp
#pragma once

#include "app/cli/options.hpp"

namespace mosaic::app
{
// Handle `mosaic import <project> <file>`: cook the asset into the project. Returns an exit code.
int run_import(const Options& options);
} // namespace mosaic::app
```

- [ ] **Step 2: Write `app/run/import.cpp`**

```cpp
#include "app/run/import.hpp"

#include "app/asset/asset_io.hpp"
#include "foundation/logging/log.hpp"

namespace mosaic::app
{
int run_import(const Options& options)
{
    if (!options.import_project || !options.import_file)
    {
        mosaic::foundation::log::error("import", "usage: mosaic import <project> <file>");
        return 1;
    }
    return import_asset(*options.import_project, *options.import_file).has_value() ? 0 : 1;
}
} // namespace mosaic::app
```

- [ ] **Step 3: Wire the source into `app/CMakeLists.txt`**

Add `run/import.cpp` to the `mosaic_app` source list (next to `run/build.cpp`):
```cmake
  run/import.cpp
```

- [ ] **Step 4: Dispatch in `app/main.cpp`**

Add the include with the other app includes:
```cpp
#include "app/run/import.hpp"
```
Right after the `Command::Build` dispatch block, add:
```cpp
    if (pr.options.command == Command::Import)
    {
        return run_import(pr.options);
    }
```

- [ ] **Step 5: Build + full suite**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: green. Functional smoke:
```bash
P="$(mktemp -d)/g"; ./build/debug/app/mosaic create "$P" >/dev/null
printf 'P6\n2 1\n255\n' > /tmp/t5.ppm; printf '\xff\x00\x00\x00\xff\x00' >> /tmp/t5.ppm
./build/debug/app/mosaic import "$P" /tmp/t5.ppm; echo "import=$?"
ls "$P/assets"   # t5.image + t5.toml
```
Expected: `import=0`; `assets/` has `t5.image` + `t5.toml`.

- [ ] **Step 6: Commit**

```bash
git add app/run/import.hpp app/run/import.cpp app/CMakeLists.txt app/main.cpp
git commit -m "feat(app): mosaic import dispatch + run_import"
```

---

### Task 5: Load project assets at startup (run + editor)

**Files:**
- Modify: `app/main.cpp` (compute `assets_dir`; load in the window block)
- Modify: `app/run/editor.cpp` (load after `setup_runtime`)

**Interfaces:**
- Consumes: `load_project_assets` (Task 2); `Options::assets_dir` (Task 3).

- [ ] **Step 1: Compute `assets_dir` in `app/main.cpp`**

Add includes:
```cpp
#include <filesystem>
#include "app/asset/asset_io.hpp"
```
In the positional-resolution block (where `pr.options.open_path` resolves to `pr.options.scene`), after setting `pr.options.scene`, add — when the positional was a project/bundle directory, point at its `assets/`:
```cpp
        std::error_code aec;
        if (std::filesystem::is_directory(*pr.options.open_path, aec))
        {
            pr.options.assets_dir =
                (std::filesystem::path(*pr.options.open_path) / "assets").string();
        }
```

- [ ] **Step 2: Load in the `run --window` block — `app/main.cpp`**

Immediately after `auto render_system = setup_runtime(world, asset_registry).render_system;`, add:
```cpp
        if (options.window && pr.options.assets_dir)
        {
            const int n = load_project_assets(asset_registry, *pr.options.assets_dir);
            if (n > 0) { mosaic::foundation::log::info("import", "loaded {} project asset(s)", n); }
        }
```
(Use whichever `Options` binding is in scope there — the window block uses `options`/`pr.options`; the existing code references `options.window`. If `assets_dir` is on `options`, use `options.assets_dir`.)

- [ ] **Step 3: Load in the editor — `app/run/editor.cpp`**

Add the include:
```cpp
#include "app/asset/asset_io.hpp"
```
Immediately after `auto render_system = setup_runtime(world, asset_registry).render_system;`, add:
```cpp
    if (options.assets_dir)
    {
        const int n = load_project_assets(asset_registry, *options.assets_dir);
        if (n > 0) { mosaic::foundation::log::info("import", "loaded {} project asset(s)", n); }
    }
```

- [ ] **Step 4: Build, full suite, functional**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug 2>&1 | tail -3
P="$(mktemp -d)/g"; ./build/debug/app/mosaic create "$P" >/dev/null
printf 'P6\n2 1\n255\n' > /tmp/t5.ppm; printf '\xff\x00\x00\x00\xff\x00' >> /tmp/t5.ppm
./build/debug/app/mosaic import "$P" /tmp/t5.ppm >/dev/null; echo "import=$?"
timeout 30 ./build/debug/app/mosaic editor "$P" --ticks 3 2>/tmp/t5ed.err; echo "editor=$?"; grep -iE "error|validation|VUID" /tmp/t5ed.err || echo "(clean)"
grep -i "loaded .* project asset" /tmp/t5ed.err && echo "asset-load logged"
```
Expected: suite green; `editor=0`, no validation errors; the "loaded 1 project asset(s)" line appears (the imported asset registered at startup).

- [ ] **Step 5: Commit**

```bash
git add app/main.cpp app/run/editor.cpp
git commit -m "feat(app): load a project's assets/ into the registry at startup (run + editor)"
```

---

### Task 6: `build_bundle` copies `assets/`

**Files:**
- Modify: `app/project/bundle.hpp` (copy `assets/` into the bundle)
- Test: extend `tests/unit/app/bundle_test.cpp`

**Interfaces:**
- Consumes: nothing new; adds a filesystem copy step to `build_bundle`.

- [ ] **Step 1: Write the failing test — extend `tests/unit/app/bundle_test.cpp`**

Add (uses the file's existing `tmp`/`make_types`/`make_dummy_binary` helpers; it also needs `import_asset` + a PPM writer — add the include `#include "app/asset/asset_io.hpp"` and a small local PPM writer):

```cpp
TEST_CASE("build_bundle copies the project's assets into the bundle", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("aproj");
    const std::string out  = tmp("aout");
    const std::string bin  = tmp("abin") + ".exe";
    fs::remove_all(proj); fs::remove_all(out);
    REQUIRE(create_project(proj, types));
    make_dummy_binary(bin);

    // Import a tiny PPM so the project has an asset.
    const std::string ppm = tmp("appm") + ".ppm";
    {
        std::string s = "P6\n2 1\n255\n";
        const unsigned char px[] = {255, 0, 0, 0, 255, 0};
        s.append(reinterpret_cast<const char*>(px), sizeof(px));
        REQUIRE(mosaic::app::save_scene_file(ppm, s));
    }
    REQUIRE(mosaic::app::import_asset(proj, ppm).has_value());

    REQUIRE(build_bundle(proj, out, bin, types));
    REQUIRE(fs::exists(fs::path(out) / "assets" / "appm.image"));
    REQUIRE(fs::exists(fs::path(out) / "assets" / "appm.toml"));

    fs::remove_all(proj); fs::remove_all(out); fs::remove(bin); fs::remove(ppm);
}
```
Add `#include "app/asset/asset_io.hpp"` to the top of `bundle_test.cpp`. Build → FAIL (assets not copied).

- [ ] **Step 2: Add the assets-copy step to `app/project/bundle.hpp`**

After the manifest write (step 4, the `save_scene_file(... kManifestName ...)` block) and before the runtime-binary copy (step 5), insert:
```cpp
    // 4.5 Copy the project's assets/ tree (imported/cooked assets) if present.
    const fs::path assets_src = fs::path(project_dir) / "assets";
    if (fs::exists(assets_src, ec))
    {
        fs::copy(assets_src, out / "assets",
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) { return fail("could not copy assets/: " + assets_src.string()); }
    }
```

- [ ] **Step 3: Build + test (unit + full)**

```bash
cmake --build build/debug -j && ctest --test-dir build/debug -R "build_bundle copies" --output-on-failure
ctest --test-dir build/debug 2>&1 | tail -3
```
Expected: the new case PASSES; full suite green.

- [ ] **Step 4: Functional — the full pipeline end to end**

```bash
P="$(mktemp -d)/g"; ./build/debug/app/mosaic create "$P" >/dev/null
printf 'P6\n2 1\n255\n' > /tmp/t5.ppm; printf '\xff\x00\x00\x00\xff\x00' >> /tmp/t5.ppm
./build/debug/app/mosaic import "$P" /tmp/t5.ppm >/dev/null; echo "import=$?"
B="$(mktemp -d)/b"; ./build/debug/app/mosaic build "$P" -o "$B"; echo "build=$?"
ls "$B/assets"   # appm.image/.toml carried into the bundle
timeout 30 "$B/mosaic" run "$B" --window --ticks 3 2>/tmp/t5b.err; echo "bundle-run=$?"; grep -iE "error|validation|VUID" /tmp/t5b.err || echo "(clean)"
```
Expected: `import=0`, `build=0`, the bundle's `assets/` has the cooked image + metadata, and `bundle-run=0` with no validation errors (the bundle loads its own assets via its own copied binary).

- [ ] **Step 5: Commit**

```bash
git add app/project/bundle.hpp tests/unit/app/bundle_test.cpp
git commit -m "feat(app): build_bundle copies the project's assets/ into the bundle"
```

---

## Notes for the executor

- **`docs/project/next-steps.md`** — after Task 6, mark T5 complete and note the **Project & Tools Workflow pillar is COMPLETE (T1–T5)**; the remaining v0.1 pillar is **Scripting (Python)**. Commit on this branch (rides in the T5 PR).
- **Finishing:** use `superpowers:finishing-a-development-branch`; base branch is `main`.
- **Visual check to request from the user:** `mosaic create g && mosaic import g <some.ppm> && mosaic editor g` — the imported texture appears in the Assets panel and can be assigned to a Material; `mosaic build g -o dist && cd dist && ./run.sh` runs with the asset bundled.
- If a `SerializationError` / include path is off, copy the include the existing `app/run/scene_load.hpp` or `asset_metadata_test.cpp` uses.
```
