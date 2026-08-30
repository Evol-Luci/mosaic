#include "modules/script_python/python_script_engine.hpp"

#include <exception>
#include <mutex>
#include <string>

#include <pybind11/embed.h>

namespace py = pybind11;

namespace mosaic::script::python
{
namespace
{
// CPython allows a single interpreter and is fragile across finalize/re-init, so initialize once
// per process and never finalize (the process reclaims it at exit). After init, the calling
// (main) thread holds the GIL; v0.1 scripting is single-threaded, so no explicit GIL handoff.
void ensure_interpreter()
{
    static std::once_flag flag;
    std::call_once(flag, [] { py::initialize_interpreter(); });
}

// Helper base class: initializes CPython before any pybind11 member objects are
// constructed. py::dict's default constructor calls PyDict_New() immediately, so
// the interpreter must be running before the member initializers run.
// C++ initializes base classes (in declaration order) before member variables,
// so placing InterpreterInit first guarantees the interpreter is up in time.
struct InterpreterInit
{
    InterpreterInit() { ensure_interpreter(); }
};

class PythonScriptEngine final
    : private InterpreterInit // must be first — initializes CPython before py::dict globals_
    , public mosaic::engine::script::ScriptEngine
{
public:
    PythonScriptEngine()
    {
        // Interpreter is already running (InterpreterInit ran first).
        // Give the namespace access to builtins (print, etc.).
        globals_["__builtins__"] = py::module_::import("builtins");
    }

    mosaic::engine::script::ScriptResult run_string(const std::string& code) override
    {
        mosaic::engine::script::ScriptResult result;
        try
        {
            py::module_ sys = py::module_::import("sys");
            py::module_ io = py::module_::import("io");
            py::object old_out = sys.attr("stdout");
            py::object old_err = sys.attr("stderr");
            py::object buffer = io.attr("StringIO")();
            sys.attr("stdout") = buffer;
            sys.attr("stderr") = buffer;

            try
            {
                py::exec(code, globals_);
                result.ok = true;
            }
            catch (py::error_already_set& e)
            {
                result.ok = false;
                result.error = e.what();
            }

            sys.attr("stdout") = old_out; // restore before reading; buffer still alive
            sys.attr("stderr") = old_err;
            result.output = buffer.attr("getvalue")().cast<std::string>();
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            if (result.error.empty())
            {
                result.error = e.what();
            }
        }
        return result;
    }

private:
    py::dict globals_; // this engine's isolated namespace (interpreter outlives it)
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
