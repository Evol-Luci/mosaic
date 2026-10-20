# CI build caching — design

**Status:** spec, written 2026-10-20, awaiting review. Nothing is implemented. No plan exists yet.

**Path: architectural.** The mechanism is small (two files). The reason it is not "bounded" is
that it fixes a *policy* — who may write into the shared, writable cache every future workflow in
this public repo inherits, and which third-party code may touch it. See "Standing constraints".

## Why this matters more than the minutes suggest

Actions minutes are free (the repo is public), so this is not about cost. It is about the
latency a person waits through, and the person is usually a maintainer with one usable block of
time that day. A ten-minute round trip can be the difference between landing a change in that
block and not landing it. That raises the value of a *reliable* modest win. It does **not**
loosen the correctness bar: a fast, subtly wrong cache costs that person a scarce block chasing a
phantom bug, which is worse than no cache.

## What was measured (and how)

Everything here was measured on 2026-10-20; none of it is carried over from `next-steps.md`.
Local numbers come from a 20-core machine with gcc 16.2.1; CI is a 4-vCPU runner with noble's
gcc, so local wall-clock is **indicative of shape, not of CI magnitude**. Local builds used a
fixed `-j4` to approximate a 4-vCPU runner, but CI actually runs `-j"$(nproc)"`; that the two
agree is an **assumption the CI log must confirm** (`nproc` is printed, below), not something
asserted here (CI's build step ran
1.4-1.8x slower than the same cold build locally: 358-477 s vs 263-285 s).

### Where a CI run's time goes

```
gh run view <id> --json jobs --jq '.jobs[]|(.steps[]|"\(((.completedAt|fromdate)-(.startedAt|fromdate)))s\t\(.name)")'
```
Five successful `ci.yml` runs (39510781140, 39510629774, 39508124859, 39168162538, 39165359631):

| Step | Seconds across the 5 runs |
|---|---|
| whole job | 486, 564, 566, 609, 643 (mean 574 = 9m34s; range 8m06s-10m43s) |
| Build (debug) | 358-477 (75-80% of the job) |
| Configure (debug) | 66-78 (this is where FetchContent clones happen) |
| Unit tests | 37-65 |
| Vulkan SDK apt | 12-27 |
| other apt, lints, checkout, smoke | at most 6 s each |

`runner_name` is `null` on all five healthy runs, so it discriminates nothing. A failed
no-runner run is identified by **step count and duration** (zero steps, ~2 s), not by that field.

### Where the compile time goes

A cold `-j4` build (`cmake --build --preset debug -j4`, 4m23s), CPU time per object from
`build/debug/.ninja_log`, taking the **last** entry per output (the log accumulates across
builds and across renamed sources, so counting its lines over-counts):

| Group | Objects | CPU | Bytes |
|---|---|---|---|
| first-party (Mosaic) | 254 | 929 s (92%) | 1,170 MB |
| third-party | 198 | 80 s (8%) | 34.3 MB |

Object *counts* come from the filesystem, not the log:
`find build/debug -name '*.o' -not -path '*/_deps/*' -not -path '*mosaic_imgui.dir*' | wc -l`.
Two classification traps worth recording: `mosaic_imgui` is Mosaic's own CMake target but compiles
imgui's sources (6 objects, third-party by content), and a path filter of `*_deps*` wrongly drops
them because their object path contains `_deps`. This tree has 254 first-party objects; the
coordinator's older `build/debug` has 256. **That 2-object difference is unreconciled** (different
age and generator) and is not material to any decision here.

### How much of the tree a typical change touches

Mapping each of the last 60 first-parent commits on `main` to the translation units that
(transitively) include a changed file, using `ninja -t deps` from a warm tree:

| Affected first-party TUs (of 254) | min | p25 | median | p75 | p90 | max |
|---|---|---|---|---|---|---|
| per commit | 0 | 1 | 5 | 42 | 77 | 92 |

15 of 60 commits affect no object at all (docs/data). **Caveat:** this applies *today's* include
graph to old commits, so it is an approximation of the shape, not a record. Nothing in the window
touched the widest headers (see next table).

### What ccache does here (local, ccache 4.14, `-j4`, `compiler_check=content`)

`cmake -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`, then `ninja -t clean` between scenarios so every
link and every object is re-requested, as on a fresh CI checkout:

| Scenario | Wall | ccache result |
|---|---|---|
| cold cache | 285 s | 0 / 452 hits |
| fully warm | 9 s | 452 / 452 hits |
| one `.cpp` edited | 10 s | 451 hits, **1** miss |
| `engine/render/image.hpp` edited (41 TUs include it) | 80 s | 411 hits, **41** misses |
| `foundation/core/entity_id.hpp` edited (171 TUs include it) | 233 s | 281 hits, **171** misses |

The miss counts equal the include fan-out exactly, which is the evidence that the cache is
neither too loose (a stale hit would show *fewer* misses than dependents) nor too tight (more).
The edits were real declarations, not comments: a comment-only header edit misses in direct mode
and then *hits* in preprocessor mode, so it understates a genuine change.

- Full tree cache on disk: **245 MB** after the cold run (ccache zstd-compresses by default; ~4.8x
  on 1.2 GB of objects). After all five scenarios, 522 MB.
- **The spread is the point.** The same mechanism gives a ~10 s build or a ~233 s build depending
  on which header changed. The median change touches 5 of 254 TUs; the widest header touches 171.

### Cloning (local, sequential, 9 tag-pinned deps)

`git clone --depth 1 --branch <tag>` vs full `git clone` + `checkout`:

| | shallow | full |
|---|---|---|
| nine tag-pinned dependencies, total | 4.9 s | 45.3 s |
| of which json / miniaudio / imgui | 2.1 s | ~35 s (327, 326, 140 MB vs 25, 11, 9.5 MB) |

A configure with all deps already on disk and a cleared cache takes **2.1 s**, against 38.7 s for
a from-scratch configure, so ~36 s of local configure is cloning. CI configure is 66-78 s; the
clone share on CI is **inferred, not measured on CI**. box3d (a SHA pin) full-clones in 0.8 s.

### Reproducibility

Built twice at the **same path** (second after `ninja -t clean`), sha256 of every `.o`, `.a`,
`mosaic` and `mosaic_unit_tests` (471 files): **471 identical, 0 differing**. So sha256 comparison
of artifacts is a valid correctness oracle at a fixed path. Not tested: a *different* path (CI's
checkout path is constant, so it only matters for sharing a cache between a laptop and CI, which
is a non-goal), and CI's compiler (the check must be repeated there: see Evidence).

### Cached vs uncached parity (local)

Same tree, same path (`build/cc`), gcc 16.2.1. Build A: no launcher, after `ninja -t clean`.
Build B: ccache launcher, after `ninja -t clean`, **452 / 452 hits (100%)**. sha256 of every
`.o`, `.a`, `mosaic` and `mosaic_unit_tests`: **471 files, 471 identical, 0 differing.**
A build assembled entirely from ccache entries is byte-identical to one compiled from scratch.
This is local evidence only; it must be repeated on CI's compiler (see Evidence).

## Design

Two independent changes, deliberately separable so the safe win is not gated on the riskier one.

### Slice A — shallow FetchContent clones (no cache)

Add `GIT_SHALLOW TRUE` to the tag-pinned `FetchContent_Declare` blocks in `CMakeLists.txt`:
Catch2, tomlplusplus, nlohmann_json, CLI11, glfw, miniaudio, enet, imgui, pybind11.

- **box3d is excluded and must stay excluded.** Its `GIT_TAG` is a commit SHA, and CMake's
  `GIT_SHALLOW` requires a branch or tag (a bare SHA is not shallow-fetchable in general: it
  depends on the server advertising the object). Put a comment on box3d's declaration saying so,
  or the next person will "tidy" the inconsistency and break configure.
- Applied per declaration, not globally, for the same reason.
- No cache, no key, no staleness: it changes how much *history* is fetched, not what is checked
  out. It is also the only item here that speeds up a developer's **local** first build.
- Expected saving: roughly 35-40 s on a cold clone (measured locally above; CI to be measured).
- Ships first, alone, if it lands about a minute, so its win is not held behind Slice B.

### Slice B — ccache, restored on every run, saved only by `main`

**Mechanism.** `apt-get install ccache` joins the existing apt step; the workflow passes
`-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache` on the configure
command line. Nothing is added to `CMakeLists.txt` or the presets, so a local build is unchanged
and a missing ccache cannot break configure. (This departs from `next-steps.md`'s "no ccache in
either CMake or the workflow": the workflow is the right and only place for it.)

**Layering of correctness.** ccache's own content hash is what guarantees a hit is valid: the
preprocessed translation unit, the compiler, and the flags. The `actions/cache` key chooses only
*which bucket of that store to restore*. A key that is too loose therefore degrades the hit rate
or wastes quota; it cannot make a wrong object compile into the build. That asymmetry is the
reason ccache is the right primitive.

**`actions/cache` key.**
```
primary:      ccache-v1-<ImageOS>-<ImageVersion>-gcc<gcc -dumpfullversion>-<github.sha>
restore-keys: ccache-v1-<ImageOS>-<ImageVersion>-gcc<gcc -dumpfullversion>-
```
Computed in a step (the runner's `ImageOS`/`ImageVersion` are environment variables, not
workflow-expression contexts). Each part is there for a stated reason:

| Component | Why |
|---|---|
| `v1` | A manual epoch: the kill switch if a cache is ever suspected bad. Bumping it abandons every old bucket. We cannot delete caches from the workflow, so this is the only lever. |
| `ImageOS`/`ImageVersion`, gcc version | Stop buckets for a different image or compiler accumulating dead entries and consuming quota. **Not** needed for correctness (ccache hashes the compiler). Image identity is the Ubuntu 26 migration mitigation. |
| `github.sha` | `actions/cache` never overwrites an existing key, so every save needs a unique one; the `restore-keys` prefix then picks the newest. |

**Deliberately not in the key:** a hash of `CMakeLists.txt` / presets / `ci.yml`. It is the
natural-looking choice and it is wrong here. Correctness does not need it (ccache hashes flags and
source), and it would reset the bucket on every dependency bump or workflow edit, turning
"too tight, never hits" into the default after any infrastructure change.

**ccache configuration** (in the workflow, via environment):

| Setting | Value | Reason |
|---|---|---|
| `compiler_check` | `content` | The default is `mtime` of the compiler binary, which on a fresh runner is not a stable identity. Hashing the binary costs a read per run, not per object. |
| `max_size` | `500M` | Measured full tree: 245 MB compressed. 500M holds one full tree plus churn (a p90 change is ~77 objects, roughly 40 MB). ccache evicts least-recently-used and refreshes an entry on every hit, so stale versions of changed objects go first. The cap also bounds `actions/cache` save and restore time, which would otherwise grow until it ate the win. **To be re-checked against measured restore time on Actions; if restore+save exceeds ~40 s, lower it.** |
| compression | ccache default (zstd) left on | Measured 4.8x. `actions/cache` re-compresses the tarball, which wastes a little CPU on incompressible data; it cannot be disabled and the cost is small next to the transfer saving. |
| `CCACHE_DIR` | `${{ runner.temp }}/ccache`, set explicitly | The store's location is **named, not assumed**. The same expression is the `path:` of both `actions/cache/restore` and `/save`, and a guard step fails the job if `ccache --show-config` reports a different `cache_dir`. Without this, the failure is a cache that saves 0 bytes and a hit rate that never moves, which reads as "ccache didn't help" when the truth is "we cached the wrong directory". The size of the directory is printed after restore and before save, and an empty store at save time is a warning. |
| `hash_dir`, `base_dir` | defaults | The checkout path is constant on the runner (`/home/runner/work/mosaic/mosaic`), so debug-info paths match across runs. Sharing with another path is a non-goal. |
| sloppiness | none (strict default) | `__DATE__`/`__TIME__` do not appear in first-party code (grep over app, engine, foundation, modules, studio, tests, tools), so there is no reason to relax anything. |
| `ccache --version` | printed in the log | CI will use noble's apt ccache, not the 4.14 measured locally. |

**Write policy.** Pull-request runs (including from forks) use `actions/cache/restore` only.
Only a push to `main` saves, and only after a green job. **That is a condition we must write,
not a default we inherit:** saving needs the split `actions/cache/save` action (the combined
`actions/cache` cannot be restricted to `main`), and the split action has no post-step
success guard. The save step therefore carries an explicit
`if: success() && github.event_name == 'push' && github.ref == 'refs/heads/main'`. A test of
the plan is that a deliberately failed run on a branch where saving is otherwise permitted saves
nothing.

*The rationale is quota, not security.* GitHub scopes a cache written by a PR run to that PR's
own ref, and `main` cannot restore from a PR's scope. So a PR, fork or not, cannot poison what
`main` reads whatever this policy says; a PR that wrote its own cache could only poison itself.
The reason for restore-only is the pool: each save is hundreds of MB against a ~10 GB LRU pool, so
letting every push of every PR save would churn it and evict `main`'s entry, forcing everyone back
to a cold build. **Do not cite this policy as protection against fork poisoning** — someone wanting
PR-scoped writes for a long-lived branch would read that, believe the change unsafe, and not make
a correct change.

*Not yet verified:* the scoping claims above are GitHub's documented behaviour and my reading of
it, not something this repo has demonstrated. The Evidence section probes it.

**Cost of restore-only.** A PR pushed five times pays for its changed objects five times, because
none of its own objects are saved. Revisit with data from a few real PR iterations.

### Considered and rejected: caching `build/_deps`

The prescription in `next-steps.md` was to cache `build/_deps` keyed on the dependency files.
Rejected, on two independent grounds, either sufficient:

1. **Target too small.** Third-party objects are 198 of 452 objects but 80 s of 1,009 s of CPU
   (8%), about 20 s of wall at `-j4` even if every one hit. First-party is 92%.
2. **It restores a build tree, not content.** Ninja decides what to rebuild from file mtimes and
   recorded command lines. A fresh checkout stamps new mtimes on sources; a restored tree carries
   old mtimes on objects. Depending on how those compare, Ninja rebuilds everything (a cache that
   never helps) or keeps objects it should not (stale). The key also omits compiler, image, build
   type, flags and baked-in absolute paths. This is exactly the fast-and-subtly-wrong shape.

ccache covers third-party objects too, by content, so nothing is lost by dropping it.

### Considered and left alone

- **Source-only FetchContent cache.** Unnecessary if Slice A lands the clone saving, and it
  would freeze whatever a mutable tag pointed at when written (only box3d is SHA-pinned).
- **Apt layer.** 20-30 s of the run, against a stale-package-list risk with a third-party
  (LunarG) apt repository. Not worth the risk for the win.
- **Caching test results / skipping unaffected tests.** Unsound without a dependency analysis the
  repo does not have. See Non-goals; note it is the largest *remaining* fixed cost once the build
  is cached.

## Standing constraints (policy, not just this slice)

1. **First-party actions only.** The repo runs exactly one action today, `actions/checkout@v4`.
   That de-facto position is now explicit: workflows use `actions/*` only. A third-party action
   with cache write access in a public repo is a supply-chain path into every subsequent build,
   which is a worse trade than the few lines of YAML it saves. Anything adding a step inherits
   this.
2. **Only `main` writes shared build caches.** (Reason: quota. See above.)
3. **A cache miss, a corrupt cache and an unavailable cache service all degrade to a slow correct
   build.** `restore` failing must not fail the job; ccache treats a bad entry as a miss.
4. **No behaviour change to local builds.** Nothing about ccache lands in `CMakeLists.txt`.

## What would make us not ship it

This is a **floor, not a target.** It was set after seeing the cold breakdown and the local
ccache scenarios above, and before any CI run with a cache exists, so it is not independent of
those; the CI numbers it will be judged on do not yet exist.

Baseline: five cold runs, mean 9m34s (574 s), median 9m26s (566 s), range 8m06s-10m43s.

Do **not** ship Slice B if any of these holds:
1. A job whose change touches a **single source file**, with a `main`-seeded cache, has a median
   above **7m00s**, or the worst of at least 5 such runs is above **9m00s**.
2. A **cold or evicted** run costs more than **30 s** above the cold baseline (restore, save and
   ccache overhead included). This is the cliff, named below.
3. **Any correctness probe fails** (see Evidence), regardless of speed.

Do **not** ship Slice A if the cold configure does not drop by at least **20 s** on CI.

**Predictions to be tested** (so a result can be called a gap, not just a pass). These are
estimates from the local table scaled by CI's ~1.5x, plus ~125 s of fixed per-job cost
(checkout/apt ~20 s, shallow configure ~25 s, tests ~50 s, cache restore/save ~30 s): single
`.cpp` or docs-only, **2m30s-3m30s**; a 77-TU change (p90), **4m30s-6m**; a 171-TU header,
**7m-9m** (barely better than cold). The deliverable report states, per scenario, the achieved
time, the prediction, and **the gap and its cause**. Meeting the floor while sitting two minutes
above the prediction is a result that stopped looking.

### Variance is a named trade-off

Cold run: ~9.5 min. Typical cached run: a few minutes. A wide-header change: most of a cold run.
The distribution is therefore **bimodal by which file changed**, not by luck, and a cold-cache cliff
exists after eviction or a runner image / compiler change (new bucket). For someone budgeting a
short block this matters as much as the mean. It is a property of the design and is not removable;
it is bounded by criterion 2 above, reported per scenario, and the eviction behaviour is measured
below because a correct, fast cache that evicts itself between runs does not exist.

## Evidence (the gate cannot see any of this)

**Instrument every run, because the runner image can change under the campaign.** The
`ubuntu-latest` to Ubuntu 26 migration was scheduled to start 2026-10-19 and had not reached
this repo as of 2026-10-20 (the most recent run reported `ubuntu-24.04`, release
`ubuntu24/20261012.334`). If it arrives mid-measurement, vCPU count, compiler and base timings
all move, and the cache key correctly starts a fresh bucket, so a "warm" scenario measured after
the flip is silently a cold one. Every run prints `ImageOS`, `ImageVersion`, `gcc --version`,
`ccache --version` and `nproc`, and those are recorded **beside** every timing. A change in any
of them **invalidates cross-run comparison**: such runs are reported separately and never
averaged in.

**A failure at "Install Vulkan SDK components" during the campaign is not this change.** The
LunarG apt list is pinned to `noble` explicitly and `ci.yml`'s own comment predicts that step
breaks when the image moves. It is a pre-existing time bomb: do not debug it as a caching bug,
do not fix it inside this slice, report it at once. The tell for a *platform* failure is zero
steps and ~2 s; a LunarG failure has many steps and a real log.

No unit test can prove a cache works. The 1455-case suite stays green and says nothing here.

**Effective** — Actions runs, per-step timing via the `gh run view` command above:
- cold: the five existing runs are the baseline (no new cold runs needed);
- fully warm same commit; single-file source change; `image.hpp`-class (41 TUs); `entity_id.hpp`-class
  (171 TUs). At least 3 runs each, 5 for the single-file scenario that decides criterion 1;
- ccache `--show-stats` and `--version` in the log; compressed cache size; restore and save time.

**Correct** — all of these must pass:
1. *Parity:* sha256 of the executables and libraries from a cached build equal those of a cold
   build of the same commit, **on CI** (reproducibility was shown locally only). If CI's cold
   builds are not self-reproducible, fall back to comparing test results and symbol/section
   content, and say so.
2. *Invalidation probes, derived from the code and workflow, not from the assertion list:* bump a
   dependency tag, edit a first-party header, change a compile flag, change the compiler. Each must
   miss exactly where expected. The local fan-out table is the model: miss count == dependent-TU
   count.
3. *Key-input derivation:* every input that changes a compiler's output is accounted for — compiler
   identity (`compiler_check=content`), flags and build type (ccache hash), include paths and
   headers (preprocessed output), checkout path (constant), dependency versions (reach ccache as
   source), embedded-Python headers (preprocessed output), glslc shader output (not a ccache
   step; always re-run).

**Fail-safe** — an empty cache, a corrupt cache and an unavailable cache service must each give a
passing, cold-speed build.

**Eviction** — `gh api repos/Evol-Luci/mosaic/actions/cache/usage` (count and bytes) sampled over
the experiment and then weekly for a month, plus hit rate over time, because eviction only shows
up as a trend. Quota and retention limits cannot be read through the API (HTTP 402, "valid payment
method"), so the ~10 GB / 7-day figures are from GitHub's documentation, **to be confirmed
empirically**.

**Scoping probe** — a throwaway workflow that (a) writes a cache from branch A and shows branch B
cannot restore it, and (b) after merge, shows a PR restores `main`'s. Only (a) can run before
merge; (b) is the first PR after the merge, and that first merged run is necessarily cold.
During measurement the workflow temporarily allows the experiment branch to save; that condition
is removed before merge. **The probe workflow is `workflow_dispatch:` only** (no `push:` or
`pull_request:` trigger, so it cannot race `ci.yml`), is named so it is obviously disposable, and
**must never reach `main`**: it is deleted, together with the temporary save condition, in one
commit before the PR is marked ready, and the final diff is checked for both.

## Non-goals

Each has a matching entry in the "CI and build" section of `docs/project/deferred-work-backlog.md`.

1. **Release-preset caching.** CI builds only `debug`; there is nothing to cache until a release
   job exists.
2. **Caching the apt layer.** Rejected on current evidence (above); recorded so it is not
   re-derived.
3. **A source-only FetchContent cache.** Unneeded if Slice A works.
4. **Test selection or caching test results.** `ctest` is 37-65 s and, once the build is cached,
   the largest fixed cost. Unsound without change-impact analysis.
5. **`ctest -j` in CI.** The CI test step runs serially. A real unrelated win, unmeasured, and a
   parallel-safety question (embedded Python tests, ports) that deserves its own slice.
6. **Per-PR cache writes.** Decided restore-only for quota; revisit with data.
7. **A periodic cold-vs-cached parity check.** A scheduled cold build compared with a cached one
   would turn the one-time correctness proof into a standing guardrail. Not built now: it doubles a
   weekly build, and the invalidation probes cover the same risk at ship time.
8. **A shared/remote ccache for developer machines.** Slice A helps local first builds; a network
   cache is a different trust problem.
9. **Cache housekeeping** (deleting stale caches via the API).
10. **Pinning mutable dependency tags to SHAs.** Found along the way: 9 of 10 dependencies are
    pinned by tag, not commit. Real, unrelated, and it affects the trust model of Slice A.
11. **Reducing header coupling / PCH / unity builds.** `entity_id.hpp` is included by 171 of 254
    TUs; cutting that fan-out helps cold and wide-header builds, which caching cannot. A different
    kind of work.

## Open items for review

- The 7m00s / 9m00s / +30 s floor and the Slice A 20 s floor, restated per-scenario above.
- **Decided:** Slice A lands as its own PR first, as soon as its CI configure number exists,
  before the Slice B campaign.
- `max_size = 500M` is a starting value to be revised from measured restore time.
