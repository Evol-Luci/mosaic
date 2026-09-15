# Plugin Package Manager (local-only) — Design

## Goal (Phase 5, Tool Ecosystem pillar — package manager, the last piece of
## the remaining sequence: templates → doc tooling → package manager)

Give a project a way to install, list, and remove reusable **plugin
packages** — bundles of one or more `.py` plugin files (the existing
`MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`/`MOSAIC_IMPORTERS` conventions) plus a
small manifest — from a local, directory-based source, recording what's
installed in the project's own `mosaic.toml`. Explicitly scoped to **local
paths only** for this slice: no network fetch, no URL source, no remote
registry. The design leaves an obvious seam for a future network-fetch
backend (a `--source` that resolves a URL by downloading to a local cache
first, then reusing the exact same local-install code path) without
requiring a redesign now — per explicit direction: local only for now, built
so it can grow into a "community" package-sharing story later without
painting this slice into a corner.

## Why "plugin" packages, not templates or something broader

The two existing extensible surfaces in this repo are project **templates**
(`templates/<name>/`, selected once at `mosaic create --template <name>`
time) and **plugins** (`plugins/*.py` inside an existing project, discovered
by `PluginHost`/`ImportPluginHost` every time Studio or `mosaic import`
runs). Templates already have their own selection mechanism at creation
time — there's no meaningful "install a template into an existing project"
operation to manage. Plugins are the better fit for an actual **package
manager**: they're added to a project on an ongoing basis, after creation,
which is exactly the workflow a package manager exists to support. This
slice scopes to plugin packages only; templates-as-packages is explicit
future work if the community-sharing idea extends there later.

## Naming: `mosaic plugin`, not `mosaic package`

`mosaic build`'s own help text already reads "Package a project into a
standalone bundle" — CLI11's `build` subcommand is the existing owner of the
word "package" as a verb in this tool. Reusing "package" for the new
subcommand would collide in a genuinely confusing way (`mosaic package
install foo` vs. what `build` already means). The new subcommand is named
`mosaic plugin` instead, with `list`/`install`/`remove` sub-subcommands —
"plugin" is already this repo's term for the artifact being managed
(`PluginHost`, `plugins/`, `MOSAIC_MENU_ITEMS`), so this reads naturally:
"install a plugin" is unambiguous where "install a package" would need
disambiguating from `build`. The design doc and spec still use "package
manager"/"package" when describing the *concept* (a versioned, distributable
unit) since that's the accurate general term; only the CLI verb changes.

## Package format

A package is a directory containing:
- `package.toml` — a small manifest: `name`, `version`, `description`.
- One or more `*.py` files at the package directory's top level (not
  recursive) — each an ordinary Studio/import plugin file, following the
  existing `MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`/`MOSAIC_IMPORTERS`
  conventions untouched. The package manager never parses or validates
  their Python content — that's `PluginHost`/`ImportPluginHost`'s job, at
  load time, same as any other file that ends up in `plugins/`.

Example, a package named `hello-plugin`:
```
packages/hello-plugin/
  package.toml
  hello.py
```
```toml
name = "hello-plugin"
version = "0.1.0"
description = "A minimal example plugin package."
```

## Local package source

A **source** is any local directory whose immediate subdirectories are
packages (each with its own `package.toml`). This repo ships one at
`packages/` (top-level, sibling to `templates/`), resolved via a new
compile-time `MOSAIC_PACKAGES_DIR` define in `app/CMakeLists.txt`,
mirroring the existing `MOSAIC_FONT_DIR`/`MOSAIC_TEMPLATES_DIR`/
`MOSAIC_DOCS_DIR` pattern — used as the default source when `--source` is
omitted. `--source <dir>` overrides it with any other local directory (a
user's own cloned collection of community packages, for instance) — this is
the seam a future network backend would extend: a later slice could accept
a URL in `--source`, fetch it to a local temp/cache directory, and hand that
path to the exact same local-source code this slice implements, so nothing
here needs to change shape later.

## CLI: `mosaic plugin {list, install, remove}`

- **`mosaic plugin list [--source <dir>]`** — prints every package found
  directly under the source directory (default `MOSAIC_PACKAGES_DIR`): name,
  version, description, one per line. A source directory that doesn't exist,
  or contains no valid packages, prints nothing and exits 0 (an empty list
  is not an error — mirrors `create_project`'s "no plugins/ folder is the
  common case, not an error" precedent). A subdirectory missing
  `package.toml` or with an unparseable one is skipped silently (not a
  malformed-source error) with a logged warning per skipped entry, the same
  fail-soft-per-item posture `PluginHost::load` already uses for individual
  broken plugin files.

- **`mosaic plugin install <name> <project_dir> [--source <dir>]`** — looks
  up `<name>` under the source, copies every `*.py` file from the package
  directory into `<project_dir>/plugins/` (creating `plugins/` if it doesn't
  exist yet), and records the install in `<project_dir>/mosaic.toml`'s new
  `installed_plugins` array (see Manifest below). Fails (exit 1, nothing
  written) if: `<project_dir>` isn't a Mosaic project (no readable
  `mosaic.toml`); `<name>` isn't found under the source; or any of the
  package's `.py` filenames already exists in `<project_dir>/plugins/`
  **and isn't already tracked as belonging to this same package** — this
  guards a hand-written plugin file (or a different package) from being
  silently clobbered, matching `create_project`'s "refuse rather than
  overwrite" precedent. Installing a package that's already installed at
  the same version is a no-op that still exits 0 (files already match); at
  a different version, files are overwritten (that package's own files,
  never someone else's) and the manifest entry's version is updated —
  this slice has no dependency resolution or version *constraints*, just a
  record of what's currently present.

- **`mosaic plugin remove <name> <project_dir>`** — deletes the `.py` files
  this package's manifest entry says it owns from `<project_dir>/plugins/`,
  then removes the `installed_plugins` entry. Not an error if `<name>` isn't
  installed (already-removed is not a failure state, mirroring `git rm`-ish
  idempotency other tools in this repo don't currently need but this one
  benefits from, since "did I already remove this" is a natural question).
  Never touches a `.py` file in `plugins/` that the manifest doesn't
  attribute to this package.

No `update`/`upgrade` subcommand in this slice — re-running `install` with a
newer source package covers the same need without inventing a second
code path, and there's no version-constraint solver here to make "update"
mean something more than "reinstall."

## Manifest: `ProjectManifest` gains `installed_plugins`

```cpp
struct InstalledPlugin
{
    std::string name;    // package name
    std::string version; // package version at install time
    std::vector<std::string> files; // filenames this package owns in plugins/
};

template <class V>
void reflect(V& v, InstalledPlugin& p)
{
    v.member("name", p.name);
    v.member("version", p.version);
    v.member("files", p.files);
}
```
`ProjectManifest` gains `std::vector<InstalledPlugin> installed_plugins;`
reflected as `v.member("installed_plugins", m.installed_plugins);`. This
round-trips through the existing TOML backend unchanged — `encode()`
already handles `std::vector<Reflectable>` generically (confirmed by
reading `foundation/serialization/reflect_value.hpp`), and the TOML backend
encodes any `Array` value generically (confirmed by reading
`foundation/serialization/toml_backend.cpp`) — no serialization-layer
changes are needed. `files` (not just re-deriving "every `.py` in the
package directory" at install time) is stored explicitly so `remove` knows
exactly what to delete even if the source package's own file list changes
between install and removal.

An existing `mosaic.toml` with no `installed_plugins` key deserializes with
an empty vector (already how missing/absent fields behave elsewhere in this
codebase's TOML round-trip) — this is backward compatible with every
project created before this slice.

## Files

- Create: `app/plugin/package_source.hpp` — header-only core: `struct
  PackageInfo { std::string name, version, description; std::filesystem::path dir; }`,
  `std::vector<PackageInfo> list_packages(const std::string& source_dir)`,
  `std::optional<PackageInfo> find_package(const std::string& source_dir, const std::string& name)`.
- Create: `app/plugin/package_install.hpp` — header-only core: `bool
  install_package(const std::string& project_dir, const PackageInfo& pkg)`,
  `bool remove_package(const std::string& project_dir, const std::string& name)`,
  both operating in terms of `ProjectManifest`/`load_manifest`/existing
  manifest-save code (reuse, don't duplicate, the save path
  `create_project`/`bundle.hpp` already use).
- Modify: `app/project/project.hpp` — add `InstalledPlugin` struct + its
  `reflect()`, add `installed_plugins` field + member call to
  `ProjectManifest`.
- Create: `app/run/plugin.hpp`, `app/run/plugin.cpp` — CLI glue for the
  three sub-subcommands, mirroring `app/run/import.cpp`'s shape (a project-
  scoped subcommand with sub-arguments).
- Modify: `app/cli/options.hpp` — new `Command::PluginList`,
  `Command::PluginInstall`, `Command::PluginRemove`; new `Options` fields:
  `plugin_name` (optional string), `plugin_project` (optional string,
  reuses the existing "positional project dir" shape other subcommands
  already have), `package_source` (optional string, `--source`).
- Modify: `app/cli/options.cpp` — `mosaic plugin` subcommand with 3 nested
  CLI11 subcommands (`list`, `install`, `remove`), each with its own
  positional/optional arguments per the CLI section above.
- Modify: `app/main.cpp` — dispatch the 3 new `Command::Plugin*` values to
  three functions, `run_plugin_list`/`run_plugin_install`/`run_plugin_remove`,
  all declared in `app/run/plugin.hpp` and defined in `app/run/plugin.cpp` —
  one file for the whole `mosaic plugin` subcommand family (it's one
  cohesive feature), but a separate function per sub-subcommand rather than
  one function switching internally, matching how `app/run/create.cpp`,
  `build.cpp`, and `import.cpp` are each a single function with a single
  responsibility.
- Modify: `app/CMakeLists.txt` — `run/plugin.cpp` added to sources,
  `MOSAIC_PACKAGES_DIR="${CMAKE_SOURCE_DIR}/packages"` compile definition.
- Create: `packages/hello-plugin/package.toml`, `packages/hello-plugin/hello.py`
  — one minimal, real, checked-in example package (a single
  `MOSAIC_MENU_ITEMS` entry printing a message via `imgui_text`-free
  `print`/log, kept deliberately trivial) so `list`/`install`/`remove` have
  something real to exercise end-to-end, both in tests and by hand.
- Create: `tests/unit/app/package_source_test.cpp`,
  `tests/unit/app/package_install_test.cpp`.
- Modify: `tests/unit/app/options_test.cpp`, `tests/CMakeLists.txt`.
- Modify: `docs/design/project-templates.md` or a new
  `docs/design/plugin-packages.md` (decided: new file, since this is a
  distinct mechanism from templates, not an extension of that doc) —
  document the package format, source resolution, and the manifest
  extension, mirroring how `project-templates.md`/`documentation-tooling.md`
  already document their own mechanisms.
- Modify: `docs/project/next-steps.md` — new dated entry.

## Testing

- `package_source_test.cpp`: `list_packages` against a fixture directory
  with 2 valid packages + 1 missing-`package.toml` subdirectory + 1
  unparseable-`package.toml` subdirectory — asserts exactly the 2 valid
  ones are returned, in some defined order (alphabetical by name, for
  determinism), and the 2 broken ones are silently skipped (not thrown).
  `list_packages` against a nonexistent source directory returns empty, not
  an error. `find_package` returns the right one / `nullopt` for a name
  that doesn't exist.
- `package_install_test.cpp`: install into a fresh project — `plugins/`
  created, `.py` files copied byte-for-byte, `mosaic.toml` gains the
  `installed_plugins` entry with the right `files` list. Install into a
  project where a same-named `.py` already exists and is untracked — fails,
  nothing written (verify via before/after directory listing + manifest
  read). Install twice at the same version — second call is a no-op,
  succeeds. Remove an installed package — files deleted, manifest entry
  gone, an *unrelated* hand-written `.py` file in the same `plugins/`
  directory is untouched. Remove a package that was never installed —
  succeeds (not an error).
- `options_test.cpp`: `mosaic plugin list`, `mosaic plugin install <name>
  <project>`, `mosaic plugin remove <name> <project>` each parse into their
  respective `Command::Plugin*` value with the right fields populated.

## Non-goals / follow-ups

- **Network fetch** — explicitly deferred; `--source` stays a local
  filesystem path only in this slice. The seam described above (resolve a
  URL to a local cache directory, then reuse this slice's code unchanged)
  is the intended extension point when that's picked up.
- **Version constraints / dependency resolution** — a package has a single
  version string recorded at install time; there's no solver, no "package A
  requires package B >= 1.2." `install` on an already-installed package at
  a different version just overwrites, it doesn't check compatibility with
  anything else.
- **A remote/community registry** — the "community vibe" motivating this
  work is a future destination, not something this slice builds; this slice
  only makes the local mechanics (format, manifest, CLI) exist so a
  registry has something to plug into later.
- **Templates as packages** — templates keep their existing, separate
  `--template` selection mechanism; unifying the two concepts (if it ever
  makes sense) is explicit future work, not started here.
- **Dependency-of-a-dependency / transitive packages** — a package here is
  a flat bundle of `.py` files; a package "depending on" another package
  isn't a concept this slice introduces.
