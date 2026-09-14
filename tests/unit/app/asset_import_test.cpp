#include <filesystem>
#include <optional>
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

#include "engine/script/script_source.hpp"

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

void write_py(const std::string& path)
{
    REQUIRE(save_scene_file(path, "def on_update(dt):\n    pass\n"));
}

void write_plugin(const std::string& project_dir, const std::string& filename,
                  const std::string& source)
{
    const fs::path dir = fs::path(project_dir) / "plugins";
    std::error_code ec;
    fs::create_directories(dir, ec);
    REQUIRE(save_scene_file((dir / filename).string(), source));
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
    const std::string stem = fs::path(src).stem().string();
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".image")));
    REQUIRE(fs::exists(fs::path(proj) / "assets" / (stem + ".toml")));

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

TEST_CASE("import_asset refuses to overwrite an asset on a stem collision", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj_collide");
    fs::remove_all(proj);
    REQUIRE(create_project(proj, types));

    // Two source files that share a stem ("tex") in different directories.
    const fs::path dir_a = fs::path(tmp("cola"));
    const fs::path dir_b = fs::path(tmp("colb"));
    fs::remove_all(dir_a); fs::remove_all(dir_b);
    fs::create_directories(dir_a); fs::create_directories(dir_b);
    const std::string src_a = (dir_a / "tex.ppm").string();
    const std::string src_b = (dir_b / "tex.ppm").string();
    write_ppm(src_a); write_ppm(src_b);

    auto first = import_asset(proj, src_a);
    REQUIRE(first.has_value());
    const fs::path meta = fs::path(proj) / "assets" / "tex.toml";
    const std::optional<std::string> before = mosaic::app::load_scene_file(meta.string());
    REQUIRE(before.has_value());

    // The colliding second import must refuse and leave the first asset untouched
    // (no silently orphaned AssetID).
    auto second = import_asset(proj, src_b);
    REQUIRE_FALSE(second.has_value());
    const std::optional<std::string> after = mosaic::app::load_scene_file(meta.string());
    REQUIRE(after.has_value());
    REQUIRE(*after == *before);

    fs::remove_all(proj); fs::remove_all(dir_a); fs::remove_all(dir_b);
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
    REQUIRE(meta_text->find("type = 'csv'") != std::string::npos);

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

