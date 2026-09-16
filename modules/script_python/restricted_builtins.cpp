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
// own methods aren't modules either -- is its own escape.
//
// A FOURTH leak was then found, in the same review pass as the third but a
// different vector: getattr() only guards attribute access performed
// directly ON THE PROXY -- it never touches the VALUES it returns for
// ordinary, legitimate attributes. `json.dumps` is a real, unwrapped
// CPython function object (not a module, not blocked, passes straight
// through), and every plain Python function carries `__globals__` -- the
// REAL module's actual namespace dict, not filtered by anything above:
//     json.dumps.__globals__['__builtins__']['eval'](...)
// reaches the genuine, unrestricted eval with zero further imports and
// none of the four names blocked above. This generalizes the same lesson
// as the WeakKeyDictionary failure earlier in this history: patching one
// named attribute at a time (__dict__, __loader__, __spec__, and now
// __globals__) never converges, because ANY function object pulled from an
// allowed module carries the same unrestricted __globals__. The structural
// fix: wrap every returned value that could itself carry a reference back
// to unrestricted state, not just modules -- CallableProxy below does for
// functions/methods what ModuleProxy does for modules. shared_attr_guard()
// centralizes the "deny these names outright, then deny anything that
// resolves to a module, then wrap anything callable" policy so both
// proxies enforce it identically and a future addition only needs to
// change one place.
constexpr const char* kBlockedReflectionAttrs[] = {
    "__dict__", "__loader__", "__spec__", "__builtins__",
    "__globals__", "__code__", "__closure__", "__self__", "__func__",
};

// AN HONEST, DELIBERATE STOPPING POINT, reached after this exact mechanism
// (wrap the returned value, deny its reflective attributes) converged for
// modules (leak #1-3 above) but did NOT converge for callables: leak #4 was
// closed by CallableProxy, leak #5 (this function's return-value guard,
// see CallableProxy::call) closes the "a callable returns another
// callable" shape -- but a CLASS returned from an allowed module (e.g.
// collections.namedtuple(...), which returns a fresh class) has neither
// __globals__ nor __code__, so it is NOT wrapped, and its methods are real
// bound methods carrying the same unrestricted __globals__ one hop
// further out. Proxying classes too was investigated and rejected, not
// for lack of effort but because CPython makes it impossible to do
// correctly: `except SomeAllowedException:` uses a hard PyType_Check that
// does not honor __instancecheck__ (a non-type proxy can never be caught
// by an except clause -- verified), and making the proxy a literal
// subclass of the real exception type does not catch instances of the
// REAL type either (a module that raises the real, un-proxied exception
// internally would then silently stop being catchable by plugin code --
// a correctness regression masquerading as a security fix). Proxying the
// INSTANCES a class constructor returns is separately impossible: Python's
// special-method dispatch (operators, str(), isinstance(), hashing, ...)
// bypasses __getattr__ entirely and looks up dunder methods on the type,
// not the instance, so a forwarding instance proxy silently loses
// arithmetic, string conversion, and type identity for exactly the kind of
// object (a real stdlib value) a plugin most needs to use normally. This
// is the same category of language-fundamental limitation already
// disclosed for the `__subclasses__()` object-graph escape -- see the
// design doc's "honest limitation" section for the full disclosure and
// the audit-hook-based mechanism named as the actual future lever for
// closing this, which belongs to a larger, distinct, not-yet-scheduled
// sandboxing slice (OS-level isolation / PEP 578 audit hooks), not this
// one.

py::object make_callable_proxy(py::object callable_obj);
py::object make_module_proxy(py::object module_obj);

// Applies the shared policy to one resolved attribute value: deny outright
// if `name` is on the reflection blocklist (checked by the caller before
// even fetching the value; kept here too as a second layer in case a
// caller is added later that skips the by-name check), deny if the value
// is itself a module, or wrap it if it is callable (and therefore may
// carry its own `__globals__`/`__code__`/etc. straight back to unrestricted
// state). Anything else -- data (str, int, dict of plain values the
// plugin's own code produced, a compiled regex object, ...) -- passes
// through unchanged.
//
// The callable check is PyCallable_Check(), not hasattr(__globals__) or
// hasattr(__code__) -- a later leak (round 10 of review): those two
// attributes only exist on ordinary Python-level functions. A C-implemented
// builtin like
// math.sqrt has neither, so it sailed through this guard completely
// unwrapped, and math.sqrt.__self__ hands back the raw, unproxied math
// module -- from there, .__loader__.load_module('sys').modules['os'].system
// is a clean escape. Checking callability directly (excluding types, which
// must stay unwrapped per the class discussion above) closes this for any
// callable, builtin or not, regardless of which reflection attributes it
// happens to expose.
py::object guard_returned_value(const std::string& name, py::object value)
{
    if (py::isinstance<py::module_>(value))
    {
        throw py::attribute_error(
            "access to '" + name + "' is not permitted here");
    }
    if (PyCallable_Check(value.ptr()) != 0 && PyType_Check(value.ptr()) == 0)
    {
        return make_callable_proxy(std::move(value));
    }
    return value;
}

class ModuleProxy
{
public:
    explicit ModuleProxy(py::object module_obj) : module_(std::move(module_obj)) {}

    py::object getattr(const std::string& name) const
    {
        for (const char* blocked : kBlockedReflectionAttrs)
        {
            if (name == blocked)
            {
                throw py::attribute_error(
                    "access to '" + name + "' is not permitted here");
            }
        }
        return guard_returned_value(name, module_.attr(py::str(name)));
    }

private:
    py::object module_;
};

// Wraps a callable (a plain function, bound method, or similar) pulled from
// an allowed module so its own reflective attributes -- __globals__ above
// all, the fourth leak found -- are denied the same way ModuleProxy denies
// them on modules. __call__ forwards to the real callable unchanged, since
// invoking json.dumps(...) must still work exactly as before; only
// attribute access on the returned function object itself is guarded.
//
// call()'s RETURN VALUE is guarded too (a fifth leak, found in the same
// review pass as the fourth): a factory function like
// functools.lru_cache(maxsize=1) returns a fresh, unwrapped closure whose
// __globals__ reaches the real module namespace exactly like json.dumps
// did before this class existed -- guard_returned_value() catches this
// the same way it catches an ordinary attribute access, since call() sites
// and getattr() sites both ultimately hand a plugin a value that needs the
// same scrutiny.
class CallableProxy
{
public:
    explicit CallableProxy(py::object callable_obj) : callable_(std::move(callable_obj)) {}

    py::object call(py::args args, py::kwargs kwargs) const
    {
        return guard_returned_value("<return value>", callable_(*args, **kwargs));
    }

    py::object getattr(const std::string& name) const
    {
        for (const char* blocked : kBlockedReflectionAttrs)
        {
            if (name == blocked)
            {
                throw py::attribute_error(
                    "access to '" + name + "' is not permitted here");
            }
        }
        return guard_returned_value(name, callable_.attr(py::str(name)));
    }

private:
    py::object callable_;
};

// Both proxy classes are registered once per process into a private module
// that is never inserted into sys.modules and never exposed to plugin
// globals -- pybind11 needs SOME module scope to attach class definitions
// to, but nothing about this module is reachable from restricted code.
struct ProxyClasses
{
    py::object module_proxy_class;
    py::object callable_proxy_class;
};

const ProxyClasses& proxy_classes()
{
    static const ProxyClasses classes = []
    {
        PyObject* raw_module = PyModule_New("_mosaic_sandbox_internal");
        if (raw_module == nullptr) { throw py::error_already_set(); }
        py::module_ internal_module = py::reinterpret_steal<py::module_>(raw_module);

        py::class_<ModuleProxy>(internal_module, "_RestrictedModuleProxy")
            .def(py::init<py::object>())
            .def("__getattr__", &ModuleProxy::getattr);
        py::class_<CallableProxy>(internal_module, "_RestrictedCallableProxy")
            .def(py::init<py::object>())
            .def("__call__", &CallableProxy::call)
            .def("__getattr__", &CallableProxy::getattr);

        return ProxyClasses{
            internal_module.attr("_RestrictedModuleProxy"),
            internal_module.attr("_RestrictedCallableProxy"),
        };
    }();
    return classes;
}

py::object make_module_proxy(py::object module_obj)
{
    return proxy_classes().module_proxy_class(std::move(module_obj));
}

py::object make_callable_proxy(py::object callable_obj)
{
    return proxy_classes().callable_proxy_class(std::move(callable_obj));
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
