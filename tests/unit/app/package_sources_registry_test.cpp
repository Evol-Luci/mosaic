#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/package_sources_registry.hpp"

using mosaic::app::add_named_source;
using mosaic::app::add_named_source_from_git;
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

// A throwaway bare git repo with one commit, for exercising the git-backed
// registry functions without any real remote host. Duplicated here rather
// than shared with tests/unit/app/git_test.cpp's identical helper, matching
// this file's own existing per-file fixture-helper convention.
void run_fixture_command(const std::string& cmd)
{
    const int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
}

std::filesystem::path make_fixture_bare_repo(const std::string& tag)
{
    namespace fs = std::filesystem;
    const fs::path bare = fs::temp_directory_path() / ("mosaic_sources_bare_" + tag);
    std::error_code ec;
    fs::remove_all(bare, ec);
    fs::create_directories(bare, ec);
    run_fixture_command("git init --bare -q \"" + bare.string() + "\"");

    const fs::path work = fs::temp_directory_path() / ("mosaic_sources_work_" + tag);
    fs::remove_all(work, ec);
    run_fixture_command("git clone -q \"" + bare.string() + "\" \"" + work.string() + "\"");
    run_fixture_command("git -C \"" + work.string() + "\" checkout -q -b main");
    {
        std::ofstream f(work / "package.toml", std::ios::binary | std::ios::trunc);
        f << "name = \"fixture\"\nversion = \"1.0.0\"\ndescription = \"fixture\"\n";
    }
    run_fixture_command("git -C \"" + work.string() +
                       "\" -c user.email=test@example.com -c user.name=test add -A");
    run_fixture_command("git -C \"" + work.string() +
                       "\" -c user.email=test@example.com -c user.name=test commit -q -m init");
    run_fixture_command("git -C \"" + work.string() + "\" push -q origin HEAD:refs/heads/main");
    run_fixture_command("git --git-dir=\"" + bare.string() + "\" symbolic-ref HEAD refs/heads/main");
    return bare;
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

TEST_CASE("NamedSource decodes git_url as empty when the field is absent from an older sources.toml", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("legacy_no_git_url");
    const std::filesystem::path dir  = make_fixture_source_dir("legacy_no_git_url");

    // Write a sources.toml in the pre-git_url shape (as PR #161 shipped it) directly,
    // bypassing add_named_source, to prove decode tolerates the missing key.
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f << "[[sources]]\n"
             "name = \"mine\"\n"
             "path = \""
          << dir.string() << "\"\n";
    }

    const std::vector<NamedSource> sources = list_named_sources(path);
    REQUIRE(sources.size() == 1);
    REQUIRE(sources[0].name == "mine");
    REQUIRE(sources[0].git_url.empty());
}

TEST_CASE("add_named_source_from_git clones and registers a git-backed source", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("git_add_ok");
    const std::filesystem::path bare = make_fixture_bare_repo("git_add_ok");

    // Point user_config_dir()'s clone destination somewhere test-local by
    // overriding XDG_CONFIG_HOME for the duration of this call.
    const std::filesystem::path config_home = make_fixture_source_dir("git_add_ok_config_home");
    setenv("XDG_CONFIG_HOME", config_home.string().c_str(), 1);

    REQUIRE(add_named_source_from_git("mine", bare.string(), path));

    const std::vector<NamedSource> sources = list_named_sources(path);
    REQUIRE(sources.size() == 1);
    REQUIRE(sources[0].name == "mine");
    REQUIRE(sources[0].git_url == bare.string());
    REQUIRE(std::filesystem::exists(std::filesystem::path(sources[0].path) / "package.toml"));

    unsetenv("XDG_CONFIG_HOME");
}

TEST_CASE("add_named_source_from_git refuses a duplicate name without cloning", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("git_add_dup");
    const std::filesystem::path dir  = make_fixture_source_dir("git_add_dup");
    const std::filesystem::path bare = make_fixture_bare_repo("git_add_dup");

    REQUIRE(add_named_source("mine", dir.string(), path));
    REQUIRE_FALSE(add_named_source_from_git("mine", bare.string(), path));

    const std::vector<NamedSource> sources = list_named_sources(path);
    REQUIRE(sources.size() == 1);
    REQUIRE(sources[0].path == dir.string()); // unchanged, still the original local source
}

TEST_CASE("add_named_source_from_git leaves the registry untouched when the clone fails", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("git_add_bad_url");
    const std::filesystem::path config_home = make_fixture_source_dir("git_add_bad_url_config_home");
    setenv("XDG_CONFIG_HOME", config_home.string().c_str(), 1);

    REQUIRE_FALSE(add_named_source_from_git("mine", "/nonexistent/mosaic/bare.git", path));
    REQUIRE(list_named_sources(path).empty());

    unsetenv("XDG_CONFIG_HOME");
}
