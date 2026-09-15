# Plugin runtime sandboxing

Studio plugins (`MOSAIC_MENU_ITEMS`/`MOSAIC_PANELS`) and asset importers
(`MOSAIC_IMPORTERS`) run with a restricted set of Python builtins and a
restricted `__import__` — gameplay `ScriptComponent` scripts do not
(they're first-party code, same trust level as the rest of a project).

## An honest limitation

**This is a defense-in-depth deterrent, not a hard security boundary.**
CPython has no supported way to fully sandbox arbitrary code running
in-process. Two distinct, language-fundamental escapes remain open to a
determined attacker with knowledge of Python internals:

- The classic `().__class__.__bases__[0].__subclasses__()`-style
  object-graph walk reaches arbitrary loaded classes without ever
  calling a blocked builtin or `import` statement.
- Any function, method, or closure reachable from an allowed module —
  directly, via a class's own methods, or via a constructed instance's
  bound methods — carries a real `__globals__` back to that module's
  unrestricted namespace. This was investigated in depth (see
  `modules/script_python/restricted_builtins.cpp` for the full
  round-by-round history) and found **not** to be closable the way the
  module-attribute-graph bug was: `except` clauses use CPython's hard
  `PyType_Check` (no `__instancecheck__` support), so a class proxy can
  never be caught by one, and making the proxy a literal subclass of
  the real exception type doesn't fix this either — it silently stops
  catching the real exception a module raises internally, a correctness
  regression masquerading as a security fix. Instance proxying is
  separately impossible: Python's special-method dispatch (operators,
  `str()`, `isinstance()`, hashing) bypasses `__getattr__` entirely and
  resolves via the type, not the instance.

Both are the same category of limitation, not different severities —
an attacker sophisticated enough to use either already has the other.
Real isolation for genuinely untrusted third-party code needs OS-level
separation (a container, a restricted process, or a
[PEP 578](https://peps.python.org/pep-0578/) audit hook with a
thread-local "restricted frame" flag, indifferent to which attribute
path reached the dangerous call) — this is the "dynamic sandboxed
execution" layer `docs/design/marketplace.md` already named as a
distinct, harder, unscheduled piece of a future marketplace review
pipeline. This slice raises the bar for the common case — blocks
`import os`/`subprocess`/`socket`/`ctypes`, blocks `eval()`/`exec()`/
`compile()` outright, blocks `open()` too for Studio plugins
specifically, and closes every module-attribute and direct-function
leak found during review — without claiming more than that.

## What's restricted, and where

`install_restricted_builtins()` (`modules/script_python/
restricted_builtins.hpp`) removes these builtins entirely: `eval`,
`exec`, `compile`, `__import__`, `input`, `breakpoint`, `exit`, `quit`,
`help`, `vars`, `globals`, `locals` — plus `open`, unless the caller
passes `allow_open = true`. It then reinstalls `__import__` as a
wrapper checking the requested module against an allowlist — `json`,
`csv`, `math`, `re`, `random`, `time`, `datetime`, `collections`,
`itertools`, `functools`, `string`, `copy`, `enum`, `dataclasses`,
`typing`, `textwrap`, `uuid` — raising `ImportError` for anything else,
including `sys` (deliberately excluded — powerful enough on its own,
via `sys.modules`/`sys._getframe`, to defeat the point).

Every value that crosses the boundary from an allowed module into
plugin code is guarded, not just the module itself:

- **`ModuleProxy`** wraps every returned module. Denies `__dict__`,
  `__loader__`, `__spec__`, and `__builtins__` outright by name (a
  module's real namespace dict, and importlib objects whose own methods
  can execute code), and denies any other attribute that resolves to
  ANOTHER module object or a callable — the latter gets wrapped rather
  than denied (see `CallableProxy` below). This closes the "reach a
  dangerous module via an allowed module's attribute graph" bug at any
  depth (`uuid.os`, `json.codecs.sys`, `json.__dict__['decoder']`, all
  confirmed blocked).
- **`CallableProxy`** wraps every function or bound method a
  `ModuleProxy` resolves. Forwards `__call__` to the real callable
  unchanged (so `json.dumps(...)` still works exactly as before), but
  denies the same reflection-attribute set on the callable itself —
  `__globals__` above all (the real module's actual namespace,
  including its genuine `__builtins__` entry) — plus `__code__`,
  `__closure__`, `__self__`, `__func__`. The callable's **return
  value** is guarded too (`functools.lru_cache(...)` returns a fresh,
  otherwise-unwrapped closure with the same `__globals__` leak).

Both proxies are real C++ classes (pybind11-registered types), not
Python-level classes — this matters: a Python-level implementation was
tried twice and broken twice in review (an instance attribute readable
via ordinary attribute lookup, then a module-level closure reachable
via `type(proxy).__getattr__.__globals__`). A pybind11-bound method has
no `__globals__` at all, and the instance carries no `__dict__` unless
`py::dynamic_attr()` is requested (it is not) — closing that whole
avenue at the language level rather than by patching individual probes.

What's **not** wrapped, deliberately: classes returned from an allowed
module (e.g. `collections.namedtuple(...)`'s return value), and
instances a class's constructor produces. See "An honest limitation"
above for why — this isn't an oversight, it's a boundary found to be
uncrossable without breaking legitimate plugin code.

This restriction is applied to:
- **Studio plugins**, unconditionally, `open` blocked
  (`create_studio_plugin_script_engine()` — every caller is already
  plugin code, and a Studio plugin's job is UI/gameplay manipulation via
  `get_component`/`set_component`/`imgui_*`, never reading arbitrary
  files).
- **Asset importers**, via a new, separate factory
  (`create_restricted_python_script_engine()`, `allow_open = true`),
  used only by `ImportPluginHost`'s production construction site
  (`app/asset/asset_io.hpp`). `open` stays available here deliberately —
  an asset importer's entire purpose is reading its source file (the
  real, shipped `tools/example_plugins/csv_importer.py` does exactly
  `with open(source_path) as f: ...`); blocking it would break the
  feature, not secure it. This isn't a new gap: import plugins could
  already read arbitrary files before this slice (fully unrestricted);
  what this slice actually adds for that path is blocking
  `os`/`subprocess`/`socket`/`ctypes`/`eval`/`exec`.

**Not** applied to gameplay `ScriptComponent` scripts
(`create_python_script_engine()` — unchanged, still fully unrestricted).
Restricting first-party project code would block legitimate needs (a save
system wanting real file I/O, say) for no security benefit, since that
code was never less-trusted to begin with.

## Extending the module allowlist

If a real, legitimate plugin need shows up for a module not on the list,
add it to `kAllowed` (`allowed_modules()` in `restricted_builtins.cpp`)
deliberately — weigh what that module's own top-level code does (does
it import anything sensitive under the hood?), though the `ModuleProxy`/
`CallableProxy` pair means a module no longer needs to be individually
proven leak-free before being trusted, only worth exposing at all.
Don't work around a restriction by widening the builtins blocklist
logic instead; the module allowlist is the intended, auditable
extension point.

## Review history

This mechanism went through 9 adversarial review rounds (escalating
from sonnet to opus for the final two) before being considered closed
for this slice, finding 8 real, distinct, verified bugs in sequence —
each fixed, none dismissed without investigation. The full narrative
(what broke, how it was found, why the eventual fix is structurally
sound rather than another point patch) lives in
`docs/superpowers/specs/2026-09-15-plugin-runtime-sandboxing-design.md`
and in the doc comments above `ModuleProxy`/`CallableProxy` in
`restricted_builtins.cpp` itself — read those before touching this
mechanism again; several tempting-looking "obvious" fixes in this
history were tried and found to reopen a worse hole than the one they
closed.
