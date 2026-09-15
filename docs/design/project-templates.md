# Project templates

`mosaic create <path> [--template <name>]` scaffolds a new project by copying
`templates/<name>/`'s contents into `<path>`, then writing a fresh `mosaic.toml`
(project name derived from `<path>`'s basename, exactly as before). Omitting
`--template` uses `default`.

## Template shape

A template directory has the same shape as a real project's contents, minus the
manifest:

```
templates/
  default/
    scenes/main.json
  <name>/
    scenes/main.json
    assets/        (optional)
    plugins/       (optional)
```

A template's `mosaic.toml` is never checked in -- `create_project` always
generates a fresh one after copying, so a template's own name never leaks into
instantiated projects. If a template has no `assets/` directory (or an empty
one), the new project still gets one with a `.gitkeep`, exactly like the
`default` template does today.

## Where templates are found

`MOSAIC_TEMPLATES_DIR`, a compile-time define set in `app/CMakeLists.txt` from
`${CMAKE_SOURCE_DIR}/templates` -- the same mechanism `MOSAIC_FONT_DIR` already
uses for `assets/fonts`. This sidesteps the CWD-relative-path gap already
documented for `plugins/` resolution (see "Where the plugins directory lives"
in `docs/design/scripting.md`) rather than repeating it: templates are found by
an absolute, baked-in path, not by the process's current working directory.

## The `default` template's origin, and its trade-off

`templates/default/scenes/main.json` is a **one-time, checked-in snapshot** of
what `build_default_scene()` (`app/run/demo_scene.hpp`) produced at the moment
this mechanism was built -- captured by running `mosaic create` once and copying
its output. From that point on, `create_project`'s `default` path no longer
calls `build_default_scene()` at all; it's a static file.

`build_default_scene()` itself is unchanged and still has its own, unrelated
caller: `app/run/scene_load.hpp`'s in-memory fallback when `mosaic run`/`editor`
is invoked with no scene file at all (an ad-hoc demo, no project on disk).

**This means the two can drift.** If `build_default_scene()` is changed later for
that ad-hoc-run purpose, `templates/default/scenes/main.json` will not
automatically follow. A contributor who wants new projects to match a changed
demo scene needs to regenerate the snapshot manually (rerun `mosaic create` on a
scratch directory, copy its `scenes/main.json` over `templates/default/`'s).
This is the same shape of accepted, documented gap as the `plugins/` CWD issue
above -- not a bug, a known and intentional decoupling.

## Adding a new template

Create `templates/<name>/` with at least `scenes/main.json` (any valid scene
JSON, e.g. saved from Studio). Add `assets/` and/or `plugins/` if the template
needs them. No other registration step exists -- `create_project` discovers
templates by directory listing, not a manifest.

## Planned templates

Only `default` ships today. A second template — a 3rd-person adventure starter —
was scoped during this mechanism's design and deliberately deferred to its own,
later spec/plan rather than folded into this one, since it's real gameplay
content authoring, not a mechanism change:

- A controllable capsule character (`CapsuleCollider` + `RigidBody` already exist
  in `engine/physics/`) supporting move, run, jump, attack, and sneak.
- A life/death state for both the player and enemies, with a simple on-screen
  health bar (`engine/ui/` has `UiText`/`UiRect` to build one from; there is no
  dedicated health-bar widget yet).
- At least one basic enemy that can damage and kill the player, and can itself be
  killed by the player's attack.

None of this exists in the engine yet — there is no character controller (beyond
the raw physics primitives above), no health/damage/combat framework, and no
enemy AI beyond the general-purpose `engine/navigation/` nav-agent system, which
was never built with combat in mind. Building it needs, at minimum, a new
reusable `Health` component (current/max HP, alive/dead) and basic combat/AI
scripting patterns — real design and implementation work, tracked as explicit
future work rather than silently dropped. Full rationale for deferring it:
`docs/superpowers/specs/2026-09-14-project-templates-mechanism-design.md`'s
Non-goals section.

## `create_project`'s `types` parameter

`create_project(dir, types, template_name, templates_root)` still takes a
`const ComponentReflection&` even though the function no longer uses it --
templates are static files now, not in-process-built scenes. It's kept, unused,
because 29 existing call sites across 7 files construct and pass it for reasons
unrelated to `create_project` itself; removing the parameter would mean touching
all of them for no behavioral gain. A future cleanup pass could remove it.
