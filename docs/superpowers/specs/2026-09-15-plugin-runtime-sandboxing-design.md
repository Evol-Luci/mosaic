# Plugin Runtime Sandboxing — Design

## Goal (subsystem-hardening backlog item split out of marketplace planning)

Restrict what *less-trusted* Python code — Studio plugins
(`MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`) and asset importers
(`MOSAIC_IMPORTERS`) — can do at runtime, independent of any future
marketplace review pipeline. Investigating the current codebase (during
`docs/design/marketplace.md`'s planning conversation) found **zero existing
sandboxing of any kind**: every plugin `.py` file runs as fully-privileged
Python in the same process as the editor, with the complete standard
library (`os`, `subprocess`, `socket`, `ctypes`) and dangerous builtins
(`open`, `eval`, `exec`, `compile`, `__import__`) available. This is a real
gap in already-shipped functionality (Plugin SDK slices 1-3, all merged),
not a hypothetical future-marketplace concern — this slice closes it now.

## An honest limitation, stated up front

**This is a defense-in-depth deterrent, not a hard security boundary.**
CPython has no supported way to fully sandbox arbitrary code running
in-process: a determined attacker with knowledge of Python internals can
still escape a restricted-builtins/restricted-import scheme via object
introspection (the classic
`().__class__.__bases__[0].__subclasses__()`-style chain reaches arbitrary
loaded classes without ever calling a blocked builtin or `import`
statement). Real isolation for genuinely untrusted third-party code
requires OS-level separation (a container, a restricted process, or
similar) — this is exactly the "dynamic sandboxed execution" layer
`docs/design/marketplace.md` already named as a distinct, harder,
unscheduled piece of the future marketplace review pipeline. This slice
raises the bar for the common case (blocks the obvious `import os;
os.system(...)` attack, blocks accidental misuse, blocks `eval()`/`exec()`
outright, and blocks `open()` too for Studio plugins specifically — asset
importers keep it, since reading their source file is their whole job)
without claiming to solve the general problem. Documented here so it's
never mistaken for more than it is.

## Scope: plugins/importers only, not gameplay scripts

Gameplay `ScriptComponent` code is first-party — the project's own
developer writes it, same trust level as any other code in their project.
Restricting it the same way plugins are restricted would block legitimate
game logic (e.g. a save system wanting real file I/O) for no security
benefit, since that code was never less-trusted to begin with. This slice
restricts exactly two execution paths:

- **Studio plugins** (`studio::PluginHost`, via
  `create_studio_plugin_script_engine()`) — always plugin code, no
  legitimate unrestricted use case exists today.
- **Asset importers** (`app::ImportPluginHost`, via a **new**
  `create_restricted_python_script_engine()`) — currently shares
  `create_python_script_engine()` with gameplay scripts (same factory,
  same unrestricted engine type); this slice gives importers their own,
  separate, restricted factory instead.

`create_python_script_engine()` itself — used by `ScriptSystem` for
gameplay `ScriptComponent`s — is **untouched**: same signature, same
unrestricted behavior, zero risk to its ~25 existing call sites across
production and test code.

## Mechanism: `install_restricted_builtins`

A new function, `mosaic::script::python::install_restricted_builtins(
pybind11::dict& globals, bool allow_open = false)` (new file
`modules/script_python/restricted_builtins.hpp`/`.cpp`, in the existing
`mosaic_script_python` module — no new module needed, both
`PythonScriptEngine` and `StudioPluginScriptEngine` already depend on this
one), replaces `globals["__builtins__"]` with a **copy** of the real
builtins dict (via Python's own `.copy()` — never alias the live
`builtins.__dict__`, which would mutate it process-wide for every other
engine including gameplay scripts) with a small blocklist removed:
`eval`, `exec`, `compile`, `__import__`, `input`, `breakpoint`, `exit`,
`quit`, `help`, `vars`, `globals`, `locals`, and — unless `allow_open` is
set — `open`. Everything else stays — including exception types (needed
for `try`/`except` to work at all) and `__build_class__` (needed for
`class Foo:` statements to compile and run).

**`allow_open` exists because asset importers' entire purpose is reading
their source file.** `MOSAIC_IMPORTERS` functions receive a source path
and are expected to do their own file I/O — the real, already-shipped
`tools/example_plugins/csv_importer.py` does exactly
`with open(source_path) as f: ...`. Blocking `open()` for import plugins
wouldn't secure anything; it would break the feature entirely. Studio
plugins (menu items/panels) have no such legitimate need — their job is
UI/gameplay manipulation via `get_component`/`set_component`/`imgui_*`,
never reading arbitrary files — so `open` stays blocked for them
(`allow_open` defaults to `false`, used unconditionally in
`StudioPluginScriptEngine`; `PythonScriptEngine`'s restricted mode passes
`allow_open = true`, since it's used only for import plugins — see
Scope). This is not a new gap: import plugins can already read arbitrary
files today (unrestricted); this slice's `os`/`subprocess`/`socket`/
`ctypes`/`eval`/`exec` restrictions are the actual security improvement
for that path, not a claim that file-read access itself is being removed.

`__import__` is then reinstalled as a restricted wrapper: it checks the
requested top-level module name against a small allowlist (`json`, `csv`,
`math`, `re`, `random`, `time`, `datetime`, `collections`, `itertools`,
`functools`, `string`, `copy`, `enum`, `dataclasses`, `typing`,
`textwrap`, `uuid`) and raises a real Python `ImportError` naming the
blocked module for anything else, otherwise forwarding to the real
`__import__` unchanged (so `from json import dumps` and `import json`
both work normally for anything on the allowlist). `sys` is deliberately
**not** on the allowlist — it's powerful enough (`sys.modules`
manipulation, `sys._getframe`) to defeat the restriction on its own.

Blocklist over hand-curated allowlist for builtins specifically (as
opposed to modules, which use an allowlist): CPython ships ~150 builtin
names: hand-allowlisting risks silently omitting something a legitimate
plugin needs (breaking existing plugins for no security gain), where a
short, explicit blocklist of the genuinely dangerous ones is easy to
audit and gets the security-relevant removals right without guessing at
every safe name.

## Files

- Create: `modules/script_python/restricted_builtins.hpp`,
  `modules/script_python/restricted_builtins.cpp` —
  `install_restricted_builtins(pybind11::dict&)`.
- Modify: `modules/script_python/python_script_engine.hpp`/`.cpp` —
  `PythonScriptEngine`'s constructor gains a private `bool restricted =
  false` parameter (switches between the existing full-builtins line and
  `install_restricted_builtins`); a new public
  `create_restricted_python_script_engine()` factory (same zero-argument
  signature as `create_python_script_engine()`, so it's a drop-in
  `EngineFactory` just like the existing one — no lambda wrapping needed
  at call sites). `create_python_script_engine()` itself is unchanged.
- Modify: `modules/script_python_studio/studio_plugin_script_engine.cpp`
  — `StudioPluginScriptEngine`'s constructor calls
  `install_restricted_builtins(globals_)` instead of the current
  full-builtins line. No signature change; `create_studio_plugin_script_engine()`
  is unchanged, every existing call site gets the restriction for free.
- Modify: `app/asset/asset_io.hpp` — the one production `ImportPluginHost`
  construction site switches from `create_python_script_engine` to
  `create_restricted_python_script_engine`.
- Create: `tests/unit/script/python_script_engine_test.cpp` — direct
  tests of both factories (not previously covered by a dedicated test
  file; existing coverage is all indirect via `ScriptSystem`/`PluginHost`).
- Create: `tests/unit/script/studio_plugin_script_engine_test.cpp` —
  direct tests of `create_studio_plugin_script_engine()`'s restriction.
- Modify: `tests/CMakeLists.txt` — the two new test files.
- Modify: `tests/unit/app/import_plugins_test.cpp` — at least one case
  confirming `ImportPluginHost` in production shape (constructed with the
  restricted factory) actually rejects a dangerous import; the existing
  tests that construct `ImportPluginHost` with `create_python_script_engine`
  directly (testing importer *logic*, not the security boundary) are left
  as-is, since they're deliberately testing something else.
- Create: `docs/design/plugin-sandboxing.md` — the honest-limitation
  framing above, the exact blocklist/allowlist, and how to extend the
  module allowlist later if a legitimate plugin needs something more.
- Modify: `docs/project/next-steps.md` — new dated entry.

## Testing

- **`python_script_engine_test.cpp`**: an unrestricted engine
  (`create_python_script_engine()`) can `import os`/`open`/`eval` — a
  regression guard proving gameplay scripts are genuinely untouched. The
  import-plugin-restricted engine (`create_restricted_python_script_engine()`,
  `allow_open = true`) fails (`ok=false`, error mentions the module name)
  on `import os`, `import subprocess`, `import socket`, `import ctypes`,
  `import sys`, bare `eval(...)`, bare `exec(...)` — each its own case —
  but **can** still call `open(...)` (that's the whole point of
  `allow_open`). A regression case confirms this explicitly: `open`
  succeeds on the import-restricted engine but is confirmed blocked on the
  Studio-plugin-restricted engine (`allow_open = false`), so the two
  profiles aren't accidentally identical. Both restricted profiles can
  still: `print`, raise/catch a built-in exception (`try: 1/0 \n except
  ZeroDivisionError: ...`), define a class (`class Foo: pass` —
  regression guard on `__build_class__`), and `import json`/`import math`
  and use them successfully.
- **`studio_plugin_script_engine_test.cpp`**: mirrors the restricted
  cases above against `create_studio_plugin_script_engine()`, plus one
  case confirming its existing `imgui_*` globals and `bind_world`-injected
  gameplay globals (`get_component` etc.) are unaffected by the
  restriction (the blocklist only touches Python builtins, never
  anything `bind_world`/`install_imgui_globals` install afterward).
- **`import_plugins_test.cpp`**: one new case constructing
  `ImportPluginHost` with `create_restricted_python_script_engine` (the
  actual production factory) and a fixture plugin file whose importer
  function attempts `import os`, asserting the invocation fails cleanly
  rather than succeeding with OS access.

## Non-goals

- OS-level process/container isolation ("dynamic sandboxed execution") —
  explicitly the harder, separate, unscheduled layer named in
  `docs/design/marketplace.md`.
- Restricting gameplay `ScriptComponent` scripts — out of scope by design
  (first-party trusted code); see Scope above.
- Static or dynamic analysis of submitted marketplace packages before
  merge — a CI-time review-pipeline concern, unrelated to what a plugin
  can do once it's already running locally.
- Making the module allowlist configurable per-project — a fixed,
  conservative allowlist for now; extend the hardcoded list later if a
  real, legitimate need shows up (documented in
  `docs/design/plugin-sandboxing.md` as the extension point).
