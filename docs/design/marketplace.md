# Marketplace — planning (not yet scheduled for implementation)

**Status: design-only.** This document records decisions made while
thinking through a future public package marketplace and its review
pipeline. Nothing here is being built yet — it exists so the decisions
aren't lost before the team is ready to build it. One piece that fell out
of this conversation *was* scheduled separately and has since shipped:
plugin runtime sandboxing (restricting what *any* plugin can do, local or
marketplace-sourced, independent of review; see
`docs/design/plugin-sandboxing.md`). A second, git-backed package sources
(a convenience layer over the existing local `mosaic plugin` source
mechanism), has also since shipped — `mosaic plugin source add` now
clones a git URL directly; see
`docs/superpowers/specs/2026-09-15-git-backed-package-sources-design.md`.

## Goal

A path from today's fully local, single-user `mosaic plugin` package
manager (see `docs/design/plugin-packages.md`) toward a public,
GitHub-hosted marketplace for templates, plugins, and (eventually)
ranked/voted feature requests — without committing to hosting costs or
infrastructure before the project's actual scale justifies it, and without
compromising on a real moderation/safety story for code that will execute
inside the Studio editor process.

## Hosting shape: single curated monorepo (Option A)

Two shapes were considered:

- **Option A — single curated monorepo** (chosen): one public repo, e.g.
  `mosaic-marketplace`, laid out identically to the existing local
  `packages/<name>/package.toml` + files format already shipped. A
  submission is a PR adding a new package directory; moderation is GitHub's
  native PR review, gated by branch protection.
- **Option B — index + external repos** (rejected for now): a central repo
  holds only pointers to individually-maintained package repos elsewhere.
  More scalable for very large ecosystems, but weaker moderation (approving
  a URL isn't approving its future content) and more moving parts than this
  project needs yet.

Option A directly reuses the local package format with zero changes, and
its moderation gate is the same "propose via diff, only a human approves"
shape this project's Living Skill Registry already uses.

## Moderation gate

- Branch protection on `main`: at least one approving review required
  before merge.
- `CODEOWNERS` for `packages/**` names the moderator(s) as required
  reviewers — a PR cannot merge without their explicit approval.
- A CI check (GitHub Actions, free on public repos) validates structure
  before a human looks at it: does `package.toml` parse, does the name
  collide with an existing package, are files outside the expected shape.
  This filters malformed submissions cheaply so human review time is spent
  only on submissions worth reviewing.
- **Code safety for plugins specifically is a distinct, harder problem**
  from structural validation, because a plugin is executable Python that
  will run inside the Studio editor process. Investigating the current
  codebase found **zero existing sandboxing of any kind**: every plugin
  `.py` file (Studio menu items, panels, and asset importers alike) runs as
  fully-privileged Python in-process, with the complete standard library
  and normal builtins available — confirmed by reading
  `modules/script_python/python_script_engine.cpp`,
  `studio/plugin_host.cpp`, and `modules/script_python_studio/
  studio_plugin_script_engine.cpp`. A future review pipeline for plugin
  submissions should layer:
  1. **Static analysis (CI, cheap):** AST-scan for dangerous imports/calls
     (`os`, `subprocess`, `socket`, `ctypes`, `eval`, `exec`) before human
     review. A first-pass filter, not a guarantee — bypassable by
     obfuscation.
  2. **Dynamic sandboxed execution (CI, heavier):** actually run a
     submission in an isolated environment (container/VM, no network,
     restricted filesystem) against a mock World/harness, flagging
     suspicious behavior before human review. Real behavioral
     verification; a genuine build (isolation boundary + a
     realistic-enough plugin API harness + something watching for red
     flags) — not designed further here.
  These two layers are marketplace-review-pipeline concerns and remain
  unscheduled. A third layer — restricting what *any* plugin can do at
  runtime, regardless of source or review — was split out as its own
  near-term backlog item, since it's a real gap in already-shipped
  functionality, not just a future marketplace concern.

## Versioning: latest-only for now

Each package's `package.toml` already carries a `version` field. Rather
than tag-pinning every version bump (so users could install a specific
historical version), the marketplace starts **latest-only**: a package's
folder is overwritten in place on each new-version PR, and
`mosaic plugin install` always gets whatever's currently in `main`. Git
history technically preserves old code, but there's no supported "install
v1.2 specifically" path yet. Revisit if real demand for version pinning
shows up — YAGNI for an MVP with few packages and no dependents relying on
old versions.

## Feature requests: GitHub Discussions

Ranked/voted feature requests use GitHub Discussions' native "Ideas"
category (built-in upvoting, sorts by vote count) on the same
`mosaic-marketplace` repo — no custom infrastructure needed. A future
in-editor/web marketplace browser could surface top-voted requests via
GitHub's API; not required for this to work at MVP.

## Personal/private versioning shares the same shape

A user's own private package source doesn't need new code: it's the same
`package.toml` + files layout, in a private git repo (or even a local git
repo with no remote), pointed to via the existing `--source <dir>`
mechanism — `list_packages`/`install_package` only ever look at files on
disk, so a source directory that happens to be a git repo already works
today. The personal repo could later become, or fork into, the public one,
since the format is already shared. That convenience —
`mosaic plugin source add <name> <git-url>` clones directly, and
`mosaic plugin source pull <name>` refreshes it — has since shipped,
independent of this marketplace's (still-unscheduled) timeline.

## Non-goals (for this planning pass)

- Actually standing up the `mosaic-marketplace` repo, CI, or branch
  protection — not scheduled.
- Designing the dynamic sandboxed-execution CI pipeline in detail — noted
  as needed, not designed.
- An in-editor or web marketplace browsing client — depends on the backend
  existing first.
- Version pinning — deferred until real demand appears.
