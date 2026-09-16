# Plugin packages

`mosaic plugin {list, install, remove}` manages reusable plugin bundles —
one or more `.py` files following the existing `MOSAIC_MENU_ITEMS`/
`MOSAIC_PANELS`/`MOSAIC_IMPORTERS` conventions — sourced from a local
directory and tracked per-project.

## Package format

A package is a directory containing `package.toml` (`name`, `version`,
`description`) plus one or more `*.py` files at its top level (not
recursive). The package manager never parses or validates the Python
content — `PluginHost`/`ImportPluginHost` do that, at load time, same as
any other file that ends up in a project's `plugins/`.

## Source resolution

A source is any local directory whose immediate subdirectories are
packages. The default source is the compile-time `MOSAIC_PACKAGES_DIR`
(`packages/` at the repo root, mirroring `MOSAIC_TEMPLATES_DIR`); `--source
<dir>` overrides it with any other local directory. This repo ships one
real example package, `packages/hello-plugin/`.

**Local only, for now:** `--source` is always a filesystem path in this
slice — no URL, no network fetch. This is a deliberate seam for later, not
a limitation baked in forever: a future slice could accept a URL in
`--source`, resolve it by downloading to a local cache directory, and hand
that path straight to the same local-source code described here, with no
redesign of `list_packages`/`install_package`/`remove_package` needed.

## Tracking installs

`mosaic.toml` gains an `installed_plugins` array of tables, one per
installed package: `name`, `version`, and `files` (the exact `.py`
filenames this package placed in `plugins/`). `install` refuses to
overwrite a `plugins/*.py` file it doesn't already own (per this record) —
a hand-written plugin file, or a different package's file, is never
clobbered. `remove` deletes only the files its own record lists.
Reinstalling the same package at the same version is a no-op; at a
different version, the package's own files are overwritten and the
recorded version updated — there's no dependency-constraint solver here,
just a record of what's currently present. A project's `installed_plugins`
key is present as an empty array (`installed_plugins = []`) once every
package has been removed, not omitted — this is the TOML backend's normal
empty-vector serialization, and an *older* project manifest that never had
the key at all still deserializes it to an empty vector for backward
compatibility.

A narrow, accepted risk: `install` isn't fully transactional — if copying a
package's files fails partway through (e.g. disk full), the files already
copied are left on disk without a matching manifest update, and a retry
will then refuse to overwrite them as "untracked." This requires a
mid-install I/O failure to trigger and needs manual cleanup to recover
from; no rollback/temp-dir-then-rename mechanism exists yet.

## Named sources: `mosaic plugin source {add, list, remove}`

Typing `--source <dir>` every time gets old once a user has their own
collection of packages. `mosaic plugin source add <name> <path>` registers
a friendly name for a local directory in a personal registry
(`~/.config/mosaic/sources.toml` — see
`docs/design/user-config-directory.md`); `mosaic plugin list --source
<name>` / `install --source <name>` then resolve that name through the
registry, falling back to treating the value as a literal path when no
name matches. `mosaic plugin source list`/`remove` manage the registry.
For a plain local-path source, Mosaic never runs git itself here — the user
clones/pulls their own source directory however they like; the registry only
remembers where it is. Git-backed named sources (added via
`docs/superpowers/specs/2026-09-15-git-backed-package-sources-design.md`) are
the exception: Mosaic runs `git clone`/`git pull` itself for those, via
`app/plugin/git.hpp`/`.cpp`.

## CLI naming: `mosaic plugin`, not `mosaic package`

`mosaic build`'s help text already reads "Package a project into a
standalone bundle" — `build` is the existing owner of "package" as a verb
in this CLI. The new subcommand is `mosaic plugin` instead, matching this
repo's existing term for the artifact being managed (`PluginHost`,
`plugins/`, `MOSAIC_MENU_ITEMS`).

## Non-goals

- Network fetch, a remote/community registry — future work; see the design
  spec's Non-goals for the full list.
- Version-constraint resolution ("package A requires package B >= 1.2") —
  not modeled; a package has a single version string.
- Templates as packages — templates keep their own, separate `--template`
  mechanism (`docs/design/project-templates.md`).

Full design: `docs/superpowers/specs/2026-09-15-plugin-package-manager-design.md`.
