#include "modules/script_python/restricted_builtins.hpp"

#include <string>
#include <unordered_set>

namespace py = pybind11;

namespace mosaic::script::python
{
namespace
{
// Genuinely dangerous builtins: dynamic code execution, interactive/debugger
// hooks, and namespace introspection that could be chained toward an escape.
// "open" is deliberately NOT in this fixed list -- whether it's blocked depends
// on install_restricted_builtins's allow_open parameter (see its doc comment).
// A blocklist (not a hand-curated allowlist of the ~150 safe names) so a
// legitimate plugin never silently loses something it needs.
constexpr const char* kBlockedBuiltins[] = {
    "eval", "exec", "compile", "__import__", "input",
    "breakpoint", "exit", "quit", "help", "vars", "globals", "locals",
};

// A small, conservative set of stdlib modules. A module's *own* top-level
// code always runs with the real, unrestricted builtins, so a module that
// itself does `import os`/`import sys` leaks the real module as a plain,
// reachable attribute (e.g. `uuid.os`) -- six originally-allowlisted modules
// (random, collections, enum, dataclasses, typing, uuid) were removed after
// being caught this way. A second, deeper instance of the SAME bug class
// (json.codecs.sys, re.enum.sys, two hops instead of one) was found next --
// rather than keep narrowing this list one discovered path at a time (which
// two rounds already showed doesn't converge), the actual fix is
// make_module_proxy() below: it closes the whole class of "reach a
// dangerous module via an allowed module's attribute graph" bug at any
// depth, so this list only needs to name modules worth exposing at all, not
// modules individually proven leak-free.
const std::unordered_set<std::string>& allowed_modules()
{
    static const std::unordered_set<std::string> kAllowed = {
        "json",       "csv",        "math",     "re",     "random",
        "time",       "datetime",   "collections", "itertools", "functools",
        "string",     "copy",       "enum",     "dataclasses", "typing",
        "textwrap",   "uuid",
    };
    return kAllowed;
}
// Wraps `module` so attribute access that would resolve to ANOTHER module
// object is denied, closing the whole class of bug found via uuid.os,
// json.codecs.sys, and re.enum.sys: rather than chase individual dangerous
// paths (which two rounds of allowlist-narrowing already showed doesn't
// converge), refuse the FIRST hop into any submodule at all, at any depth --
// a legitimate attribute (a function, class, or constant) still passes
// through unchanged.
//
// TWO Python-level storage schemes were tried and broken before this one:
//
// 1. `self._mod` via object.__setattr__ -- __getattr__ is only a FALLBACK
//    for attributes absent from the instance, so `proxy._mod` resolved
//    through ordinary attribute lookup and returned the real, unwrapped
//    module directly.
// 2. A module-level `weakref.WeakKeyDictionary` (`_module_store`) keyed by
//    the proxy, referenced from inside the __getattr__ *closure* -- fixed
//    (1), but review found `type(proxy).__getattr__.__globals__` reaches
//    the SAME dict via ordinary, always-available reflection
//    (`__class__`/`__getattr__`/`__globals__` are all plain attribute
//    reads that resolve via `object`'s own slots before any override ever
//    runs, and none of `type`, `__class__`, `__globals__` are on the
//    builtins blocklist) -- worse than (1), since it recovers every
//    wrapped module ever created, not just the one instance being probed.
//    Root cause: ANY Python function's `__globals__` is unrestricted
//    reflection, independent of what storage scheme it closes over --
//    moving the reference from an instance attribute to a shared dict
//    changed *where* it lived, not *whether* a Python-level function could
//    still be walked back to it.
//
// The actual fix: the module reference lives in real C++ state (ModuleProxy
// below, a pybind11-registered type), not in any Python-visible attribute,
// dict, or closure. A pybind11-generated method has no __globals__ (that's
// a CPython-function-object-only attribute), and a plain py::class_
// instance carries no per-instance __dict__ unless py::dynamic_attr() is
// requested (it is not, here) -- so there is no Python-reachable path from
// a ModuleProxy instance to the underlying module short of the already-
// disclosed, out-of-scope, language-fundamental __subclasses__()-style
// object-graph escape (see the design doc's "honest limitation" section).
//
// A THIRD leak was then found in this same fix, one layer removed from the
// first two: getattr()'s isinstance<py::module_> check only rejects the
// attribute VALUE when it is itself a module -- but a CONTAINER attribute
// can still hand back real, unwrapped submodules as its own contents
// without the container itself being a module. `__dict__` is exactly this:
// `module_.attr("__dict__")` returns the wrapped module's REAL namespace
// dict (not a module, so isinstance<> lets it straight through), and that
// dict's values include every submodule the module imported internally --
// `json.__dict__['decoder']` reaches the real `json.decoder` module (which
// itself reaches `re`, etc.) with plain dict subscripting, no attribute
// access and no further reflection needed. `__loader__`/`__spec__` are
// worse in a different way: they're real importlib objects (not dicts),
// but importlib loaders expose methods (e.g. exec_module) capable of
// executing arbitrary code, so handing one back -- even wrapped, since its
// own methods aren't modules either -- is its own escape. All three are
// denied outright, by name, before the isinstance<> check ever runs, since
// none of them are attributes a Studio plugin or asset importer has any
// legitimate reason to touch.
class ModuleProxy
{
public:
    explicit ModuleProxy(py::object module_obj) : module_(std::move(module_obj)) {}

    py::object getattr(const std::string& name) const
    {
        static const std::unordered_set<std::string> kBlockedModuleAttrs = {
            "__dict__", "__loader__", "__spec__", "__builtins__",
        };
        if (kBlockedModuleAttrs.count(name) != 0)
        {
            throw py::attribute_error(
                "access to '" + name + "' is not permitted here");
        }
        py::object value = module_.attr(py::str(name));
        if (py::isinstance<py::module_>(value))
        {
            throw py::attribute_error(
                "access to '" + name + "' is not permitted here");
        }
        return value;
    }

private:
    py::object module_;
};

py::object make_module_proxy(py::object module_obj)
{
    // A private class, registered once per process into a module that is
    // never inserted into sys.modules and never exposed to plugin globals
    // -- pybind11 needs SOME module scope to attach a class definition to,
    // but nothing about this module is reachable from restricted code.
    static py::object proxy_class = []
    {
        PyObject* raw_module = PyModule_New("_mosaic_sandbox_internal");
        if (raw_module == nullptr) { throw py::error_already_set(); }
        py::module_ internal_module = py::reinterpret_steal<py::module_>(raw_module);

        py::class_<ModuleProxy>(internal_module, "_RestrictedModuleProxy")
            .def(py::init<py::object>())
            .def("__getattr__", &ModuleProxy::getattr);
        return internal_module.attr("_RestrictedModuleProxy");
    }();
    return proxy_class(module_obj);
}
} // namespace

void install_restricted_builtins(py::dict& globals, bool allow_open)
{
    py::module_ builtins_mod = py::module_::import("builtins");
    // .copy() (Python's own dict method), never alias builtins.__dict__ directly --
    // popping names from an alias would mutate the real, process-wide builtins
    // module and break every other engine, including unrestricted gameplay scripts.
    py::dict restricted = builtins_mod.attr("__dict__").attr("copy")().cast<py::dict>();

    for (const char* name : kBlockedBuiltins)
    {
        restricted.attr("pop")(name, py::none());
    }
    if (!allow_open)
    {
        restricted.attr("pop")("open", py::none());
    }

    restricted["__import__"] = py::cpp_function(
        [restricted](py::args args, py::kwargs kwargs) -> py::object
        {
            const std::string name = args.size() > 0 ? args[0].cast<std::string>() : std::string();
            const std::string top_level = name.substr(0, name.find('.'));
            if (top_level == "builtins")
            {
                // CPython's own internals legitimately call __import__('builtins')
                // during some stdlib operations (observed via json.dumps()), not
                // just from a plugin's own explicit "import builtins" statement --
                // returning ImportError here breaks those internals. But the real,
                // unrestricted `builtins` module must never be handed back (that
                // would trivially defeat every restriction above via
                // `import builtins; builtins.eval(...)`): return THIS restricted
                // dict instead, satisfying whichever caller needs the name to
                // resolve without exposing anything beyond what's already allowed.
                return restricted;
            }
            if (allowed_modules().count(top_level) == 0)
            {
                throw py::import_error("import of '" + name + "' is not permitted here");
            }
            py::module_ builtins_mod = py::module_::import("builtins");
            py::object  real_import  = builtins_mod.attr("__import__");
            return make_module_proxy(real_import(*args, **kwargs));
        });

    globals["__builtins__"] = restricted;
}
} // namespace mosaic::script::python
