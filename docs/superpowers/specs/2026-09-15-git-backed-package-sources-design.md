# Git-backed package sources — design

## Goal

`mosaic plugin source add <name> <path>` already lets a user register a
friendly name for a local directory of packages
(`~/.config/mosaic/sources.toml`, shipped in PR #161). The one
inconvenience it deliberately left unsolved: the user has to clone and
`git pull` their own source directory by hand, outside Mosaic, before
pointing `add` at it. This feature closes that gap — `add` accepts a
git URL directly, Mosaic clones it into a managed location, and a new
`pull` command refreshes it — without requiring the user to leave the
`mosaic plugin source` CLI surface.

This was carved out of `docs/design/marketplace.md` (a design-only,
unscheduled planning doc) as its own separately-scheduled backlog item,
independent of that document's much larger, still-unscheduled public
marketplace plan.

## Why this is architectural, not bounded

The change touches an existing command (`mosaic plugin source add`)
and could look like a small addition. It's treated as architectural
because it crosses a line this codebase has never crossed:
**subprocess execution.** A grep across `foundation/`, `engine/`,
`modules/`, `app/`, and `studio/` for `popen`, `std::system`, `fork(`,
`execve`, or `subprocess` returns nothing — every previous slice that
touched an adjacent temptation (the marketplace plan itself, the
plugin package manager) explicitly scoped itself to stop short of
running any external process. This feature is the first to actually
need one, so the decisions here (how git is invoked, where its output
goes, how failures and hangs are handled) set a precedent future
subprocess-invoking code in this codebase will likely follow or react
against.

## Decisions

### Shell out to the user's `git`, not a vendored library

Two shapes were considered:

- **Shell out to `git`** (chosen) — the user's own installed `git`
  binary, invoked via `fork()` + `execvp()`. No new vendored
  dependency; every failure mode is just git's own stderr, which any
  developer already knows how to read.
- **Vendor libgit2** (rejected) — no external binary dependency, but a
  real vendored C library with SSH/HTTPS transport and credential
  callbacks — a much larger surface than this feature needs, and a
  poor match for a project whose only two vendored C dependencies so
  far (ENet, Box3D) are both narrowly-scoped, single-purpose
  libraries.

### Never through a shell

`git.cpp` (see below) never calls `system()` or `popen()` — both pass
their argument through `/bin/sh -c`, which means a URL or path
containing shell metacharacters (`` ` ``, `$()`, `;`, `|`, ...) could
execute arbitrary shell code. Both the git URL and the destination
path are effectively user input (a URL a user pastes from wherever
they found a package source), so this isn't a theoretical concern.
Instead, `git_clone`/`git_pull` always `fork()` + `execvp("git", argv)`
with an explicit, fixed argv array — the same category of hardening
this codebase just finished applying to plugin sandboxing
(`docs/design/plugin-sandboxing.md`), applied here at the process
boundary instead of the Python one.

### `GIT_TERMINAL_PROMPT=0` in the child's environment

Without this, `git clone` against a private repo the user isn't
authenticated for blocks indefinitely on an interactive password
prompt — invisible to Mosaic, which has no terminal to show it, so the
CLI would appear to simply hang forever. Setting this env var in the
forked child (never touching the parent's environment) makes git fail
immediately with a clean, capturable error instead.

### Cache location: alongside `sources.toml`, not a separate cache dir

Clones live at `~/.config/mosaic/sources/<name>/`
(`$XDG_CONFIG_HOME/mosaic/sources/<name>/` if set), next to the
registry file itself. The stricter XDG-correct answer would put
regenerable clone data under a cache directory
(`~/.cache/mosaic/...`), but this codebase has no cache-dir concept
yet, and introducing one as a side effect of this feature would be a
second new directory concept riding along with the first
(`user_config_dir()`, PR #161). One user-level directory holding both
the registry and what it points to is simpler to explain and to find.

### `add` auto-detects a git URL; no separate verb

`mosaic plugin source add <name> <value>` stays one command.
`is_git_url(value)` recognizes `https://`, `http://`, `git@`,
`ssh://`, or a trailing `.git` — anything else is treated exactly as
today, as a literal local path. This mirrors the existing
`resolve_source()` precedent (an unrecognized `--source` value is
just treated as a path, no separate flag needed) rather than adding a
second command surface for what's conceptually the same operation
(register a named source).

### Updating is an explicit `pull`, never implicit

`mosaic plugin source pull <name>` is a new, separate command. `list`
and `install` never silently fetch over the network first. This
matches this project's established explicit-opt-in stance for
anything that does background network I/O (Live-Link's toggle
defaults to `Off` for the same reason) — a command that looks local
(`mosaic plugin list`) should never surprise the user with a hang or a
failure that's actually about network access, not about listing
packages.

### `remove` deletes the clone directory too

For a git-backed source, `mosaic plugin source remove <name>` calls
`fs::remove_all` on its clone directory after removing the registry
entry. This is a deliberate difference from a plain local-path source
(where `remove` has only ever forgotten the registry entry, never
touched the user's own directory) — the clone directory is Mosaic's
own artifact, created by Mosaic, not the user's data. Leaving it
behind would let orphaned clones accumulate silently under
`~/.config/mosaic/sources/` with nothing pointing at them. A deletion
failure (permissions, a file in use) logs a warning but does not fail
the command — the registry entry is gone either way, matching
`add_named_source`'s existing "the registry write is the operation
that matters" precedent.

## Data model change

`NamedSource` (`app/plugin/package_sources_registry.hpp`) gains one
field:

```cpp
struct NamedSource
{
    std::string name;
    std::string path;     // local dir; the clone dir if git-backed
    std::string git_url;  // empty = a plain local-path source (today's behavior)
};
```

This needs no migration and no `std::optional`: `ReadVisitor::member`
(`foundation/serialization/reflect_value.hpp`) silently leaves a field
at its default-constructed value when the corresponding TOML key is
absent, and a vector element is default-constructed before decoding
(`out.resize(arr.size())` in `decode`'s vector branch) — so an
existing `sources.toml` written before this feature, with no
`git_url` key at all, deserializes every entry with `git_url == ""`,
which is exactly the "not git-backed" sentinel this design already
needs. Confirmed by reading the actual decode path rather than
assumed.

## New module: `app/plugin/git.hpp` / `git.cpp`

The only place `fork`/`execvp` appear in the codebase — mirrors how
`modules/render_vulkan` is the only place Vulkan headers appear, or
how ENet symbols never leave `enet_transport.cpp`.

```cpp
namespace mosaic::app
{
// https://, http://, git@, ssh://, or a trailing ".git" -- anything else
// is treated as a literal local path by the caller.
bool is_git_url(const std::string& value);

struct GitResult
{
    bool        ok;
    std::string output; // combined stdout+stderr, for error messages and logging
};

// Clones `url` into `dest` (which must not already exist). Runs with
// GIT_TERMINAL_PROMPT=0 so an auth prompt fails fast instead of hanging.
GitResult git_clone(const std::string& url, const std::filesystem::path& dest);

// Runs `git pull` inside `repo_dir` (must already be a git working tree).
GitResult git_pull(const std::filesystem::path& repo_dir);
}
```

Implementation: `fork()`, and in the child, set
`GIT_TERMINAL_PROMPT=0` via `setenv` before `execvp("git", argv)` with
argv built as a fixed `{"git", "clone", url.c_str(), dest.c_str(),
nullptr}` (or `{"git", "-C", repo_dir.c_str(), "pull", nullptr}`) —
never through `sh -c`. The parent redirects the child's stdout/stderr
into a pipe (both merged into one stream — a user reading `GitResult
::output` on failure doesn't care which stream a given line came from)
read to EOF, then `waitpid`s and reports `ok = (exit code == 0)`.

Linux-only, matching this project's existing Linux-only precedents
(`LiveLinkServer`'s POSIX sockets) — no Windows process-spawning path
is written or claimed.

## Registry changes

`app/plugin/package_sources_registry.hpp` gains:

```cpp
// Clones `url` into user_config_dir()/sources/<name>/ and registers it.
// Fails (nothing written, nothing left on disk) if `name` is already
// registered, or if the clone destination already exists, or if the clone
// itself fails.
bool add_named_source_from_git(const std::string& name, const std::string& url,
                               const std::filesystem::path& registry_path = sources_registry_path());

// Runs `git pull` on the named source's clone directory. Fails if `name`
// isn't registered, or is registered but not git-backed (git_url empty).
bool pull_named_source(const std::string& name,
                       const std::filesystem::path& registry_path = sources_registry_path());
```

`remove_named_source` gains the clone-directory-deletion behavior
described above; its signature is unchanged.

`resolve_source()`, `list_packages()`, `find_package()`, and
`install_package()` need **zero changes** — once cloned, a git-backed
source is just a local directory like any other; every downstream
consumer of `NamedSource::path` already only ever reads it as a
filesystem path.

## CLI changes

`app/cli/options.hpp`/`.cpp`: `run_plugin_source_add` (`app/run/
plugin.cpp`) checks `is_git_url(*options.source_path)` and dispatches
to `add_named_source_from_git` or the existing `add_named_source`
accordingly — one command, no new parsing branch needed beyond that
check. A new `Command::PluginSourcePull` (parsed the same way
`PluginSourceRemove` is: one positional, the name) plus a new
`run_plugin_source_pull`. `run_plugin_source_list`'s output line gains
a conditional suffix: `name -> path (git: url)` when `git_url` is
non-empty, `name -> path` otherwise (unchanged for existing sources).

## Testing

No mocking git — every test that clones or pulls invokes the real
`git` binary, matching how N6 tested `EnetTransport` against real
loopback UDP rather than a fake transport. A test fixture creates a
throwaway bare repo with `git init --bare` in a temp directory (one
commit added via a normal working clone, then pushed back), then
clones from it via a plain filesystem path ending in `.git` — fully
offline, deterministic, no network dependency, no real remote host
involved.

Covered:
- `is_git_url`: true for `https://…`, `http://…`, `git@host:path`,
  `ssh://…`, anything ending `.git`; false for a plain path
  (`/home/user/packages`, `./local-dir`, `packages`).
- `git_clone` against the fixture bare repo: succeeds, destination
  contains the repo's files; fails cleanly (ok=false, non-empty
  output) against a nonexistent source URL; fails cleanly if `dest`
  already exists.
- `git_pull` against a clone of the fixture repo: succeeds as a no-op
  when nothing changed upstream; after a second commit is pushed to
  the bare repo, pulling brings the new file into the clone.
- `add_named_source_from_git`: registers on a successful clone; leaves
  the registry untouched on a clone failure; refuses (no clone
  attempted) if `name` is already registered, matching
  `add_named_source`'s existing precedent.
- `pull_named_source`: errors for an unregistered name; errors for a
  registered-but-not-git-backed name; succeeds and updates the clone
  for a git-backed one.
- `remove_named_source`: unchanged behavior for a plain source (only
  the local-path unit test from PR #161, re-run to confirm no
  regression); for a git-backed source, the clone directory is gone
  from disk after removal.
- CLI-level: `run_plugin_source_add` dispatches to the git path when
  given a URL and the local path when given a directory (two thin
  dispatch tests, mirroring the existing `[cli]` test style).

Not covered, deliberately: real network access to a real remote host
(GitHub, etc.) — every test uses a local, offline fixture repo, the
same tradeoff every other headless test in this codebase already
makes for anything that would otherwise need real infrastructure.

## Non-goals

- Authentication beyond whatever the user's own `git`/SSH-agent/
  credential-helper setup already provides. Mosaic never asks for or
  stores credentials — `GIT_TERMINAL_PROMPT=0` only prevents a silent
  hang; a repo genuinely requiring interactive auth still fails
  cleanly (this is correct: an unattended CLI shouldn't be typing
  passwords into a prompt it can't see).
- Version pinning to a specific commit/tag/branch — `add` always
  clones the default branch, `pull` always fast-forwards it. Matches
  `docs/design/marketplace.md`'s existing "latest-only for now" call
  for the same reason (YAGNI until real demand shows up).
- Windows/macOS process-spawning. This slice is Linux-only, matching
  existing precedent elsewhere in this codebase.
- The public marketplace itself, its moderation pipeline, or plugin
  runtime sandboxing for git-sourced (vs. any other) plugins — all
  separately scoped in `docs/design/marketplace.md`,
  `docs/design/plugin-sandboxing.md`, and unaffected by this slice.
