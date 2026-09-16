#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "app/plugin/git.hpp"

using mosaic::app::git_clone;
using mosaic::app::git_pull;
using mosaic::app::GitResult;
using mosaic::app::is_git_url;

namespace
{
namespace fs = std::filesystem;

fs::path make_fixture_dir(const std::string& tag)
{
    const fs::path dir = fs::temp_directory_path() / ("mosaic_git_test_" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// Runs a shell command for TEST FIXTURE SETUP ONLY. git.cpp itself never uses a
// shell (see its own comment) -- this is fixed, trusted test code building a
// throwaway fixture repo, not the production code path this suite exercises.
void run_fixture_command(const std::string& cmd)
{
    const int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
}

// A throwaway bare git repo with one commit (package.toml), reachable via a
// local ".git"-suffixed-looking path -- fully offline, no real remote host.
// Explicitly names and pins the bare repo's HEAD to "main" via symbolic-ref
// (rather than relying on git_clone/push default-branch behavior, which
// varies by the host's git version and init.defaultBranch config) so cloning
// it always checks out a working tree deterministically.
fs::path make_fixture_bare_repo(const std::string& tag)
{
    const fs::path bare = make_fixture_dir("bare_" + tag);
    run_fixture_command("git init --bare -q \"" + bare.string() + "\"");

    const fs::path work = make_fixture_dir("work_" + tag);
    run_fixture_command("git clone -q \"" + bare.string() + "\" \"" + work.string() + "\"");
    run_fixture_command("git -C \"" + work.string() + "\" checkout -q -b main");
    {
        std::ofstream f(work / "package.toml", std::ios::binary | std::ios::trunc);
        f << "name = \"fixture\"\nversion = \"1.0.0\"\ndescription = \"fixture package\"\n";
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

TEST_CASE("is_git_url classifies URLs and paths", "[app]")
{
    REQUIRE(is_git_url("https://github.com/example/repo.git"));
    REQUIRE(is_git_url("http://example.com/repo"));
    REQUIRE(is_git_url("git@github.com:example/repo.git"));
    REQUIRE(is_git_url("ssh://git@example.com/repo.git"));
    REQUIRE(is_git_url("/home/user/bare.git"));

    REQUIRE_FALSE(is_git_url("/home/user/packages"));
    REQUIRE_FALSE(is_git_url("./local-dir"));
    REQUIRE_FALSE(is_git_url("packages"));
}

TEST_CASE("is_git_url rejects a leading dash even if it otherwise looks like a URL", "[app]")
{
    REQUIRE_FALSE(is_git_url("-x.git"));
    REQUIRE_FALSE(is_git_url("--upload-pack=evil"));
    REQUIRE_FALSE(is_git_url("-ssh://example.com/repo.git"));
}

TEST_CASE("git_clone rejects a URL that looks like a git flag", "[app]")
{
    const fs::path dest = make_fixture_dir("clone_flag_like_url_dest");
    std::error_code ec;
    fs::remove_all(dest, ec);

    // "--" before the URL in git_clone's argv means this always fails as a bad
    // remote rather than being interpreted as a git option.
    const GitResult r = git_clone("--upload-pack=touch /tmp/pwned", dest);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("git_clone clones a real local repo into a fresh destination", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("clone_ok");
    const fs::path dest = make_fixture_dir("clone_ok_dest");
    std::error_code ec;
    fs::remove_all(dest, ec); // git_clone requires dest to not exist

    const GitResult r = git_clone(bare.string(), dest);
    REQUIRE(r.ok);
    REQUIRE(fs::exists(dest / "package.toml"));
}

TEST_CASE("git_clone fails cleanly against a nonexistent source", "[app]")
{
    const fs::path dest = make_fixture_dir("clone_bad_src_dest");
    std::error_code ec;
    fs::remove_all(dest, ec);

    const GitResult r = git_clone("/nonexistent/mosaic/bare.git", dest);
    REQUIRE_FALSE(r.ok);
    REQUIRE_FALSE(r.output.empty());
}

TEST_CASE("git_clone fails cleanly when the destination already exists", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("clone_dest_exists");
    const fs::path dest = make_fixture_dir("clone_dest_exists_dest"); // already exists

    const GitResult r = git_clone(bare.string(), dest);
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("git_pull is a no-op when nothing changed upstream", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("pull_noop");
    const fs::path dest = make_fixture_dir("pull_noop_dest");
    std::error_code ec;
    fs::remove_all(dest, ec);
    REQUIRE(git_clone(bare.string(), dest).ok);

    const GitResult r = git_pull(dest);
    REQUIRE(r.ok);
}

TEST_CASE("git_pull brings in a new commit pushed to the source afterward", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("pull_new_commit");
    const fs::path dest = make_fixture_dir("pull_new_commit_dest");
    std::error_code ec;
    fs::remove_all(dest, ec);
    REQUIRE(git_clone(bare.string(), dest).ok);

    const fs::path work2 = make_fixture_dir("pull_new_commit_work2");
    run_fixture_command("git clone -q \"" + bare.string() + "\" \"" + work2.string() + "\"");
    {
        std::ofstream f(work2 / "extra.txt", std::ios::binary | std::ios::trunc);
        f << "hello\n";
    }
    run_fixture_command("git -C \"" + work2.string() +
                       "\" -c user.email=test@example.com -c user.name=test add -A");
    run_fixture_command("git -C \"" + work2.string() +
                       "\" -c user.email=test@example.com -c user.name=test commit -q -m second");
    run_fixture_command("git -C \"" + work2.string() + "\" push -q origin HEAD:refs/heads/main");

    REQUIRE_FALSE(fs::exists(dest / "extra.txt"));
    const GitResult r = git_pull(dest);
    REQUIRE(r.ok);
    REQUIRE(fs::exists(dest / "extra.txt"));
}
