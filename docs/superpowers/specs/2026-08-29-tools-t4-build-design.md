# Tools T4 — `mosaic build` (standalone bundle) (design)

**Decomposition:** [tools-workflow-decomposition](2026-08-29-tools-workflow-decomposition.md) (slice T4).
**Builds on:** T3 (project model + `load_manifest` + `resolve_startup_scene`), T2 (`populate_world`,
`scene_io`), #10 TOML, #35 app/CLI.
**Date:** 2026-08-29.

## Summary

`mosaic build <project> [-o <out>]` packages a project into a **self-contained, runnable
bundle**: a directory holding a copy of the `mosaic` runtime binary, the project's default scene,
and the manifest. Because the bundle is project-format (`mosaic.toml` + `scenes/`), the copied
binary runs it through T3's existing `mosaic run <dir>` path — so T4 adds only the packaging
(build) side; the run side is already done. The bundle runs on another Linux machine with no
Mosaic install and no source tree: `cd dist && ./run.sh`.

## Scope

**In:**
- A `build_bundle(project_dir, out_dir, runtime_binary, types)` packager (app tier), with the
  runtime binary path injected for headless testability.
- Copies the project's default scene, writes the manifest, copies the running `mosaic` executable
  (`+x`), and writes an executable `run.sh` launcher.
- A `mosaic build <project> [-o <out>]` subcommand; entry dispatch in `main()`.
- Safe overwrite semantics (regenerate a prior bundle; refuse to clobber a non-bundle dir).

**Deferred (later slices / YAGNI):**
- Copying real per-project `assets/` files — that is **T5** (importer). v0.1 scenes reference the
  built-in demo assets, which the copied binary's `setup_runtime` registers.
- Multi-scene bundles (only `default_scene` is packaged); cross-platform packaging / installers /
  zipping; binary stripping or size optimization.

## Architecture

Project/CLI/build code lives in the **`app/`** tier.

### 1. The bundle layout

```
dist/
  mosaic          # a copy of the running mosaic executable (chmod +x)
  mosaic.toml     # the manifest (name + default_scene) — identical project format
  scenes/
    main.json     # the project's default scene, copied
  run.sh          # exec "$(dirname "$0")/mosaic" run "$(dirname "$0")" --window "$@"  (chmod +x)
```

The bundle is deliberately **project-format**: `mosaic.toml` + `scenes/`. Running it reuses T3
unchanged — `./mosaic run .` → `resolve_startup_scene(".")` → `populate_world` → render. No new
run-side code.

### 2. `build_bundle` (`app/project/bundle.hpp`)

```cpp
// Package `project_dir` into a self-contained bundle at `out_dir`, copying `runtime_binary`
// (the mosaic executable) into it. Returns false (and logs) on any failure; rolls back a
// partially written `out_dir`. Reuses T3 load_manifest + T2 scene_io.
bool build_bundle(const std::string& project_dir, const std::string& out_dir,
                  const std::string& runtime_binary,
                  const mosaic::engine::reflection::ComponentReflection& types);
```

Steps:
1. `load_manifest(project_dir)` (T3) — error if `project_dir` is not a project.
2. Resolve overwrite (see §5). Create `<out>/` and `<out>/scenes/` (and the parent dirs of the
   scene's relative path).
3. Copy the default scene: `std::filesystem::copy_file(project_dir/<default_scene>,
   out_dir/<default_scene>)` — error if the source scene is missing. (Verbatim copy preserves
   hand-edits; the scene is #19 JSON.)
4. Write `<out>/mosaic.toml` = `to_toml(to_value(manifest))` (unchanged manifest) via
   `save_scene_file`.
5. Copy `runtime_binary` → `<out>/mosaic` (`copy_file`), then set the executable bit
   (`fs::permissions(..., perms::owner_all | group/others read+exec, perm_options::add)`).
6. Write `<out>/run.sh` (the launcher above) via `save_scene_file`, then set `+x`.

`types` is threaded for a possible future re-serialize step; v0.1 copies the scene verbatim.

### 3. `run_build` (`app/run/build.{hpp,cpp}`)

```cpp
int run_build(const Options& options);
```
Resolves the running executable via `std::filesystem::read_symlink("/proc/self/exe")` (Linux),
builds a `TypeRegistry` + `ComponentReflection` (`register_builtin_components`), computes the
output dir (`options.build_out` or the default `<project>/dist`), and calls `build_bundle`.
Returns 0/1.

### 4. CLI (`app/cli/options.{hpp,cpp}`)

- `Command` gains `Build`.
- `Options` gains `std::optional<std::string> build_project;` and
  `std::optional<std::string> build_out;`.
- New subcommand: `mosaic build <project> [-o <out>]` — required positional `project`
  (→ `build_project`), optional `-o/--output <dir>` (→ `build_out`).

### 5. Overwrite semantics (safe + rebuildable)

- `<out>` absent or **empty** → create + write.
- `<out>` exists and **is a prior bundle** (contains a `mosaic.toml`) → `remove_all` it, then
  regenerate (a clean rebuild). The default `<project>/dist` hits this path on repeat builds.
- `<out>` exists, non-empty, and **not** a bundle → **refuse** (error + non-zero exit, leaving it
  untouched). Protects an arbitrary `-o <path>` from being clobbered.

### 6. Entry (`app/main.cpp`)

Dispatch `Command::Build` → `run_build(pr.options)`, next to the existing `Command::Create`
dispatch (both before the editor/run paths). No `editor.cpp` / run-path changes.

## Data flow

```
mosaic build mygame            -> run_build: exe = /proc/self/exe; out = mygame/dist
                                  -> build_bundle("mygame", "mygame/dist", exe, types)
                                     -> dist/{mosaic (+x), mosaic.toml, scenes/main.json, run.sh (+x)}
cd mygame/dist && ./run.sh      -> ./mosaic run . --window
                                  -> resolve_startup_scene(".") (T3) -> populate_world (T2) -> render
```

## Error handling

All failures log a clear message and yield a non-zero exit:
- `<project>` is not a project (no `mosaic.toml`) → error.
- The manifest's `default_scene` file is missing on disk → error; roll back `<out>`.
- `runtime_binary` unreadable / a copy or write fails → error; roll back `<out>`.
- `<out>` refused (non-empty, non-bundle) → error; **leave `<out>` untouched** (no rollback of a
  path we didn't create).

Rollback = `std::filesystem::remove_all(out_dir)` (best-effort) on any failure that occurs after
we began writing into a bundle-owned `<out>` — mirroring T3's `create_project`. Never remove a
refused pre-existing non-bundle dir.

## Testability

Headless-first — the injected `runtime_binary` path makes `build_bundle` fully unit-testable
without the real executable (tests pass a dummy file):

- **`build_bundle` happy path** (`tests/unit/app/bundle_test.cpp`, temp dirs): from a
  `create_project` project + a dummy binary file, the bundle contains `mosaic.toml` (parses; same
  `default_scene`), `scenes/main.json` (loads via `populate_world` → 7 entities), a `mosaic` file
  whose bytes equal the dummy's **and** which has the owner-executable bit set, and an executable
  `run.sh`.
- **Overwrite rule:** a second `build_bundle` over the prior bundle succeeds (regenerates);
  building into a non-empty non-bundle dir returns false and leaves that dir's contents untouched.
- **Errors:** a non-project input → false; a project whose `default_scene` file was deleted →
  false + `<out>` rolled back (does not exist).
- **CLI** (`tests/unit/app/options_test.cpp`): `build <project> -o <out>` → `Command::Build` +
  `build_project`/`build_out`; `build <project>` with no `-o` leaves `build_out` empty (the
  default `<project>/dist` is computed in `run_build`).
- **Functional (the real payoff):** `mosaic create P && mosaic build P -o B && B/mosaic run B
  --window --ticks 3` exits 0 with 0 Vulkan validation errors — a bundle **run by its own copied
  binary**, no source tree. And `B/run.sh` launches (exit 0).

## Ground rules honored

- **Tier contract:** build/CLI code in `app/`; `engine/`/`foundation/` unchanged. Filesystem via
  `<filesystem>` + T2's `scene_io`, confined to `app/`.
- **Serialization is #10:** the bundle manifest is the reflected `ProjectManifest` over TOML;
  scenes stay #19 JSON (copied verbatim). No new mechanism.
- **Reuses T2/T3:** `load_manifest` (T3), `save_scene_file` (T2); the bundle *runs* through T3's
  `resolve_startup_scene` — no duplicated run logic. Only the *build* side is new.
- **No new third-party deps** (`<filesystem>`, `/proc/self/exe` are stdlib/OS). Catch2 test names
  avoid a leading `--`.

## Acceptance

- [ ] `build_bundle` produces the four-artifact bundle; the `mosaic` copy is executable; the
      scene loads; the manifest round-trips.
- [ ] Overwrite: regenerates a prior bundle; refuses a non-bundle non-empty `<out>` untouched.
- [ ] Errors (non-project, missing scene, copy failure) → false + rollback; all unit-tested.
- [ ] `mosaic build <project> [-o <out>]` parses to `Command::Build` + project/out.
- [ ] Functional: a built bundle **runs via its own copied binary** (`B/mosaic run B --window`)
      and `run.sh`, exit 0, 0 validation errors, no source tree.

## Depends on / relates

- **Depends on:** T3 (`load_manifest`, `resolve_startup_scene`, project format), T2
  (`populate_world`, `scene_io`), #10 TOML, #35 CLI. **Blocks / relates:** T5 (the importer
  produces per-project `assets/` files that `build_bundle` will then copy into the bundle — the
  seam this slice leaves open).
