# User config directory

`~/.config/mosaic/` (respecting `$XDG_CONFIG_HOME` when set) is the first
user-level — not project-level, not compile-time-baked — directory anywhere
in this codebase. Every other "default location" in `mosaic` resolves to
either a project-relative path (`plugins/`, `assets/`, `mosaic.toml`) or a
compile-time constant baked in by CMake (`MOSAIC_TEMPLATES_DIR`,
`MOSAIC_PACKAGES_DIR`, `MOSAIC_DOCS_DIR`, `MOSAIC_FONT_DIR`) — nothing reads
`$HOME` or persists state that outlives a single project until this.

## Resolution rule

`app::user_config_dir()` (`app/plugin/package_sources_registry.hpp`)
returns `$XDG_CONFIG_HOME/mosaic` if that variable is set and non-empty,
else `$HOME/.config/mosaic` — standard XDG base-directory behavior, even
though nothing else in this repo follows that convention yet, since this is
where it's established for the first time.

## What lives here today

- **`sources.toml`** — the named package-sources registry
  (`SourcesRegistry`/`NamedSource`, managed via `mosaic plugin source
  {add,list,remove}`): a friendly name mapped to a local filesystem path
  holding packages. See `docs/design/plugin-packages.md`.

## Plausible future uses (not scheduled, documented for later)

Now that a user-level config location exists, these are directions that
would naturally live here rather than reinventing a location each time:

- **Editor preferences** — window size/position, panel layout, theme, a
  recently-opened-projects list. Currently Studio has no persisted
  preferences at all; every launch starts from the same defaults.
- **Marketplace auth** — once the public marketplace
  (`docs/design/marketplace.md`, still planning-only) exists, a personal
  API token or similar credential for submitting packages would belong
  here, not in any project file.
- **A local cache of pulled repositories** — this has since shipped:
  `mosaic plugin source add <name> <git-url>` now has Mosaic run `git
  clone`/`git pull` itself (see `app/plugin/git.hpp`/`.cpp`). Rather than the
  sibling XDG cache directory (`$XDG_CACHE_HOME`, e.g. `~/.cache/mosaic/`)
  speculated here, the clones live under `~/.config/mosaic/sources/<name>/`
  — a subdirectory of this same user-config directory. See the design
  spec's "Cache location" section for the rationale: this codebase has no
  existing cache-dir concept, so introducing one as a side effect would
  have meant two new user-level directory concepts riding in on one
  feature instead of one.
- **License/registration state** — this repo ships both an open license
  and `LICENSE.commercial.md`; if commercial-licensed features ever need a
  local activation record, this is the natural place, not a project file.

None of the above is scheduled. Recorded so the next feature that needs a
user-level home doesn't have to rediscover this decision.
