#include "modules/script_python/python_script_engine.hpp"

#include <exception>
#include <mutex>
#include <string>

#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "modules/script_python/value_marshal.hpp"

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

            // RAII guard: restore sys.stdout/stderr on every exit path, including
            // any unexpected C++ exception that bypasses the inner try/catch.
            struct StreamGuard
            {
                py::module_& sys_;
                py::object& old_out_;
                py::object& old_err_;
                ~StreamGuard() noexcept
                {
                    try
                    {
                        sys_.attr("stdout") = old_out_;
                        sys_.attr("stderr") = old_err_;
                    }
                    catch (...)
                    {
                    }
                }
            } guard{sys, old_out, old_err};

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

            // Read captured output while buffer is still alive; guard restores streams on exit.
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

    void bind_world(mosaic::engine::World& world,
                    mosaic::engine::reflection::ComponentReflection& types) override
    {
        world_ = &world;
        types_ = &types;
        globals_["get_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> py::object
            {
                auto v = types_->component_value(*world_, mosaic::foundation::EntityID(eid), name);
                return v ? value_to_py(*v) : py::none();
            });
        globals_["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                return types_->set_value(*world_, mosaic::foundation::EntityID(eid), name,
                                         py_to_value(value));
            });
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        globals_[py::str(name)] = value_to_py(value);
    }

private:
    py::dict globals_; // this engine's isolated namespace (interpreter outlives it)
    mosaic::engine::World* world_ = nullptr;
    mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
