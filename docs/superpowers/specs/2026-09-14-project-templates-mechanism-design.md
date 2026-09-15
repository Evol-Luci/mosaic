# Project Templates Mechanism — Design

## Goal (Phase 5, Tool Ecosystem pillar — first of the remaining Phase 5 sub-projects)

Let `mosaic create <path> --template <name>` scaffold a new project from a named,
file-based template instead of always generating the same hardcoded demo scene.
This is the *mechanism* only: template **selection, discovery, and copying**. It
ships with one real template (`default`, a checked-in snapshot of today's existing
starter scene — zero behavior change when `--template` is omitted). A second,
richer "3rd-person adventure" template is explicitly out of scope here — it needs
gameplay systems (health/combat/character movement) that don't exist yet, and gets
its own follow-up spec once this mechanism exists to build on.

Per the user's stated goal for this sub-project: the design should scale cleanly to
a future marketplace where users share templates — file-based templates (not
compiled-in C++ scene builders) are what make that possible without touching
`create_project` again later.

## Current state

- `create_project(dir, types)` (`app/project/project_io.hpp`) always: creates
  `scenes/`+`assets/`, writes a fresh `mosaic.toml` (name derived from `dir`'s
  basename), constructs a `World` via `build_default_scene(world)`
  (`app/run/demo_scene.hpp`), serializes it with `mosaic::engine::scene::save_to_json`,
  and writes `scenes/main.json`. Refuses to overwrite an existing `dir`; removes
  `dir` on any write failure (never leaves a half-scaffolded project).
- `build_default_scene` has a **second, unrelated caller**:
  `app/run/scene_load.hpp`'s in-memory fallback when `mosaic run`/`editor` is
  invoked with no scene file at all (a quick ad-hoc demo, no project on disk). That
  caller is untouched by this design — `build_default_scene` keeps existing exactly
  as it is today.
- `app/cli/options.hpp`'s `Options::create_path` + `app/cli/options.cpp`'s `create`
  CLI11 subcommand is the only place a new `--template` flag needs wiring.
- Precedent for baking a repo-relative path into the binary at compile time already
  exists: `app/CMakeLists.txt` sets
  `target_compile_definitions(mosaic_app PUBLIC MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts")`,
  consumed directly as `std::string(MOSAIC_FONT_DIR) + "/DemoSans.ttf"`
  (`app/run/demo_scene.hpp`). This sidesteps the same CWD-relative-path gap already
  documented for `plugins/` resolution (`docs/design/scripting.md`), rather than
  repeating it.

## Design

### Template storage: `templates/<name>/` at the repo root

Mirrors `tools/example_plugins/`'s existing tracked-directory convention. Each
template directory has the same shape as a real project's contents, minus the
manifest:

```
templates/
  default/
    scenes/main.json
```

A template may optionally include `assets/` and `plugins/` subdirectories (a future
richer template will use both); `default` needs neither, since a fresh empty
`assets/` (with `.gitkeep`, as `create_project` writes today) covers it. A
template's `mosaic.toml` is never checked in — `create_project` always generates a
fresh one after copying, exactly as it does today, so a template's own name doesn't
leak into instantiated projects.

Location is resolved via a new compile-time define, mirroring `MOSAIC_FONT_DIR`
exactly:

```cmake
target_compile_definitions(mosaic_app PUBLIC
  MOSAIC_FONT_DIR="${CMAKE_SOURCE_DIR}/assets/fonts"
  MOSAIC_TEMPLATES_DIR="${CMAKE_SOURCE_DIR}/templates")
```

### `create_project` becomes a copy, not a builder

`create_project(dir, types, template_name = "default")` gains a third parameter
with a default, so every existing call site compiles unchanged unless it wants to
name a template explicitly. Its body changes from "construct a `World`, serialize
it" to:

1. Refuse if `dir` already exists (unchanged).
2. Resolve `templates_dir = std::string(MOSAIC_TEMPLATES_DIR) + "/" + template_name`.
   If it doesn't exist or isn't a directory: log an error listing every subdirectory
   of `MOSAIC_TEMPLATES_DIR` (the available template names) and return `false` —
   nothing written.
3. Recursively copy every file under `templates_dir` into `dir`
   (`std::filesystem::copy` with `copy_options::recursive`). Any copy failure: log,
   remove `dir`, return `false` (same all-or-nothing guarantee as today).
4. If the copy didn't produce an `assets/` subdirectory (the template had none),
   create one with `.gitkeep`, exactly like today's unconditional step.
5. Write `mosaic.toml` (unchanged: name from `dir`'s basename, `default_scene =
   kStarterScene`) — always freshly generated, after the copy, so it can't be
   shadowed by anything a template might (incorrectly) include.

`types` (`const ComponentReflection&`) is no longer used inside `create_project`
once scene construction is gone — but 26 call sites across 7 files
(`app/run/create.cpp` + six test files) already construct and pass it, mostly for
reasons unrelated to `create_project` itself (building a `World` elsewhere in the
same test). **Decision: keep the parameter, unused, rather than touch 26 call
sites for a signature cleanup that provides no behavioral value.** A comment at the
parameter notes why it's retained. Flagged here explicitly as a judgment call, not
silently made.

### The `default` template's content

Generated **once**, before any code changes: run today's (pre-this-change)
`mosaic create <tmp-dir>`, then copy the resulting `<tmp-dir>/scenes/main.json`
verbatim into `templates/default/scenes/main.json` and check it in. From that point
on, `create_project`'s `default` path no longer depends on `build_default_scene` at
all — it's a static file, byte-for-byte identical to what today's `mosaic create`
already produces, so this ships with **zero observable behavior change** for anyone
not passing `--template`.

**Trade-off, stated plainly:** if `build_default_scene()` is later changed for its
*other* purpose (the ad-hoc no-scene-file fallback in `scene_load.hpp`), the
checked-in `templates/default/scenes/main.json` won't automatically follow — the
two are now independent. This is the same shape of accepted, documented gap as
`plugins/`'s CWD-relative resolution; a future contributor changing
`build_default_scene` should know to regenerate `templates/default/` too if they
want new projects to match. Noted in `docs/design/project-templates.md` (see
Files).

### CLI surface

`app/cli/options.hpp`'s `Options` gains `std::optional<std::string> template_name;
// --template <name> (create)`. `app/cli/options.cpp`'s `create` subcommand gains
`create->add_option("--template", template_name, "Named template to scaffold from
(default: \"default\")")`. `app/run/create.cpp` passes
`options.template_name.value_or("default")` as `create_project`'s third argument.
An unrecognized name surfaces exactly as described in step 2 above — no special CLI
validation needed, since `create_project` already checks and reports.

## Testing

- **`tests/unit/app/project_create_test.cpp`** (extended): the existing
  `"create_project scaffolds a manifest + starter scene + assets dir"` test proves
  the regression guarantee unmodified (default template still produces the exact
  same three artifacts). New cases: an unknown template name fails cleanly with
  nothing written (mirrors the existing overwrite-refusal test's shape); a
  temp-directory template containing an `assets/` file is copied into the new
  project's `assets/`; a temp-directory template containing a `plugins/` file is
  copied into the new project's `plugins/`; a template with no `assets/` still
  gets an empty one with `.gitkeep` (today's guarantee, preserved).
- **`tests/unit/app/options_test.cpp`** (extended): `--template <name>` parses into
  `Options::template_name`; omitting it leaves `template_name` as `std::nullopt`
  (the `.value_or("default")` fallback lives in `create.cpp`, not in parsing).

## Files

- Modify: `app/project/project_io.hpp` — `create_project`'s new copy-based body +
  third parameter.
- Modify: `app/project/project.hpp` — nothing structural needed here, `kStarterScene`
  etc. stay as-is (a template's scene file is still conventionally `scenes/main.json`).
- Modify: `app/cli/options.hpp` / `app/cli/options.cpp` — `--template` flag.
- Modify: `app/run/create.cpp` — thread `template_name` through.
- Modify: `app/CMakeLists.txt` — `MOSAIC_TEMPLATES_DIR` compile definition.
- Create: `templates/default/scenes/main.json` — the one-time generated snapshot.
- Modify: `tests/unit/app/project_create_test.cpp`, `tests/unit/app/options_test.cpp`
  — new cases per Testing above.
- Create: `docs/design/project-templates.md` — documents the `templates/<name>/`
  shape, the `MOSAIC_TEMPLATES_DIR` resolution mechanism, the `default`-template
  generation/drift trade-off, and the `--template` CLI flag — mirroring how
  `docs/design/scripting.md` documents the plugin system.
- Modify: `docs/project/next-steps.md` — new dated entry.

## Non-goals / follow-ups

- **The 3rd-person adventure template's actual content** (character movement,
  attack/sneak, health/death states, a basic enemy) — a separate, later spec/plan,
  once a `Health` component and basic combat/AI scripting patterns exist. This
  mechanism is designed so that work is pure content authoring under
  `templates/adventure/`, no further `create_project` changes needed.
- **A marketplace/repository** for sharing community templates — explicitly what
  this file-based design is meant to scale toward, but building the actual
  publish/discover/fetch infrastructure (hosting, accounts, versioning) is its own,
  much larger future sub-project.
- **Template parameterization beyond the project name** (e.g. a template-specific
  setup wizard, prompted values substituted into scene content) — v1 templates are
  copied verbatim; grow this against real demand if a template ever needs it.
- **Removing the now-unused `types` parameter from `create_project`** — deliberately
  deferred; see the Design section's stated trade-off.
