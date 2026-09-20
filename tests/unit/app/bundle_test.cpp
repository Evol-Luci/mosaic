#include <filesystem>
#include <string>

#include <unistd.h> // ::getpid (unique temp dirs)

#include <catch2/catch_test_macros.hpp>

#include "app/asset/asset_io.hpp"
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
    // 1 ground, 4 dynamic physics objects, 1 camera, 1 orbiter, 1 fountain, 1 puffer, 1 point light, 1 decal = 11 transforms
    int transforms = 0;
    w.for_each<mosaic::engine::transform::Transform>(
        [&](World::EntityID, mosaic::engine::transform::Transform&) { transforms++; });
    return transforms;
}
} // namespace

TEST_CASE("build_bundle produces a runnable self-contained bundle", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("proj");
    const std::string out  = tmp("out");
    const std::string bin  = tmp("bin") + ".exe";
    fs::remove_all(proj); fs::remove_all(out);
    REQUIRE(create_project(proj));
    const std::string bin_bytes = make_dummy_binary(bin);

    REQUIRE(build_bundle(proj, out, bin, types));

    // Manifest copied + parses, same default scene.
    auto m = load_manifest(out);
    REQUIRE(m.has_value());
    REQUIRE(m->default_scene == "scenes/main.json");
    // Scene copied + loads to the 11-entity demo (8 + the fountain emitter + the scripted puffer + the decal).
    REQUIRE(fs::exists(fs::path(out) / "scenes" / "main.json"));
    REQUIRE(transform_count((fs::path(out) / "scenes" / "main.json").string(), types) == 11);
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
    REQUIRE(create_project(proj));
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

TEST_CASE("build_bundle refuses self-build and refuses a project dir as output", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string bin = tmp("bin4") + ".exe";
    make_dummy_binary(bin);

    // --- self-build: output == project dir ---
    const std::string proj = tmp("proj4");
    fs::remove_all(proj);
    REQUIRE(create_project(proj));

    REQUIRE_FALSE(build_bundle(proj, proj, bin, types));
    // Project must be intact.
    REQUIRE(fs::exists(fs::path(proj) / "mosaic.toml"));
    REQUIRE(fs::exists(fs::path(proj) / "scenes" / "main.json"));

    // --- project used as -o: refused as non-bundle, left untouched ---
    const std::string proj2 = tmp("proj4b");
    fs::remove_all(proj2);
    REQUIRE(create_project(proj2));

    REQUIRE_FALSE(build_bundle(proj, proj2, bin, types));
    // proj2's manifest must still be present.
    REQUIRE(fs::exists(fs::path(proj2) / "mosaic.toml"));

    fs::remove_all(proj); fs::remove_all(proj2); fs::remove(bin);
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
    REQUIRE_FALSE(fs::exists(out1));

    // Project whose default scene was deleted -> copy fails -> out rolled back.
    const std::string proj = tmp("proj3");
    fs::remove_all(proj);
    REQUIRE(create_project(proj));
    fs::remove(fs::path(proj) / "scenes" / "main.json");
    const std::string out2 = tmp("out3b");
    fs::remove_all(out2);
    REQUIRE_FALSE(build_bundle(proj, out2, bin, types));
    REQUIRE_FALSE(fs::exists(out2)); // rolled back

    fs::remove_all(notproj); fs::remove_all(proj); fs::remove(bin);
}

TEST_CASE("build_bundle copies the project's assets into the bundle", "[app]")
{
    TypeRegistry reg; ComponentReflection types = make_types(reg);
    const std::string proj = tmp("aproj");
    const std::string out  = tmp("aout");
    const std::string bin  = tmp("abin") + ".exe";
    fs::remove_all(proj); fs::remove_all(out);
    REQUIRE(create_project(proj));
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
    const std::string stem = fs::path(ppm).stem().string();
    REQUIRE(fs::exists(fs::path(out) / "assets" / (stem + ".image")));
    REQUIRE(fs::exists(fs::path(out) / "assets" / (stem + ".toml")));

    fs::remove_all(proj); fs::remove_all(out); fs::remove(bin); fs::remove(ppm);
}
