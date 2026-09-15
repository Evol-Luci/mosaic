#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_source.hpp"

using mosaic::app::find_package;
using mosaic::app::list_packages;
using mosaic::app::PackageInfo;

namespace
{
// A throwaway source directory under the test binary's CWD, unique per test run
// via Catch2's TEST_CASE name being distinct -- reused verbatim from this file's
// existing sibling tests' throwaway-directory convention (see project_create_test.cpp).
std::filesystem::path make_fixture_source(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("mosaic_pkgsrc_test_" + tag);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

void write_file(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << contents;
}
} // namespace

TEST_CASE("list_packages returns only valid packages, sorted by name", "[app]")
{
    const std::filesystem::path root = make_fixture_source("list_valid");

    std::filesystem::create_directories(root / "zeta");
    write_file(root / "zeta" / "package.toml",
              "name = \"zeta\"\nversion = \"1.0.0\"\ndescription = \"Z package\"\n");

    std::filesystem::create_directories(root / "alpha");
    write_file(root / "alpha" / "package.toml",
              "name = \"alpha\"\nversion = \"2.0.0\"\ndescription = \"A package\"\n");

    // Missing package.toml entirely -- skipped, not an error.
    std::filesystem::create_directories(root / "no-manifest");

    // Unparseable package.toml -- skipped, not an error.
    std::filesystem::create_directories(root / "broken");
    write_file(root / "broken" / "package.toml", "this is not valid toml {{{");

    const std::vector<PackageInfo> packages = list_packages(root.string());

    REQUIRE(packages.size() == 2);
    REQUIRE(packages[0].name == "alpha");
    REQUIRE(packages[0].version == "2.0.0");
    REQUIRE(packages[0].description == "A package");
    REQUIRE(packages[1].name == "zeta");
}

TEST_CASE("list_packages on a nonexistent source directory returns empty", "[app]")
{
    const std::vector<PackageInfo> packages = list_packages("/nonexistent/mosaic/packages/dir");
    REQUIRE(packages.empty());
}

TEST_CASE("find_package finds an existing package and misses a nonexistent one", "[app]")
{
    const std::filesystem::path root = make_fixture_source("find");
    std::filesystem::create_directories(root / "widget");
    write_file(root / "widget" / "package.toml",
              "name = \"widget\"\nversion = \"0.1.0\"\ndescription = \"A widget\"\n");

    const std::optional<PackageInfo> found = find_package(root.string(), "widget");
    REQUIRE(found.has_value());
    REQUIRE(found->name == "widget");

    const std::optional<PackageInfo> missing = find_package(root.string(), "does-not-exist");
    REQUIRE_FALSE(missing.has_value());
}

TEST_CASE("the bundled hello-plugin example package is discoverable", "[app]")
{
    const std::optional<PackageInfo> found =
        find_package(mosaic::app::default_package_source(), "hello-plugin");
    REQUIRE(found.has_value());
    REQUIRE(found->version == "0.1.0");
}
