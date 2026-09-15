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

// A small, conservative set of stdlib modules with no meaningful attack surface
// on their own. Extend this list (see docs/design/plugin-sandboxing.md) if a real
// plugin needs something more -- don't work around it by widening the blocklist
// approach above instead.
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
            return real_import(*args, **kwargs);
        });

    globals["__builtins__"] = restricted;
}
} // namespace mosaic::script::python
