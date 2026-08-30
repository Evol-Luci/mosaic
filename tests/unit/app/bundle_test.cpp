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
