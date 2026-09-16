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

**A second, equally fundamental escape, found and deliberately not
chased further: any function, method, or closure reachable from an
allowed module carries a real `__globals__` back to that module's
unrestricted namespace.** This was found in stages — a module-level
function accessed directly (`json.dumps.__globals__`), a class's own
methods (`json.JSONDecodeError.__init__.__globals__`), and a factory
function's return value (`functools.lru_cache(...).__globals__`) — and
each stage was investigated for a real fix, not just documented away
on first sight (see `modules/script_python/restricted_builtins.cpp`
for the full round-by-round history: `CallableProxy` genuinely closes
the first and third shapes). What remains open — a class's methods,
and the bound methods of any instance a class's constructor
returns — was investigated as a candidate for the same treatment and
found to be **not** a bounded extension of the same mechanism, for
reasons verified live against the interpreter, not merely argued:
- `except SomeAllowedException:` uses CPython's hard `PyType_Check`
  and does not honor `__instancecheck__` — a non-type class proxy can
  never be caught by an `except` clause at all.
- Making the proxy a literal subclass of the real exception type
  doesn't fix this either: a subclass proxy does not catch instances
  of the real type a module raises internally, which would silently
  stop existing plugin exception-handling code from working — a
  correctness regression masquerading as a security fix.
- Instances a class's constructor returns would need their own proxy
  layer, and Python's special-method dispatch (operators, `str()`,
  `isinstance()`, hashing) bypasses `__getattr__` entirely and
  resolves via the type, not the instance — a forwarding instance
  proxy silently loses arithmetic, string conversion, and type
  identity for exactly the kind of value a plugin most needs to use
  normally.
- This cascades rather than terminating: a correct class proxy would
  still only relocate the leak to the instances it constructs, which
  need their own (impossible, per above) proxy layer in turn.

This is the same category of limitation as the `__subclasses__()`
escape above, not a new one — both require CPython-internals literacy
an attacker exploiting the *first* limitation already has, so closing
this second one buys no marginal deterrence against a determined
attacker while risking real correctness regressions for honest plugin
code. The actual mechanism that *would* close this at its root is a
[PEP 578](https://peps.python.org/pep-0578/) audit hook
(`sys.addaudithook`) with a thread-local "restricted frame" flag,
denying `exec`/`compile`/`import`/`subprocess.Popen`/`os.system`/
`open` while the flag is set — indifferent to which attribute path a
plugin used to reach the dangerous call, since every path found so far
terminates in one of those. That belongs to the same distinct, harder,
unscheduled "dynamic sandboxed execution" / OS-level isolation layer
already named above, not this slice.

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
`math`, `re`, `time`, `datetime`, `itertools`, `functools`, `string`,
`copy`, `textwrap`) and raises a real Python `ImportError` naming the
blocked module for anything else, otherwise forwarding to the real
`__import__` unchanged (so `from json import dumps` and `import json`
both work normally for anything on the allowlist). `sys` is deliberately
**not** on the allowlist — it's powerful enough (`sys.modules`
manipulation, `sys._getframe`) to defeat the restriction on its own.

**A real, verified bypass found during final review, not just a
theoretical concern: several stdlib modules leak `os`/`sys` as a direct
attribute of themselves.** `random`, `collections`, `enum`, `dataclasses`,
`typing`, and `uuid` were all originally on the allowlist and all do
`import os`/`import sys` in their own top-level code — since a module's
*own* code always runs with the real, unrestricted builtins (this is
standard CPython behavior: only the *calling* frame's builtins are
restricted, not an imported module's), the resulting `os`/`sys` module
object ends up reachable as a plain attribute — `uuid.os`,
`random._os`, `collections._sys`, `enum.sys`, `dataclasses.sys`,
`typing.sys` — with **zero further `import` statements**, completely
bypassing the `__import__` gate via ordinary attribute access. Verified
directly: `import uuid; uuid.os.system(...)` executes arbitrary shell
commands. This is a different, much more easily discoverable class of
gap than the already-accepted `__subclasses__()`-style introspection
escape — it doesn't require any special knowledge of Python internals,
just reading one attribute. Fixed by removing all six modules from the
allowlist — the 11 remaining (`json`, `csv`, `math`, `re`, `time`,
`datetime`, `itertools`, `functools`, `string`, `copy`, `textwrap`) were
checked the same way (`dir(module)` against `os`/`sys`/`subprocess`/
`socket`/`ctypes`) and are clean at that shallow, one-hop depth.

**A second round of adversarial review found this doesn't fully close
the gap: `json.codecs.sys` and `re.enum.sys` both reach the real `sys`
module two attribute-hops deep, with zero import statements** —
`import json; json.codecs.sys.modules['os'].system(...)` also runs
arbitrary shell commands, verified directly. This is not a hypothetical:
it is a second, confirmed instance of the exact same bug class, one hop
deeper than the first round caught.

**Ruling — stop removing modules one leak at a time; close the whole
class instead.** Two rounds of "remove the leaky module, re-audit the
rest" already found new leaks each time, with no reason to expect a
third round (checking every remaining module three hops deep) would
converge rather than find yet another instance — almost every
non-trivial stdlib module imports `sys` *somewhere* in its own
dependency tree, and Python has no module-attribute privacy to stop a
determined search from finding it. Removing `json` specifically was
also unattractive on its own merits: it's load-bearing, shipped
functionality (the real, checked-in
`tools/example_plugins/csv_importer.py` importer uses `import csv,
json` through this exact restricted engine).

**The actual fix: any module handed back by the restricted `__import__`
is wrapped in a thin proxy (`make_module_proxy`,
`modules/script_python/restricted_builtins.cpp`) whose `__getattr__`
denies any attribute that itself resolves to another module object.**
This closes the *entire class* of bug at any depth in one change,
rather than patching individual discovered paths: `json.codecs`,
`re.enum`, and any other submodule-shaped attribute are refused at the
first hop, so `json.codecs.sys` (or any n-hop variant not yet
specifically found) can never be reached — the dangerous name is never
even one attribute away from being blocked. Legitimate, non-module
attributes (`json.dumps`, `re.compile`, ...) pass through the proxy
unchanged. Verified: both previously-confirmed exploit paths
(`json.codecs`, `re.enum`) now raise `AttributeError` at the first hop,
while `json.dumps({'a': 1})` and `re.compile('a+')` still work normally.

This does **not** close the separately-documented `__subclasses__()`-style
introspection escape (a different mechanism entirely — reaching a
dangerous class via the live object graph, not via a module's own
attributes) — that remains the accepted, out-of-scope limitation this
slice opened with; real isolation for a genuinely adversarial plugin
still needs the unscheduled "dynamic sandboxed execution" (OS-level
isolation) layer named in `docs/design/marketplace.md`. But the specific,
much more easily discoverable module-attribute-graph class this section
describes — no special Python-internals knowledge required, just reading
one or two attributes — is now closed, not merely narrowed.

**A fourth round of review found the proxy's own first implementation
leaked the real module through its own storage.** The wrapped module was
originally stored as a plain instance attribute (`self._mod`, via
`object.__setattr__`) — but `__getattr__` is only a *fallback* for
attributes absent from the instance, so `proxy._mod` resolved through
ordinary attribute lookup and handed back the real, completely unwrapped
module, undoing every check above in one guessable step (`json._mod`
required no cleverness, just reading the implementation or guessing a
common internal-storage name). Fixed by never storing the module as an
instance attribute at all: a `weakref.WeakKeyDictionary` keyed by the
proxy object itself (auto-cleared when the proxy is garbage collected)
holds the mapping instead, so every attribute name — including `_mod` —
now genuinely falls through to `__getattr__` uniformly, and
`object.__getattribute__(proxy, '_mod')` (the lower-level bypass a
plugin could try next) fails the same way, since there is truly nothing
there to find.

**A fifth round found the `WeakKeyDictionary` fix moved the leak rather
than closing it.** `_module_store` lived as a Python-level global inside
the same `py::exec`'d namespace as the proxy class, referenced from
inside `__getattr__`'s closure. Review found this is still reachable —
with no blocked builtin needed at all — via
`type(proxy).__getattr__.__globals__['_module_store']`:
`__class__`/`type()`, `__getattr__` (a plain class attribute, found via
normal MRO lookup, not "missing" — so the proxy's own override never
even intercepts the read), and `__globals__` (unrestricted on any plain
Python function object) are all ordinary attribute reads that resolve
before any instance-level override runs, and `type` was never on the
builtins blocklist to begin with. Worse than the fourth round's bug:
iterating `_module_store.items()` recovers every module wrapped by any
proxy created anywhere in the interpreter, not just the one instance
being probed. The root cause generalizes past this specific attempt: any
Python function's `__globals__` is unrestricted reflection, independent
of what storage scheme it closes over — the third attempt needed to stop
using a Python-level function as the thing holding the real module at
all.

**Fixed by moving the module reference into actual C++ state.**
`ModuleProxy` (`modules/script_python/restricted_builtins.cpp`) is a
plain C++ class holding the wrapped `py::object` as a private member,
registered as a Python type via `py::class_<ModuleProxy>` on a private,
never-`sys.modules`-registered internal module (pybind11 needs some
module scope to attach a class definition to; nothing about this module
is exposed to plugin code). `__getattr__` is bound as a C++ method
(`&ModuleProxy::getattr`) rather than a Python function, so it has no
`__globals__` at all (that attribute exists only on CPython Python
-function objects), and the instance carries no `__dict__` (pybind11
objects have none unless `py::dynamic_attr()` is requested, which this
type does not) — closing both the instance-attribute path (round 4) and
the closure/`__globals__` path (round 5) at the language level, not by
patching the specific probe found. What remains reachable is only the
already-disclosed, out-of-scope, language-fundamental
`__subclasses__()`-style object-graph escape, unrelated to this proxy's
implementation. Verified via a new assertion in the existing regression
test that `type(json).__getattr__.__globals__` itself raises
(`AttributeError`: the attribute is simply absent), not just that its
contents are empty.

**A sixth round found a third leak, one layer removed from the first
two: a container attribute can hand back real, unwrapped submodules as
its own contents without the container ITSELF being a module.**
`getattr()`'s `isinstance<py::module_>` check only rejected the
attribute value when the value itself was a module — but `__dict__`
returns the wrapped module's actual namespace dict, which is not a
module, so it passed straight through; that dict's values include every
submodule the module imported internally, so `json.__dict__['decoder']`
reached the real `json.decoder` module (chaining onward to `re`, `sys`,
etc.) via plain dict subscripting, no attribute access or reflection
required at all — the exact same bug class the proxy exists to close,
reached one different way. `__loader__`/`__spec__` are a different kind
of escape: real importlib objects whose own methods (`exec_module`) can
execute arbitrary code, not a module leak but no less dangerous. An
existing regression test's own assertion (`json.__dict__` has size 0)
was itself wrong — reproduced live, it returned the real dict with 26
entries, not an empty one; the assertion was checking a property that
never actually held. Fixed by denying `__dict__`, `__loader__`,
`__spec__`, and `__builtins__` by name, checked before the
`isinstance<>` check ever runs — none of the four are attributes a
sandboxed plugin has any legitimate reason to touch. The incorrect
assertion was removed and replaced with a dedicated test confirming all
four are blocked and legitimate use (`json.dumps(...)`) is unaffected.

**A seventh round found a fourth leak, different in kind from the first
three: `getattr()` only ever guarded attribute access performed on the
proxy itself — never the values it legitimately returns.** `json.dumps`
is a real, unwrapped CPython function object (not a module, so every
prior check let it straight through), and every plain Python function
carries `__globals__` — the real module's actual namespace dict,
including its genuine, unrestricted `__builtins__` entry:
`json.dumps.__globals__['__builtins__']['eval'](...)` reaches the real
`eval` with zero further imports and none of the four
previously-blocked names involved at all, a full sandbox escape,
verified live against the actual built binary. This generalizes the
same lesson the `WeakKeyDictionary` failure taught two rounds earlier:
naming one attribute at a time never converges, because *any* function
pulled from an allowed module carries the same unrestricted
`__globals__`/`__code__`. Fixed structurally: `CallableProxy` wraps any
value `ModuleProxy::getattr` resolves that turns out to be callable
(detected via `hasattr(value, "__globals__")` or `hasattr(value,
"__code__")`), forwarding `__call__` to the real function unchanged
while denying the same reflection-attribute set on the function object
itself. A shared `guard_returned_value()` helper applies this policy
recursively in both proxies, so an attribute of a wrapped callable that
itself resolves to a module or another callable is guarded the same
way at any depth — matching the "close the whole class" approach
already used for the module-attribute-graph bug, rather than the
"block a fifth name" approach that had already failed to converge
three times in a row.

**`"builtins"` is special-cased, found the hard way during implementation:**
CPython's own internals call `__import__('builtins', ...)` during some
stdlib operations — observed via `json.dumps()`, which triggers it even
though the plugin code never writes `import builtins` itself. Blocking it
outright breaks otherwise-safe allowlisted code. But the naive fix (let
`"builtins"` through to the real `__import__`) reopens a trivial full
bypass: `import builtins; builtins.eval(...)`. The actual fix: `"builtins"`
resolves to the restricted dict itself (not the real module) — this
satisfies whatever internal need triggers the call, while both attribute
access (`builtins.eval`) and subscript access (`builtins['eval']`) on the
result fail cleanly, since `eval` was already removed from the restricted
copy. Verified with a permanent regression test asserting both bypass
shapes fail.

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
