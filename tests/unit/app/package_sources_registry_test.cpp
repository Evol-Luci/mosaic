#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_sources_registry.hpp"

using mosaic::app::add_named_source;
using mosaic::app::list_named_sources;
using mosaic::app::load_sources_registry;
using mosaic::app::NamedSource;
using mosaic::app::remove_named_source;
using mosaic::app::resolve_source;
using mosaic::app::save_sources_registry;
using mosaic::app::SourcesRegistry;

namespace
{
std::filesystem::path make_fixture_registry_path(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("mosaic_sources_test_" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir / "sources.toml";
}

std::filesystem::path make_fixture_source_dir(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("mosaic_sources_srcdir_test_" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}
} // namespace

TEST_CASE("load_sources_registry on a missing file returns an empty registry", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("missing");
    const SourcesRegistry registry = load_sources_registry(path);
    REQUIRE(registry.sources.empty());
}

TEST_CASE("add_named_source then list_named_sources round-trips through the registry file", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("add_list");
    const std::filesystem::path dir  = make_fixture_source_dir("add_list");

    REQUIRE(add_named_source("mine", dir.string(), path));

    const std::vector<NamedSource> sources = list_named_sources(path);
    REQUIRE(sources.size() == 1);
    REQUIRE(sources[0].name == "mine");
    REQUIRE(sources[0].path == dir.string());
}

TEST_CASE("add_named_source refuses a nonexistent directory", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("add_bad_dir");
    REQUIRE_FALSE(add_named_source("mine", "/nonexistent/mosaic/source/dir", path));
    REQUIRE(list_named_sources(path).empty());
}

TEST_CASE("add_named_source refuses a duplicate name without overwriting", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("add_dup");
    const std::filesystem::path dir1 = make_fixture_source_dir("add_dup_1");
    const std::filesystem::path dir2 = make_fixture_source_dir("add_dup_2");

    REQUIRE(add_named_source("mine", dir1.string(), path));
    REQUIRE_FALSE(add_named_source("mine", dir2.string(), path));

    const std::vector<NamedSource> sources = list_named_sources(path);
    REQUIRE(sources.size() == 1);
    REQUIRE(sources[0].path == dir1.string()); // unchanged
}

TEST_CASE("remove_named_source removes a registered source", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("remove_basic");
    const std::filesystem::path dir  = make_fixture_source_dir("remove_basic");

    REQUIRE(add_named_source("mine", dir.string(), path));
    REQUIRE(remove_named_source("mine", path));
    REQUIRE(list_named_sources(path).empty());
}

TEST_CASE("remove_named_source on an unregistered name succeeds as a no-op", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("remove_noop");
    REQUIRE(remove_named_source("never-registered", path));
}

TEST_CASE("resolve_source returns the registered path for a known name", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("resolve_hit");
    const std::filesystem::path dir  = make_fixture_source_dir("resolve_hit");
    REQUIRE(add_named_source("mine", dir.string(), path));

    REQUIRE(resolve_source("mine", path) == dir.string());
}

TEST_CASE("resolve_source returns its input unchanged for an unregistered name", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("resolve_miss");
    REQUIRE(resolve_source("/some/literal/path", path) == "/some/literal/path");
}
