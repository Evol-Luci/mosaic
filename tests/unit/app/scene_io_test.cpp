#include <cstdio>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/run/scene_io.hpp"

using mosaic::app::load_scene_file;
using mosaic::app::save_scene_file;

namespace
{
std::string temp_path(const char* tag)
{
    return std::string(std::tmpnam(nullptr)) + tag + ".json";
}
} // namespace

TEST_CASE("save_scene_file then load_scene_file round-trips the contents", "[app]")
{
    const std::string path = temp_path("_sio");
    const std::string body = R"({"entities":[]})";
    REQUIRE(save_scene_file(path, body));

    const auto read = load_scene_file(path);
    REQUIRE(read.has_value());
    REQUIRE(*read == body);
    std::remove(path.c_str());
}

TEST_CASE("load_scene_file returns nullopt for a missing file", "[app]")
{
    REQUIRE_FALSE(load_scene_file("/no/such/mosaic/scene_xyz.json").has_value());
}

TEST_CASE("save_scene_file returns false for an unwritable path", "[app]")
{
    REQUIRE_FALSE(save_scene_file("/no/such/dir/scene.json", "{}"));
}
