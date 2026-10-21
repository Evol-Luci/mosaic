# CI parallel ctest — design

**Status:** spec, written 2026-10-21, awaiting review. Nothing is implemented. No plan exists yet.
No parallel timing has been collected; the only numbers below are the serial baselines and the
hazard audit, and the oracle is fixed *before* any parallel number exists.

**Path: bounded.** The mechanism is one flag on one workflow line. What makes it a slice rather
than a line is the claim underneath it ("1455 independent test processes are safe to run
concurrently, and no case is near its timeout when they do") and the instrument that proves it.

## Why this matters

After the ccache slice a warm CI job is 93-143 s and the serial `ctest --preset debug` step is
35-64 s of it, the largest block compiler caching cannot touch. The reader of this is a maintainer
with one usable block of time; a PR that is red at random costs that block, so the bar is
"never randomly red", not "fast". A CI that is 25 s faster and flaky is worse than the current one.

## Environments (printed beside every number in this document)

| Label | What | Use |
|---|---|---|
| **LOCAL** | 20 cores, 62 GB, gcc 16.2.1, cmake 4.4.3, debug preset, commit `1305684`, no ccache (not installed) | **Shape only, never magnitude** |
| **CI** | `ubuntu-latest`: `ImageOS=ubuntu24 ImageVersion=20261012.334.1 nproc=4`, gcc 13.3.0-6ubuntu2~24.04.1, cmake 3.31.6 (runs 39534182320 and 39530657371, the latter on image `20261018.339.1` per the caching spec) | Magnitude, **within one `ImageVersion` only** |

Never average a timing across an `ImageVersion` change (a rollout silently invalidated the ccache
three times in the last slice). The 4-vCPU runner's RAM (documented as 16 GB) is **not measured**;
the plan adds `free -m` to the existing environment step.

## The old gate was stale

The Build/CI backlog entry said `ctest -j` was "gated on two flaky tests". Both claims were checked.

### Gate 1: `PhysicsSystem::raycast is callable from another thread while update runs`: already fixed

`tests/unit/physics/physics_system_test.cpp:601`. The backlog says it "asserts a race outcome
... needs a deterministic handshake". It got one in `e7927cf` / #230 (2026-10-09): the reader thread
is started, then the main thread waits for `hits > 0` (yielding) with a **5 s steady-clock ceiling**,
and only then runs the 200-tick concurrent phase. The final assertions are main-thread and
deterministic.

**Is 5 s sufficient under `-j4` on 4 vCPUs?** My judgement: yes, by a wide margin, with one named
residual.

- *What the wait needs.* One scheduling of one thread that calls `raycast` once, while nothing else
  holds the physics lock (no tick has run). That is microseconds of work plus one timeslice. The
  contended-runner cost is therefore "time until the kernel next schedules a runnable thread".
- *How bad can that get under `-j4`.* At most 4 test processes run at once on 4 vCPUs. The only
  CPU-heavy case is the single-threaded HUD-underlay case (below); the other tests are
  milliseconds. Even if all 4 slots were CPU-bound, CFS bounds scheduling latency in the low tens of
  milliseconds with a handful of runnable threads per CPU. 5 s is two orders of magnitude of
  headroom over that, not a race with it.
- *Measured (LOCAL, shape only).* The case pinned to 4 cores (`taskset -c 0-3`) with 12 busy-loop
  processes also pinned to those 4 cores (about 3x oversubscription, harsher than `-j4`): **300 / 300
  pass**; 50 consecutive runs took 1.87 s total (about 37 ms each, wait included).
- *The residual.* The ceiling is wall-clock, so a VM that is descheduled for more than 5 s (steal
  time) trips it. Such a runner also breaks the other wall-clock ceilings below, and is an
  environmental failure, not a `-j` effect. It would be classified as such, not as a flake.
- *Why this is not the old failure.* The old failure (2026-10-09, 2-core CI) asserted a count after
  a loop with nothing synchronising reader and loop. The handshake removed the dependency on
  *when* the reader runs; only "eventually, within 5 s" remains.

So Gate 1 is **closed**. It is also not the only wall-clock ceiling in the suite; the full list is
in "Time ceilings" below.

### Gate 2: `threaded ScriptSystem applies script writes one tick later`: unreachable on the ctest path

`tests/unit/script/script_system_test.cpp:191`. The backlog and project memory record a SIGSEGV
that occurs "only under full-suite ordering" (embedded-Python GIL/teardown under thread state
accumulated by prior cases). The claim to check: `catch_discover_tests` gives every case its own
process, so ctest never produces that ordering.

Confirmed from the generated file, `build/debug/tests/mosaic_unit_tests-b12d07c_tests.cmake`
(LOCAL, `1305684`):

```
add_test( [==[threaded ScriptSystem applies script writes one tick later]==] /home/lucievol/Projects/mosaic-ctest-j/build/debug/tests/mosaic_unit_tests [==[threaded ScriptSystem applies script writes one tick later]==]  )
set_tests_properties( [==[threaded ScriptSystem applies script writes one tick later]==] PROPERTIES WORKING_DIRECTORY /home/lucievol/Projects/mosaic-ctest-j/build/debug/tests TIMEOUT 60)
```

- `grep -c '^add_test'` on that file: **1455**; `ctest -N` agrees. Every `add_test` runs the binary
  with exactly one test name as its sole argument. There is no suite-level `add_test`.
- Therefore the full-suite ordering is never constructed by ctest, serially or at any `-j`. `-j`
  changes which *other processes* run alongside, never which cases share a process. The SIGSEGV is
  a hazard of running the binary directly, a different thing, and the older backlog entry already
  says so ("ctest structurally cannot see it").
- The CI workflow comment ("has not reproduced since" the `ScriptWorker::collect()` fix in #208,
  `22d7add`) is consistent: the ctest path cannot reproduce it.
- *Measured (LOCAL, shape only).* The case, pinned to 4 cores under the same 12-spinner load:
  **300 / 300 pass**; and in the serial baseline all 1455 pass.
- *What remains.* The case has a thread handshake inside one process (the #208 class: a window race
  in `ScriptWorker`). CPU contention changes thread timing, so I treat "it passes under contention"
  as supporting evidence only; the mechanism argument is that the race was removed by construction in
  #208, not made unlikely.

So Gate 2 is **inapplicable to the ctest path**. It stays open as a hazard of direct invocation
(backlog, Physics pillar section, the entry "The `[script]` SIGSEGV now has a deterministic, one-command reproduction"; and a Non-goal below).

## The real gate: are 1455 concurrent processes safe?

Method: static reading plus mechanical checks, then one serial run observing side effects. **This is
an audit, not a proof**: static reading cannot show absence, which is why the oracle below exists.

### Concurrency-hazard audit

| # | Hazard | Finding | Class |
|---|---|---|---|
| H1 | **Fixed ports: ENet.** `tests/unit/net/enet_transport_test.cpp` binds UDP 34001-34006, one per case. | Distinct per case, so no two cases collide. Only file in `tests/` with fixed ports (grep for `bind`/`listen`/`port =`/`AF_INET` across all of `tests/`). UDP has no TIME_WAIT, so (b) cannot occur. **But 32768-60999 is the ephemeral range** (`/proc/sys/net/ipv4/ip_local_port_range` here; Ubuntu default), and each ENet client binds an ephemeral UDP port, so a client could take a port another case's server is about to bind. The six cases are adjacent in definition order, so they *do* run together at `-j4`. Estimated well under 1 in 2000 per run, by arithmetic (about 6 servers x a few live ephemeral sockets / 28 232), **not measured**. Failure mode: `create_enet_server` returns null, dereferenced: a bare SIGSEGV. | **Residual, accepted, recorded** (backlog) |
| H2 | **Fixed ports: LiveLink.** `tests/unit/studio/live_link_server_test.cpp` | Uses `start(0)` (kernel-chosen), `SO_REUSEADDR` set. Every case that accepts a client uses a kernel port it never rebinds. Two cases do bind a *known* port: "start() fails on an already-bound port" (holds server1 open: safe) and "port reports an explicitly requested port verbatim" (**probe.start(0), stop(), then bind that number**): a microsecond window in which a concurrent bind could take it, ~1/28 000 per overlapping bind. No connection is accepted on the probed port, so no TIME_WAIT. | **Residual, accepted, recorded** (backlog) |
| H3 | **Other sockets.** | No other `socket`/`bind`/`listen`/`enet_host_create` in `tests/`. `app/main.cpp` hosts ENet only on `--host`, which no unit test invokes. Audio uses the miniaudio **null** device (`create_miniaudio_backend(true)`), so no sound device is opened. | None |
| H4 | **Shared filesystem: literal tmp paths.** | Every file that touches `temp_directory_path()`/`tmpnam` was read (21 files). Names fall into: (i) **pid-keyed** (`mosaic_t3_*`, `t3r`, `t4`, `t5`, `a3`, `font`, `hotreload`, `pfw`, `mcp_*`): unique per concurrent process by construction; (ii) **time-keyed** (`import_plugins`, `plugin_host`: `steady_clock` nanoseconds); (iii) **`tmpnam`** (`scene_io`, `scene_save_roundtrip`, `populate_world`): random 6-char suffix, ~5.7e10 space; (iv) **literal-tag** (`package_sources_registry`, `package_source`, `package_install`, `studio_prefs`, `git_test`, `mcp_tools` plugin-list): the full path is a fixed function of a tag. A mechanical scan (165 tag/literal uses across the 14 files that have them, grouped per `TEST_CASE`) found **no tag or `mosaic_*` literal used by two different cases**. Literal-tag paths therefore collide only if two cases share a tag, and none does. Cross-file prefixes are distinct. | **None found**; the guarantee is "no shared tag today", not structural |
| H5 | **Writes into the source/build tree.** `WORKING_DIRECTORY` is `build/debug/tests` for every test. | After a full serial run: `git status --porcelain --ignored` outside `build/` is empty; no file under `build/debug/tests` newer than the binary other than the cmake file generated at build time; `/tmp` `mosaic_*` entry count unchanged (93 before, 93 after: the suite neither leaks nor reuses unpredictably). Tests read the source tree (`MOSAIC_TEST_DIR`, fixtures, shader sources) read-only. | **None** |
| H6 | **User config / environment.** | `XDG_CONFIG_HOME` is set per-process by `setenv` in the cases that need it (`package_sources_registry`, `mcp_tools`); `setenv` is process-local so it cannot leak across test processes. `~/.config/mosaic` mtimes were unchanged by a full run. CI's `HOME` is clean. `GIT_SSH_COMMAND` read-only. | **None** |
| H7 | **Embedded Python.** | `py::initialize_interpreter()` runs once per process (`std::call_once`, `python_script_engine.cpp:37-60`); an interpreter is process-local memory, so 4 processes have 4 independent interpreters and GILs. The only shared state is on disk: `.pyc` writes (atomic rename) into the plugin/script directories, which are per-process temp dirs (H4) or the read-only source tree (and the source tree stayed clean, H5). `PYTHONHOME` is read-only. | **None** (isolation is by process, not by care) |
| H8 | **Memory.** | LOCAL, max RSS of **all 1455 cases** (each run alone): **median 19 MB, maximum 85 MB** (mcp_tools plugin_script_*), so 4 concurrent copies need about 340 MB worst case. The 298 MB debug binary is file-backed, mapped shared between processes; it is not 4x. Against a 16 GB runner (documented, unmeasured) this is not a constraint. | **None** |
| H9 | **Per-test `TIMEOUT 60`** (`tests/CMakeLists.txt:260`). | See next section. **This is the live risk.** | **Real finding** |
| H10 | **GPU / Vulkan.** | No test creates a device, an instance or a window: the only matches for `Vulkan`/`glfw` in `tests/` are comments, textual source-contract tests (read `.cpp` files as strings), and `Vulkan::Vulkan` as a link dependency. `vulkan_validation_contract_test.cpp:10` states "no test creates a VkDevice". No GPU contention is possible. | **None** |
| H11 | **Wall-clock ceilings inside tests.** | See "Time ceilings". | Accepted with margins |
| H12 | **Cross-process loopback slowness (sandbox property).** | Every socket test in the suite is **same-process** loopback (server and client in one test). The sandbox's multi-second cross-process delay does not apply, and a slow net test locally must not be read as a `-j` problem. | n/a |
| H13 | **Python/threads inside a process.** | Unchanged by `-j`: thread counts per process do not depend on the number of processes. | n/a |

### The real risk: one slow case against `TIMEOUT 60`

The 2026-10-07 sizing comment in `tests/CMakeLists.txt` records the slowest case at 1.11 s and says
"60 s is ~54x the slowest". That is **no longer true**. Serial baselines, all 1455 passing:

| Env | Case | Serial duration | Share of the test step | Headroom to 60 s |
|---|---|---|---|---|
| LOCAL | `the default scene's HUD underlay image draws beneath the checker` (`tests/unit/app/demo_scene_test.cpp:116`) | **16.5 s** (3 repeats: 16.39, 16.56, 16.55; user 16.37 s of 16.49 s wall, so single-threaded CPU) | 16.6 of 46.9 s case-sum (35%); step wall 47.6 s | 3.6x |
| CI | same case | **23.36 s** (run 39534182320) and **23.42 s** (39530657371) | 23.4 of 60.8 s (38%) and of 60.45 s | **2.6x** |

Next slowest on both: `PluginFileWatcher.poll detects a modified file's mtime` at 1.11 s (it
`sleep_for(1100 ms)`; contention cannot shorten it), then 0.4 s (LOCAL) / 0.62 s (CI) for the IBL
furnace cases. After the first, nothing is within 40x of the timeout.

Consequences:

1. **TIMEOUT headroom.** Under `-j4` this CPU-bound single-threaded case shares 4 vCPUs with at
   most 3 other processes. If the runner's 4 vCPUs are 2 hyperthreaded cores, a CPU-bound case can
   plausibly slow by 1.2-1.6x when its neighbours are also busy; 23.4 s x 1.7 = 40 s, still under
   60 s, and 2.6x is more than any plausible inflation. But this is the single case where `-j4` has
   a real way to produce a red run, so the oracle watches it by name (O6).
2. **It is the floor of the speed-up.** No schedule can make the test step shorter than its longest
   case: the best possible CI step is about 23.4 s, not 60.8 / 4 = 15 s. The case starts at roughly
   test 413 of 1455, i.e. about 3 s into a `-j4` step, which is early enough that scheduling order
   costs nothing (no cost data exists on a fresh checkout, so ctest uses definition order).
3. **It is not caused by `-j` and not a `-j` fix.** It is a regression nobody saw (arrived with
   #241; `TIMEOUT` was sized when the slowest case was 1.11 s). Root cause **not established**:
   it is `render_system->render(...)` on the default scene, and ptrace is blocked in this sandbox so
   no profile was taken. Every sibling that builds the same scene without `render()` takes about
   7 ms. Backlog (Non-goal 1).

### Time ceilings (wall-clock assertions in tests)

| Where | Ceiling | Typical (CI/LOCAL serial) | Margin |
|---|---|---|---|
| `physics_system_test.cpp:649` raycast reader | 5 s | microseconds | >1000x |
| `physics_contact_reentry_test.cpp:42` `kWatchdogMs` | 3 s | 0.2 s for the slowest of its two cases (LOCAL) | 14x |
| `enet_transport_test.cpp` `poll_until` | 200x5 ms = 1 s (400x5 = 2 s for 2-client) | 8-26 ms | 40-125x |
| `live_link_server_test.cpp` `poll_until` | 200x5 ms = 1 s | 7-12 ms | 80-140x |
| ctest `TIMEOUT` | 60 s | 23.4 s worst (above) | 2.6x |

All but the last have at least 14x margin. The ENet/LiveLink loops poll same-process loopback.

## Design

**One change to `.github/workflows/ci.yml`:** the Unit tests step runs
`ctest --preset debug --output-on-failure -j"$(nproc)"` (the build step already uses
`-j"$(nproc)"`, so the two stay consistent; CI prints `nproc` in the environment step). Not
`-jN` with a literal `4`, so a runner-size change moves it with the build. Not in
`CMakePresets.json`: that would change every developer's local default (Non-goal 6).

Everything else in this slice is the *instrument*, not the change:

- The oracle's checks (below), implemented as a small script under `tools/ci/` with its own
  `unittest` file, following the existing `check_dependency_rules.py` / `check_test_case_names.py`
  pattern, run in the workflow after the tests, **or** kept out of the final workflow and used only
  in the measurement campaign. That choice is for the plan and for review; the spec requires only
  that the oracle runs at least in the campaign.
- `--output-junit` so a per-test outcome map exists.
- `--no-tests=error`, so a discovery regression that finds zero tests is an error, not a vacuous
  green.
- Timing lines printed beside the environment line.

Rejected alternatives: **`ctest --test-load`** (no evidence load throttling is needed at 340 MB and
4 slots); **`--schedule-random`** (would randomise the very ordering we want stable, and hides a
failure's reproducibility); **a `RESOURCE_LOCK` on the ENet cases** (a CMake change to close a
residual estimated well below 1 in 2000; recorded, not done); **`RUN_SERIAL` for the HUD case**
(would serialise 23 s of the step against everything else and cancel the win).

## The oracle (fixed before any parallel number is collected)

"Tests pass" is not an oracle: a filtered run passes. These are the observations that would prove
this work wrong. **N** below is always computed in the same job from `ctest -N` ("Total Tests:") and
cross-checked against `grep -c '^add_test'` on the generated `*_tests.cmake`; it is **never a literal
in the check** (1455 at `1305684`, quoted here only as a reference point). A literal would rot the
first time a test is added and teach people to edit the check.

**O1 — count run.** The number of testcases in the `-jK` JUnit file equals N, equals the serial
run's count. A parallel invocation that runs fewer is filtering, not parallelising, and fails here.
*Negative control, run once before trusting the check:* `ctest -j4 -R ENet` against the same build
must turn O1 **red**.

**O2 — pass count.** Passed = N; failures = 0; skipped = 0; disabled = 0; "Not Run" = 0.

**O3 — per-test outcome cannot depend on `-j`.** The map `{test name -> status}` from `-jK` equals the
serial map: identical name sets, identical status for every name. Compared as a diff of the two maps,
not as two summary counts (two runs with the same counts can disagree on which tests). *Negative
control:* a deliberately failing probe test case (on a throwaway branch only, never merged) must show
up as a name-level difference and as O2 red.

**O4 — the mechanism moves.** Efficiency = (sum of per-test durations) / (step wall) is about 1.0
serially (LOCAL: 46.9 / 47.6 = 0.98) and must be **> 1.5** under `-j4` on CI. A value near 1.0 means
`-j` did not take effect. A value above 4.0 is impossible on 4 vCPUs and means a measurement error.
Sum of durations comes from the JUnit file; wall from ctest's own "Total Test time (real)".

**O5 — the physical floor holds.** Step wall >= the longest single case's duration in the same run
(about 23.4 s on CI). Wall below that is a measurement error, not a speed-up.

**O6 — timeout headroom.** In every `-j4` CI run, the longest single case is **<= 40 s** (two-thirds
of `TIMEOUT 60`) and I report its inflation factor against the serial duration of the *same*
`ImageVersion`. 40 s was chosen as 23.4 s x 1.7, i.e. generous for contention but leaving real
margin. If any case exceeds 40 s, `-j4` ships only with a stated fix (raise the timeout with a
measured justification, or fix the case), never silently.

**O7 — repeatability.** LOCAL: 30 consecutive `taskset -c 0-3 ctest -j4` runs, O1-O3 hold in all 30
(shape only). CI: at least 10 consecutive green runs on the PR branch with O1-O6 holding, across
`ImageVersion`s only if they all complete within one image. **These runs can refute and cannot
prove**: 0 failures in n runs bounds the true failure rate only to about 3/n at 95% (30% for n = 10,
10% for n = 30). The proof of safety is the audit above; the runs are a net for what the audit
cannot see. I will say this in the results rather than claim "stable".

**Speed prediction (CI, written now).** Step wall **25-40 s** against the 60.8 s serial baseline
(35-64 s historically), efficiency 1.5-2.6, job saving about **20-35 s of 93-143 s**. If the step
exceeds **50 s**, the premise (CPU-bound, parallelisable) is wrong and I stop and report. If it is
below **23 s**, O5 says the measurement is wrong. The prediction is deliberately a range; the
flat 4x that `60.8 / 4` suggests is excluded by the longest-case floor.

### Classification of a red run (before it counts as data)

Every red run is classified into exactly one of: **known flake**, **named environmental hazard**,
or **real finding**.

- **O1, O2 or O3 red is never a flake.** A changed count or a changed per-test outcome is a
  correctness failure of this slice and stops the campaign.
- *Known flakes* are only the ones with a recorded mechanism: the threaded script test's
  direct-invocation SIGSEGV (not reachable here), and the physics raycast test on a starved runner
  (its message says so). A name on the list is necessary, not sufficient: the same case red twice in
  a campaign is a finding.
- *Named environmental hazards:* LunarG apt step pinned to `noble` (a runner-image rollout); a
  runner assigned no steps (job fails in ~2 s with 0 steps); the Node 20 deprecation annotation on
  `actions/cache@v4` and `actions/checkout@v4` (pre-existing, not ours, **not touched**); the
  `mosaic_unit_tests` ~170 MB link intermittently failing under memory pressure (grep for a real
  compiler error; if none, re-run the build; not a finding).
- Anything else is a **real finding** until shown otherwise.

### Measurement protocol

- Print `ImageOS`, `ImageVersion`, `nproc`, gcc and cmake (already printed) next to every timing.
  Compare only within one `ImageVersion`; if the image changes mid-campaign, restart the comparison
  for that image rather than averaging.
- The serial baseline is re-measured **in the same campaign and on the same image** as the parallel
  numbers. Preferred mechanism for the campaign: a throwaway workflow variant that runs the suite
  once serially and once at `-j"$(nproc)"` in the *same job* (same machine, same cold/warm cache state)
  and compares the two JUnit maps. That variant exists only on a throwaway branch.
- LOCAL timings are labelled LOCAL and described as shape.

## Standing constraints

- First-party actions only; no new third-party actions; no change to cache keys; no change to the
  `actions/cache` or `actions/checkout` versions.
- Authority: the slice edits only `.github/workflows/ci.yml`, `tools/ci/` (if the oracle script
  lands), docs, and this spec/plan. It does not merge, does not change repo settings, secrets or
  permissions, and does not touch `.claude/`.
- If any `TEST_CASE` is added or renamed: no leading `--`, balanced `[`, and a comma is escaped as
  `\,` when filtering by name from a script (ctest already escapes them).

## Non-goals

Each has a matching entry in the "CI and build" section of
`docs/project/deferred-work-backlog.md`, and where a tier row exists it is listed in "Effort vs.
impact".

1. **Root-causing the 16.5 s HUD-underlay case** (`demo_scene_test.cpp:116`). Separable; fixing it
   is worth more than `-j` (serial step about 37 s, parallel floor about 10 s). Backlog: "The
   default-scene HUD-underlay case takes 16.5 s locally and 23.4 s on CI"; 🟢 row.
2. **Moving the ENet test ports out of the ephemeral range, or a `RESOURCE_LOCK`.** Closes H1's
   residual. Backlog: "ENet test ports sit inside the Linux ephemeral range"; 🟢 row.
3. **The LiveLink release-then-rebind window** (H2). Backlog: "`LiveLinkServer::port reports an
   explicitly requested port verbatim` releases then rebinds".
4. **Longest-first scheduling (`COST`).** Free today because the long case starts early. Backlog:
   "Longest-first scheduling for `ctest -j`".
5. **A per-test duration budget in CI.** `TIMEOUT 60` is a hang backstop; the HUD case grew unseen.
   Backlog: "A per-test duration budget". Depends on non-goal 1.
6. **`execution.jobs` in the `debug` test preset (local `ctest -j`).** Changes every developer's
   default. Backlog: "`execution.jobs` in the `debug` test preset".
7. **A CI job that runs `mosaic_unit_tests` as one process** to cover the full-suite-order SIGSEGV.
   Backlog: "A CI job that runs `mosaic_unit_tests` as one process".
8. **Fixing the full-suite-order SIGSEGV itself** (embedded-Python GIL/teardown). Already tracked in
   the Physics pillar section (the `[script]` SIGSEGV entry); this slice only establishes that ctest cannot reach it.
9. **Test selection or caching results**, and the apt and configure steps. Already in the CI and
   build section (test selection) or out of scope (apt/configure are not test parallelism).

## Open items for review

- **The 40 s ceiling in O6 and the `> 1.5` in O4** are my choices; they are open to ruling before
  any data is collected.
- **Whether the oracle script ships in the workflow or stays in the campaign.** A standing check
  catches a future discovery regression; it also adds a step and a thing to maintain.
- **`-j"$(nproc)"` vs a literal.** I chose `nproc`; if the runner image ever reports more vCPUs the
  slot count rises with it and H8 and H1 scale accordingly.
- **Whether to accept H1 and H2 as residuals** or fold the ENet port move into this slice. I
  recommend accepting and recording: it is a test-code change to close an estimate of well under
  1 in 2000 per run, and the oracle's O3 would catch it if it fired.
- **CI RAM is documented, not measured.**
- **The HUD case's cause is unknown.** If a reviewer prefers, fixing it first changes the whole
  speed prediction (floor drops to about 10 s) but is a different slice.
