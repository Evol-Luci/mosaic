# Git-backed Package Sources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `mosaic plugin source add <name> <value>` accept a git URL directly (cloning it under `~/.config/mosaic/sources/<name>/`), and add `mosaic plugin source pull <name>` to refresh it — closing the one manual step (clone/pull the source directory yourself) the local named-sources feature (PR #161) deliberately left outside the CLI.

**Architecture:** A new, isolated module (`app/plugin/git.hpp`/`.cpp`) is the only place in the codebase that spawns a subprocess — `fork()` + `execvp("git", …)`, never a shell, with `GIT_TERMINAL_PROMPT=0` so an auth prompt fails fast instead of hanging. `app/plugin/package_sources_registry.hpp` gains a `git_url` field on `NamedSource` (empty = today's plain local-path source) plus `add_named_source_from_git`/`pull_named_source`, and `remove_named_source` deletes a git-backed source's clone directory. Once cloned, a git-backed source is just a local directory — `resolve_source`/`list_packages`/`find_package`/`install_package` need no changes at all.

**Tech Stack:** C++20, CLI11, the project's own `foundation::serialization` TOML/reflection stack, POSIX (`fork`/`execvp`/`pipe`/`waitpid`), Catch2. The real `git` binary is invoked in both production code and tests — no mocking.

**Spec:** `docs/superpowers/specs/2026-09-15-git-backed-package-sources-design.md`

## Global Constraints

- Never invoke git through a shell (`system()`/`popen()`) — always `fork()` + `execvp()` with an explicit argv array. Both the URL and destination path are effectively user input.
- Every child process that runs `git` sets `GIT_TERMINAL_PROMPT=0` so an auth prompt fails immediately instead of hanging.
- Linux-only — no Windows process-spawning path. Matches this project's existing Linux-only precedents (e.g. `LiveLinkServer`).
- No credential storage or handling of any kind — whatever the user's own git/SSH-agent setup already provides is all that's supported.
- No version pinning — `add` clones the default branch, `pull` always fast-forwards it (matches `docs/design/marketplace.md`'s existing "latest-only for now" stance).
- All new registry/CLI functions follow this codebase's existing error convention: log via `mosaic::foundation::log::error`/`warn`/`info` with category `"plugin"`, return `bool`/`int`, never throw across a public boundary.

---

### Task 1: `NamedSource` gains a `git_url` field

**Files:**
- Modify: `app/plugin/package_sources_registry.hpp`
- Test: `tests/unit/app/package_sources_registry_test.cpp`

**Interfaces:**
- Produces: `NamedSource{std::string name; std::string path; std::string git_url;}` — `git_url == ""` means a plain local-path source (all existing behavior unchanged). Later tasks read/write this field.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/app/package_sources_registry_test.cpp` (after the existing includes/usings, alongside the other `TEST_CASE`s):

```cpp
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
```

Add `#include <fstream>` to the top of the file if not already present (it already is, per the existing file).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "NamedSource decodes git_url*"`
Expected: **compile failure** — `NamedSource` has no member `git_url` yet.

- [ ] **Step 3: Add the field**

In `app/plugin/package_sources_registry.hpp`, change:

```cpp
struct NamedSource
{
    std::string name;
    std::string path;
};

template <class V>
void reflect(V& v, NamedSource& s)
{
    v.member("name", s.name);
    v.member("path", s.path);
}
```

to:

```cpp
struct NamedSource
{
    std::string name;
    std::string path;
    std::string git_url; // empty = a plain local-path source (today's behavior)
};

template <class V>
void reflect(V& v, NamedSource& s)
{
    v.member("name", s.name);
    v.member("path", s.path);
    v.member("git_url", s.git_url);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS, including the existing `package_sources_registry_test.cpp` and `package_source_test.cpp` cases (no regressions — `ReadVisitor::member` leaves `git_url` at its default `""` when the key is present-but-blank or absent, and every existing call to `add_named_source` still only ever writes `name`/`path`, so `git_url` stays `""` for every source added the old way too).

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_sources_registry.hpp tests/unit/app/package_sources_registry_test.cpp
git commit -m "feat(app): add NamedSource::git_url, backward-compatible with existing sources.toml"
```

---

### Task 2: `app/plugin/git.hpp`/`.cpp` — the subprocess wrapper

**Files:**
- Create: `app/plugin/git.hpp`
- Create: `app/plugin/git.cpp`
- Modify: `app/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`
- Test: `tests/unit/app/git_test.cpp` (new)

**Interfaces:**
- Produces:
  - `bool mosaic::app::is_git_url(const std::string& value)`
  - `struct mosaic::app::GitResult { bool ok; std::string output; }`
  - `mosaic::app::GitResult mosaic::app::git_clone(const std::string& url, const std::filesystem::path& dest)`
  - `mosaic::app::GitResult mosaic::app::git_pull(const std::filesystem::path& repo_dir)`
  - These four are consumed by Tasks 3, 4, and 7.

- [ ] **Step 1: Write `app/plugin/git.hpp`**

```cpp
#pragma once

#include <filesystem>
#include <string>

namespace mosaic::app
{

// True if `value` looks like a git remote rather than a local filesystem path: a
// URL scheme (https://, http://, ssh://), the git@host: SSH shorthand, or a
// trailing ".git". Anything else is treated as a literal local path by the
// caller (mirrors resolve_source()'s existing "unrecognized value is just a
// path" precedent).
bool is_git_url(const std::string& value);

// The result of running one git subprocess: whether it exited zero, and its
// combined stdout+stderr (for error messages and logging -- callers don't need
// to know which stream a given line came from).
struct GitResult
{
    bool        ok = false;
    std::string output;
};

// Clones `url` into `dest`, which must not already exist. Runs with
// GIT_TERMINAL_PROMPT=0 so an auth prompt against a private repo fails fast
// instead of hanging forever on a prompt Mosaic has no terminal to show.
GitResult git_clone(const std::string& url, const std::filesystem::path& dest);

// Runs `git pull` inside `repo_dir`, which must already be a git working tree.
GitResult git_pull(const std::filesystem::path& repo_dir);

} // namespace mosaic::app
```

- [ ] **Step 2: Write `app/plugin/git.cpp`**

```cpp
#include "app/plugin/git.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace mosaic::app
{
namespace
{

// Runs `args` (args[0] must be "git") via fork()+execvp(), never through a
// shell -- both the URL and destination path passed to git_clone/git_pull are
// effectively user input, and system()/popen() pass their argument through
// /bin/sh -c, letting shell metacharacters (`, $(), ;, |) execute arbitrary
// commands. The child sets GIT_TERMINAL_PROMPT=0 so an auth prompt against a
// private repo fails immediately instead of hanging.
GitResult run_git(const std::vector<std::string>& args)
{
    std::array<int, 2> pipe_fds{-1, -1};
    if (pipe(pipe_fds.data()) != 0)
    {
        return GitResult{false, std::string("pipe() failed: ") + std::strerror(errno)};
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return GitResult{false, std::string("fork() failed: ") + std::strerror(errno)};
    }

    if (pid == 0)
    {
        // Child: redirect stdout+stderr into the pipe's write end, then exec git.
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        setenv("GIT_TERMINAL_PROMPT", "0", 1);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args)
        {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp("git", argv.data());
        _exit(127); // execvp only returns on failure
    }

    // Parent: read the child's combined output to EOF, then reap it.
    close(pipe_fds[1]);
    std::string           output;
    std::array<char, 4096> buf{};
    ssize_t n = 0;
    while ((n = read(pipe_fds[0], buf.data(), buf.size())) > 0)
    {
        output.append(buf.data(), static_cast<std::size_t>(n));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return GitResult{ok, output};
}

} // namespace

bool is_git_url(const std::string& value)
{
    static constexpr std::array<const char*, 4> kSchemes = {"https://", "http://", "ssh://", "git@"};
    for (const char* scheme : kSchemes)
    {
        if (value.rfind(scheme, 0) == 0)
        {
            return true;
        }
    }
    return value.size() > 4 && value.compare(value.size() - 4, 4, ".git") == 0;
}

GitResult git_clone(const std::string& url, const std::filesystem::path& dest)
{
    std::error_code ec;
    if (std::filesystem::exists(dest, ec))
    {
        return GitResult{false, "destination already exists: " + dest.string()};
    }
    return run_git({"git", "clone", url, dest.string()});
}

GitResult git_pull(const std::filesystem::path& repo_dir)
{
    return run_git({"git", "-C", repo_dir.string(), "pull"});
}

} // namespace mosaic::app
```

- [ ] **Step 3: Wire the new source file into the build**

In `app/CMakeLists.txt`, change:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
)
```

to:

```cmake
add_library(mosaic_app STATIC
  cli/options.cpp
  run/status_reporter.cpp
  run/editor.cpp
  run/create.cpp
  run/docs.cpp
  run/build.cpp
  run/import.cpp
  run/plugin.cpp
  asset/font_cook.cpp
  asset/import_plugins.cpp
  plugin/git.cpp
)
```

- [ ] **Step 4: Write the failing tests**

Create `tests/unit/app/git_test.cpp`:

```cpp
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

TEST_CASE("git_clone clones a real local repo into a fresh destination", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("clone_ok");
    const fs::path dest = make_fixture_dir("clone_ok_dest");
    fs::remove_all(dest, std::error_code{}); // git_clone requires dest to not exist

    const GitResult r = git_clone(bare.string(), dest);
    REQUIRE(r.ok);
    REQUIRE(fs::exists(dest / "package.toml"));
}

TEST_CASE("git_clone fails cleanly against a nonexistent source", "[app]")
{
    const fs::path dest = make_fixture_dir("clone_bad_src_dest");
    fs::remove_all(dest, std::error_code{});

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
    fs::remove_all(dest, std::error_code{});
    REQUIRE(git_clone(bare.string(), dest).ok);

    const GitResult r = git_pull(dest);
    REQUIRE(r.ok);
}

TEST_CASE("git_pull brings in a new commit pushed to the source afterward", "[app]")
{
    const fs::path bare = make_fixture_bare_repo("pull_new_commit");
    const fs::path dest = make_fixture_dir("pull_new_commit_dest");
    fs::remove_all(dest, std::error_code{});
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
```

In `tests/CMakeLists.txt`, find the line `unit/app/package_sources_registry_test.cpp` (part of the `add_executable(mosaic_unit_tests ...)` source list) and add a new line directly after it: `unit/app/git_test.cpp`.

- [ ] **Step 5: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"`
Expected: **build failure** — `app/plugin/git.hpp` doesn't exist yet (this step's Files section already created it in Steps 1-2, so if you're executing steps in order this will already build; run the test binary instead: `./build/debug/tests/mosaic_unit_tests "[app]" -c "is_git_url classifies URLs and paths"` and confirm it passes only once Steps 1-2 are in place — if executing strictly test-first, comment out Steps 1-2's content, confirm a link/compile failure, then restore them).

- [ ] **Step 6: Run tests to verify they pass**

Run: `./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS for all six new test cases, plus every pre-existing `[app]` case unaffected.

- [ ] **Step 7: Commit**

```bash
git add app/plugin/git.hpp app/plugin/git.cpp app/CMakeLists.txt tests/CMakeLists.txt tests/unit/app/git_test.cpp
git commit -m "feat(app): add git.hpp/.cpp, a fork+execvp wrapper around git clone/pull"
```

---

### Task 3: `add_named_source_from_git`

**Files:**
- Modify: `app/plugin/package_sources_registry.hpp`
- Test: `tests/unit/app/package_sources_registry_test.cpp`

**Interfaces:**
- Consumes: `mosaic::app::git_clone` (Task 2), `NamedSource::git_url` (Task 1).
- Produces: `bool mosaic::app::add_named_source_from_git(const std::string& name, const std::string& url, const std::filesystem::path& registry_path = sources_registry_path())` — consumed by Task 7's CLI dispatch.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/app/package_sources_registry_test.cpp`. First, add a local fixture helper near the top of the anonymous namespace (alongside `make_fixture_registry_path`/`make_fixture_source_dir`):

```cpp
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
```

Add `#include <cstdlib>` to the top of the file.

Then add the test cases, and add `add_named_source_from_git` to the `using` list:

```cpp
using mosaic::app::add_named_source_from_git;
```

```cpp
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"`
Expected: compile failure — `add_named_source_from_git` is not declared yet.

- [ ] **Step 3: Implement `add_named_source_from_git`**

In `app/plugin/package_sources_registry.hpp`, add `#include "app/plugin/git.hpp"` to the includes, and add this function after `add_named_source`:

```cpp
// Clones `url` into user_config_dir()/sources/<name>/ and registers it. Fails
// (nothing written to the registry, nothing left on disk) if `name` is
// already registered, or the clone destination already exists, or the clone
// itself fails.
inline bool add_named_source_from_git(
    const std::string& name, const std::string& url,
    const std::filesystem::path& registry_path = sources_registry_path())
{
    SourcesRegistry registry = load_sources_registry(registry_path);
    for (const NamedSource& s : registry.sources)
    {
        if (s.name == name)
        {
            mosaic::foundation::log::error(
                "plugin", "source '{}' is already registered (path: {})", name, s.path);
            return false;
        }
    }

    const std::filesystem::path dest = user_config_dir() / "sources" / name;
    const GitResult             result = git_clone(url, dest);
    if (!result.ok)
    {
        mosaic::foundation::log::error("plugin", "git clone of '{}' failed: {}", url, result.output);
        return false;
    }

    registry.sources.push_back(NamedSource{name, dest.string(), url});
    return save_sources_registry(registry, registry_path);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_sources_registry.hpp tests/unit/app/package_sources_registry_test.cpp
git commit -m "feat(app): add add_named_source_from_git"
```

---

### Task 4: `pull_named_source`

**Files:**
- Modify: `app/plugin/package_sources_registry.hpp`
- Test: `tests/unit/app/package_sources_registry_test.cpp`

**Interfaces:**
- Consumes: `mosaic::app::git_pull` (Task 2), `add_named_source_from_git` (Task 3, to set up a git-backed fixture source).
- Produces: `bool mosaic::app::pull_named_source(const std::string& name, const std::filesystem::path& registry_path = sources_registry_path())` — consumed by Task 7's CLI dispatch.

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/app/package_sources_registry_test.cpp` (add `pull_named_source` to the `using` list):

```cpp
using mosaic::app::pull_named_source;
```

```cpp
TEST_CASE("pull_named_source errors for an unregistered name", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("pull_missing");
    REQUIRE_FALSE(pull_named_source("never-registered", path));
}

TEST_CASE("pull_named_source errors for a registered but non-git source", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("pull_not_git");
    const std::filesystem::path dir  = make_fixture_source_dir("pull_not_git");
    REQUIRE(add_named_source("mine", dir.string(), path));

    REQUIRE_FALSE(pull_named_source("mine", path));
}

TEST_CASE("pull_named_source pulls a new commit into a git-backed source's clone", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("pull_ok");
    const std::filesystem::path bare = make_fixture_bare_repo("pull_ok");
    const std::filesystem::path config_home = make_fixture_source_dir("pull_ok_config_home");
    setenv("XDG_CONFIG_HOME", config_home.string().c_str(), 1);

    REQUIRE(add_named_source_from_git("mine", bare.string(), path));
    const std::filesystem::path clone_dir = list_named_sources(path)[0].path;

    // Push a second commit to the bare repo from a separate working clone.
    const std::filesystem::path work2 = make_fixture_source_dir("pull_ok_work2");
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

    REQUIRE_FALSE(std::filesystem::exists(clone_dir / "extra.txt"));
    REQUIRE(pull_named_source("mine", path));
    REQUIRE(std::filesystem::exists(clone_dir / "extra.txt"));

    unsetenv("XDG_CONFIG_HOME");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"`
Expected: compile failure — `pull_named_source` is not declared yet.

- [ ] **Step 3: Implement `pull_named_source`**

In `app/plugin/package_sources_registry.hpp`, add this function after `remove_named_source`:

```cpp
// Runs `git pull` on the named source's clone directory. Fails if `name`
// isn't registered, or is registered but not git-backed (git_url empty -- a
// plain local-path source has nothing to pull).
inline bool pull_named_source(
    const std::string& name, const std::filesystem::path& registry_path = sources_registry_path())
{
    const SourcesRegistry registry = load_sources_registry(registry_path);
    auto it = std::find_if(registry.sources.begin(), registry.sources.end(),
                           [&](const NamedSource& s) { return s.name == name; });
    if (it == registry.sources.end())
    {
        mosaic::foundation::log::error("plugin", "source '{}' is not registered", name);
        return false;
    }
    if (it->git_url.empty())
    {
        mosaic::foundation::log::error(
            "plugin", "source '{}' is not git-backed (path: {})", name, it->path);
        return false;
    }
    const GitResult result = git_pull(it->path);
    if (!result.ok)
    {
        mosaic::foundation::log::error(
            "plugin", "git pull for source '{}' failed: {}", name, result.output);
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_sources_registry.hpp tests/unit/app/package_sources_registry_test.cpp
git commit -m "feat(app): add pull_named_source"
```

---

### Task 5: `remove_named_source` deletes a git-backed source's clone directory

**Files:**
- Modify: `app/plugin/package_sources_registry.hpp`
- Test: `tests/unit/app/package_sources_registry_test.cpp`

**Interfaces:**
- Consumes: `add_named_source_from_git` (Task 3, to set up a git-backed fixture source).
- Produces: no signature change to `remove_named_source` — same `bool remove_named_source(const std::string&, const std::filesystem::path&)`.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/app/package_sources_registry_test.cpp`:

```cpp
TEST_CASE("remove_named_source deletes a git-backed source's clone directory", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("remove_git");
    const std::filesystem::path bare = make_fixture_bare_repo("remove_git");
    const std::filesystem::path config_home = make_fixture_source_dir("remove_git_config_home");
    setenv("XDG_CONFIG_HOME", config_home.string().c_str(), 1);

    REQUIRE(add_named_source_from_git("mine", bare.string(), path));
    const std::filesystem::path clone_dir = list_named_sources(path)[0].path;
    REQUIRE(std::filesystem::exists(clone_dir));

    REQUIRE(remove_named_source("mine", path));
    REQUIRE(list_named_sources(path).empty());
    REQUIRE_FALSE(std::filesystem::exists(clone_dir));

    unsetenv("XDG_CONFIG_HOME");
}

TEST_CASE("remove_named_source on a plain local source never touches the source directory", "[app]")
{
    const std::filesystem::path path = make_fixture_registry_path("remove_plain_untouched");
    const std::filesystem::path dir  = make_fixture_source_dir("remove_plain_untouched");
    REQUIRE(add_named_source("mine", dir.string(), path));

    REQUIRE(remove_named_source("mine", path));
    REQUIRE(std::filesystem::exists(dir)); // unchanged -- this is the user's own directory
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "remove_named_source deletes a git-backed*"`
Expected: FAIL — `remove_named_source` doesn't delete anything yet, so the clone directory still exists after removal.

- [ ] **Step 3: Update `remove_named_source`**

In `app/plugin/package_sources_registry.hpp`, change:

```cpp
inline bool remove_named_source(const std::string& name,
                                const std::filesystem::path& registry_path = sources_registry_path())
{
    SourcesRegistry registry = load_sources_registry(registry_path);
    auto it = std::find_if(registry.sources.begin(), registry.sources.end(),
                           [&](const NamedSource& s) { return s.name == name; });
    if (it == registry.sources.end())
    {
        mosaic::foundation::log::info("plugin", "source '{}' is not registered", name);
        return true;
    }
    registry.sources.erase(it);
    return save_sources_registry(registry, registry_path);
}
```

to:

```cpp
// Removes `name` if present. Not registered is not an error -- returns true, a
// no-op. For a git-backed source, also deletes its clone directory (Mosaic's
// own artifact, not the user's data -- see docs/superpowers/specs/
// 2026-09-15-git-backed-package-sources-design.md for why this differs from a
// plain local-path source, whose directory is never touched). A deletion
// failure only logs a warning -- the registry write is the operation that
// matters, matching add_named_source's existing precedent.
inline bool remove_named_source(const std::string& name,
                                const std::filesystem::path& registry_path = sources_registry_path())
{
    SourcesRegistry registry = load_sources_registry(registry_path);
    auto it = std::find_if(registry.sources.begin(), registry.sources.end(),
                           [&](const NamedSource& s) { return s.name == name; });
    if (it == registry.sources.end())
    {
        mosaic::foundation::log::info("plugin", "source '{}' is not registered", name);
        return true;
    }
    const bool        is_git_backed = !it->git_url.empty();
    const std::string clone_path    = it->path;
    registry.sources.erase(it);
    const bool saved = save_sources_registry(registry, registry_path);
    if (is_git_backed)
    {
        std::error_code ec;
        std::filesystem::remove_all(clone_path, ec);
        if (ec)
        {
            mosaic::foundation::log::warn(
                "plugin", "could not remove clone directory {}: {}", clone_path, ec.message());
        }
    }
    return saved;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "[app]"`
Expected: PASS, including the pre-existing `remove_named_source removes a registered source` and `remove_named_source on an unregistered name succeeds as a no-op` cases.

- [ ] **Step 5: Commit**

```bash
git add app/plugin/package_sources_registry.hpp tests/unit/app/package_sources_registry_test.cpp
git commit -m "fix(app): remove_named_source deletes a git-backed source's clone directory"
```

---

### Task 6: CLI parsing — `plugin source pull` and git-URL-aware help text

**Files:**
- Modify: `app/cli/options.hpp`
- Modify: `app/cli/options.cpp`
- Test: `tests/unit/app/options_test.cpp`

**Interfaces:**
- Produces: `Command::PluginSourcePull`, and `Options::source_name` populated for it (reusing the existing field). Consumed by Task 7.

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/app/options_test.cpp`, right after the existing `"plugin source remove subcommand parses name"` test case:

```cpp
TEST_CASE("plugin source pull subcommand parses name", "[cli]")
{
    auto result = parse_args({"mosaic", "plugin", "source", "pull", "mine"});
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.options.command == mosaic::app::Command::PluginSourcePull);
    REQUIRE(result.options.source_name == "mine");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)"`
Expected: compile failure — `Command::PluginSourcePull` doesn't exist yet.

- [ ] **Step 3: Add the command and parsing**

In `app/cli/options.hpp`, change:

```cpp
enum class Command { Run, Editor, Create, Build, Import, Docs, PluginList, PluginInstall, PluginRemove,
                    PluginSourceAdd, PluginSourceList, PluginSourceRemove };
```

to:

```cpp
enum class Command { Run, Editor, Create, Build, Import, Docs, PluginList, PluginInstall, PluginRemove,
                    PluginSourceAdd, PluginSourceList, PluginSourceRemove, PluginSourcePull };
```

In `app/cli/options.cpp`, change:

```cpp
    CLI::App* plugin_source_add =
        plugin_source->add_subcommand("add", "Register a named local package source");
    std::string plugin_source_add_name;
    std::string plugin_source_add_path;
    plugin_source_add->add_option("name", plugin_source_add_name, "Source name")->required();
    plugin_source_add->add_option("path", plugin_source_add_path, "Local directory path")->required();

    CLI::App* plugin_source_list =
        plugin_source->add_subcommand("list", "List registered package sources");

    CLI::App* plugin_source_remove =
        plugin_source->add_subcommand("remove", "Remove a registered package source");
    std::string plugin_source_remove_name;
    plugin_source_remove->add_option("name", plugin_source_remove_name, "Source name")->required();
```

to:

```cpp
    CLI::App* plugin_source_add = plugin_source->add_subcommand(
        "add", "Register a named package source (a local directory, or a git URL to clone)");
    std::string plugin_source_add_name;
    std::string plugin_source_add_path;
    plugin_source_add->add_option("name", plugin_source_add_name, "Source name")->required();
    plugin_source_add
        ->add_option("path", plugin_source_add_path, "Local directory path, or a git URL")
        ->required();

    CLI::App* plugin_source_list =
        plugin_source->add_subcommand("list", "List registered package sources");

    CLI::App* plugin_source_remove =
        plugin_source->add_subcommand("remove", "Remove a registered package source");
    std::string plugin_source_remove_name;
    plugin_source_remove->add_option("name", plugin_source_remove_name, "Source name")->required();

    CLI::App* plugin_source_pull = plugin_source->add_subcommand(
        "pull", "Fetch the latest commit for a git-backed package source");
    std::string plugin_source_pull_name;
    plugin_source_pull->add_option("name", plugin_source_pull_name, "Source name")->required();
```

Then, in the block that sets `result.options.command`/`source_name` from the parsed subcommands (find `if (plugin_source_remove->parsed())`), add:

```cpp
    if (plugin_source_pull->parsed())
    {
        result.options.command = Command::PluginSourcePull;
    }
```

right after the existing `plugin_source_remove->parsed()` block, and add:

```cpp
    if (!plugin_source_pull_name.empty())
    {
        result.options.source_name = plugin_source_pull_name;
    }
```

right after the existing `plugin_source_remove_name`-to-`source_name` assignment.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build/debug --target mosaic_unit_tests -j"$(nproc)" && ./build/debug/tests/mosaic_unit_tests "[cli]"`
Expected: PASS, including every pre-existing `[cli]` case.

- [ ] **Step 5: Commit**

```bash
git add app/cli/options.hpp app/cli/options.cpp tests/unit/app/options_test.cpp
git commit -m "feat(cli): parse mosaic plugin source pull"
```

---

### Task 7: Wire `run_plugin_source_add`/`run_plugin_source_pull` and update `run_plugin_source_list`

**Files:**
- Modify: `app/run/plugin.hpp`
- Modify: `app/run/plugin.cpp`
- Modify: `app/main.cpp`

**Interfaces:**
- Consumes: `is_git_url`/`GitResult` (Task 2), `add_named_source_from_git`/`pull_named_source` (Tasks 3-4), `Command::PluginSourcePull` (Task 6).
- Produces: `int run_plugin_source_pull(const Options&)`, consumed by `app/main.cpp`'s dispatch.

This task has no new automated test file: matching this codebase's existing convention, the thin `run_plugin_*` CLI-dispatch wrappers in `app/run/plugin.cpp` (`run_plugin_list`, `run_plugin_install`, `run_plugin_remove` — none of which have a dedicated test file today) are exercised manually via the real CLI rather than unit-tested; the logic they dispatch to (`is_git_url`, `add_named_source_from_git`, `pull_named_source`) is already fully covered by Tasks 2-4. This task ends with a manual end-to-end CLI verification instead.

- [ ] **Step 1: Add the declaration**

In `app/run/plugin.hpp`, add after the existing `run_plugin_source_remove` declaration:

```cpp
int run_plugin_source_pull(const Options& options);
```

- [ ] **Step 2: Wire the dispatch and the new command**

In `app/run/plugin.cpp`, add `#include "app/plugin/git.hpp"` to the includes, then change:

```cpp
int run_plugin_source_add(const Options& options)
{
    if (!options.source_name || !options.source_path)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin source add <name> <path>");
        return 1;
    }
    return add_named_source(*options.source_name, *options.source_path) ? 0 : 1;
}
```

to:

```cpp
int run_plugin_source_add(const Options& options)
{
    if (!options.source_name || !options.source_path)
    {
        mosaic::foundation::log::error(
            "plugin", "usage: mosaic plugin source add <name> <path-or-git-url>");
        return 1;
    }
    if (is_git_url(*options.source_path))
    {
        return add_named_source_from_git(*options.source_name, *options.source_path) ? 0 : 1;
    }
    return add_named_source(*options.source_name, *options.source_path) ? 0 : 1;
}
```

and change:

```cpp
int run_plugin_source_list(const Options&)
{
    const std::vector<NamedSource> sources = list_named_sources();
    if (sources.empty())
    {
        mosaic::foundation::log::info("plugin", "no package sources registered");
        return 0;
    }
    for (const NamedSource& s : sources)
    {
        std::cout << s.name << " -> " << s.path << "\n";
    }
    return 0;
}
```

to:

```cpp
int run_plugin_source_list(const Options&)
{
    const std::vector<NamedSource> sources = list_named_sources();
    if (sources.empty())
    {
        mosaic::foundation::log::info("plugin", "no package sources registered");
        return 0;
    }
    for (const NamedSource& s : sources)
    {
        std::cout << s.name << " -> " << s.path;
        if (!s.git_url.empty())
        {
            std::cout << " (git: " << s.git_url << ")";
        }
        std::cout << "\n";
    }
    return 0;
}
```

and add, after `run_plugin_source_remove`:

```cpp
int run_plugin_source_pull(const Options& options)
{
    if (!options.source_name)
    {
        mosaic::foundation::log::error("plugin", "usage: mosaic plugin source pull <name>");
        return 1;
    }
    return pull_named_source(*options.source_name) ? 0 : 1;
}
```

- [ ] **Step 3: Dispatch the new command in `main.cpp`**

In `app/main.cpp`, find the block:

```cpp
    if (pr.options.command == Command::PluginSourceRemove)
    {
        return run_plugin_source_remove(pr.options);
    }
```

and add directly after it:

```cpp
    if (pr.options.command == Command::PluginSourcePull)
    {
        return run_plugin_source_pull(pr.options);
    }
```

- [ ] **Step 4: Build**

Run: `cmake --build build/debug --target mosaic -j"$(nproc)"`
Expected: builds cleanly.

- [ ] **Step 5: Manual end-to-end CLI verification**

Run each of these in order (uses a scratch `XDG_CONFIG_HOME` so this never touches your real `~/.config/mosaic`):

```bash
export MOSAIC_DEMO_CONFIG=$(mktemp -d)
export XDG_CONFIG_HOME="$MOSAIC_DEMO_CONFIG"

# Set up a throwaway bare repo with one package, entirely offline.
BARE=$(mktemp -d)/demo.git
git init --bare -q "$BARE"
WORK=$(mktemp -d)
git clone -q "$BARE" "$WORK"
git -C "$WORK" checkout -q -b main
mkdir -p "$WORK/hello-plugin"
cat > "$WORK/hello-plugin/package.toml" <<'EOF'
name = "hello-plugin"
version = "1.0.0"
description = "demo"
EOF
git -C "$WORK" -c user.email=demo@example.com -c user.name=demo add -A
git -C "$WORK" -c user.email=demo@example.com -c user.name=demo commit -q -m init
git -C "$WORK" push -q origin HEAD:refs/heads/main
git --git-dir="$BARE" symbolic-ref HEAD refs/heads/main

# Exercise the CLI.
./build/debug/app/mosaic plugin source add demo "$BARE"
./build/debug/app/mosaic plugin source list
./build/debug/app/mosaic plugin list --source demo
./build/debug/app/mosaic plugin source pull demo
./build/debug/app/mosaic plugin source remove demo
./build/debug/app/mosaic plugin source list

unset XDG_CONFIG_HOME
```

Expected:
- `plugin source add demo "$BARE"` exits 0.
- `plugin source list` prints `demo -> <config-home>/mosaic/sources/demo (git: <bare-path>)`.
- `plugin list --source demo` prints `hello-plugin 1.0.0 - demo`.
- `plugin source pull demo` exits 0 (a no-op pull, nothing new upstream).
- `plugin source remove demo` exits 0, and `plugin source list` afterward prints `no package sources registered`.
- The clone directory (`<config-home>/mosaic/sources/demo`) no longer exists after removal — confirm with `ls "$MOSAIC_DEMO_CONFIG/mosaic/sources/"` (should not list `demo`, or the `sources` dir may not exist at all).

If any step's output doesn't match, stop and diagnose before continuing — this is the only end-to-end check this task gets.

- [ ] **Step 6: Commit**

```bash
git add app/run/plugin.hpp app/run/plugin.cpp app/main.cpp
git commit -m "feat(cli): wire mosaic plugin source add's git-URL dispatch and source pull"
```

---

### Task 8: Update project documentation

**Files:**
- Modify: `docs/design/marketplace.md`
- Modify: `docs/project/next-steps.md`

**Interfaces:** None — documentation only.

- [ ] **Step 1: Update `docs/design/marketplace.md`**

In the intro paragraph, change:

> Two pieces that fell out of this conversation *are* scheduled now, as their own separate backlog items (not part of this doc's scope): git-backed package sources (a convenience layer over the existing local `mosaic plugin` source mechanism), and plugin runtime sandboxing (restricting what *any* plugin can do, local or marketplace-sourced, independent of review).

to:

> One piece that fell out of this conversation *was* scheduled separately and has since shipped: plugin runtime sandboxing (restricting what *any* plugin can do, local or marketplace-sourced, independent of review; see `docs/design/plugin-sandboxing.md`). A second, git-backed package sources (a convenience layer over the existing local `mosaic plugin` source mechanism), has also since shipped — `mosaic plugin source add` now clones a git URL directly; see `docs/superpowers/specs/2026-09-15-git-backed-package-sources-design.md`.

And in the "Personal/private versioning shares the same shape" section, change:

> The one missing convenience — `mosaic plugin source add/pull <name> <git-url>` style named-source management, instead of manually tracking clone paths — is scheduled separately as "git-backed package sources," independent of this marketplace's timeline.

to:

> That convenience — `mosaic plugin source add <name> <git-url>` clones directly, and `mosaic plugin source pull <name>` refreshes it — has since shipped, independent of this marketplace's (still-unscheduled) timeline.

- [ ] **Step 2: Append a next-steps.md entry**

Prepend a new entry to the top of `docs/project/next-steps.md`'s running log (following the file's existing convention: the newest entry leads, prior entries follow after a `¶` separator). Read the file's current opening lines first with `sed -n '1,10p' docs/project/next-steps.md` to get the exact current leading text to preserve after your new entry, then update line 7 (the entry line) to prepend:

```
**Last updated:** 2026-09-15 (**Git-backed package sources — `mosaic plugin source add <name> <git-url>` / `pull`** — the one convenience `docs/design/marketplace.md` deliberately left unscheduled: `add` now auto-detects a git URL (`https://`, `http://`, `ssh://`, `git@`, or a trailing `.git`) and clones it into `~/.config/mosaic/sources/<name>/` via a new `app/plugin/git.hpp`/`.cpp` module -- the first subprocess-execution precedent in this codebase (`fork()`+`execvp("git", ...)`, never a shell, `GIT_TERMINAL_PROMPT=0` so a private repo's auth prompt fails fast instead of hanging). A new `mosaic plugin source pull <name>` refreshes a git-backed source's clone; `remove` now also deletes the clone directory for a git-backed source (a plain local-path source's directory is never touched, unchanged). `NamedSource` gained a `git_url` field (empty = today's plain local-path source), fully backward-compatible with an existing `sources.toml` with no migration needed. No version pinning (`add`/`pull` always track the default branch, matching the marketplace doc's existing "latest-only for now" stance) and no credential handling of any kind -- whatever the user's own git/SSH-agent setup provides is all that's supported. Headless-tested against a real, throwaway local bare repo (`git init --bare` in a temp fixture, no real remote host, no network) rather than mocking git. Full design: `docs/superpowers/specs/2026-09-15-git-backed-package-sources-design.md`.) ¶ (
```

(Note: paste this immediately before the previous "Last updated" entry's content, replacing just the `**Last updated:** 2026-09-15 (` opening of the prior entry with a `¶ (` continuation, matching the file's existing chain-of-entries format exactly as the last several entries already do.)

- [ ] **Step 3: Commit**

```bash
git add docs/design/marketplace.md docs/project/next-steps.md
git commit -m "docs: record git-backed package sources as shipped"
```

---

## Self-Review Notes

- **Spec coverage:** every decision in the design spec has a corresponding task — data model (Task 1), the subprocess wrapper incl. `is_git_url`/`GIT_TERMINAL_PROMPT=0`/never-a-shell (Task 2), `add_named_source_from_git` (Task 3), `pull_named_source` (Task 4), `remove_named_source`'s clone-deletion (Task 5), CLI auto-detection and the new `pull` command (Tasks 6-7), and the marketplace.md/next-steps.md updates the spec's own framing implies (Task 8).
- **Placeholder scan:** no TBD/TODO; every step has real, complete code or an exact shell transcript.
- **Type consistency:** `GitResult`, `is_git_url`, `git_clone`, `git_pull` (Task 2) are used with identical signatures in Tasks 3, 4, and 7. `NamedSource::git_url` (Task 1) is read/written identically in Tasks 3, 4, 5, and 7's `run_plugin_source_list` update.
