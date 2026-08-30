#include "modules/script_python/python_script_engine.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <string>

#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include "engine/commands/set_component_command.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
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
                const auto e = mosaic::foundation::EntityID(eid);
                const mosaic::foundation::serialization::Value v = py_to_value(value);
                if (stack_ != nullptr)
                {
                    auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
                        *world_, *types_, e, name, v);
                    cmd->execute();
                    if (!cmd->valid()) { return false; }   // failed set: not recorded
                    stack_->push_executed(std::move(cmd));
                    return true;
                }
                return types_->set_value(*world_, e, name, v);
            });
        globals_["create_entity"] = py::cpp_function(
            [this]() -> std::uint64_t { return world_->create_entity().value(); });
        globals_["destroy_entity"] = py::cpp_function(
            [this](std::uint64_t eid) -> bool
            {
                const auto e = mosaic::foundation::EntityID(eid);
                if (!world_->is_valid(e)) { return false; }
                world_->destroy_entity(e);
                return true;
            });
        globals_["entity_count"] = py::cpp_function(
            [this]() -> std::uint64_t { return static_cast<std::uint64_t>(world_->entity_count()); });
        globals_["entities"] = py::cpp_function(
            [this]() -> py::list
            {
                py::list out;
                world_->for_each_entity([&](mosaic::foundation::EntityID e)
                                        { out.append(py::int_(e.value())); });
                return out;
            });
        globals_["is_valid"] = py::cpp_function(
            [this](std::uint64_t eid) -> bool
            { return world_->is_valid(mosaic::foundation::EntityID(eid)); });
        // Guarded undo/redo/undo_depth: functional even before bind_commands — they return
        // false/0 while stack_ is null, and route to the stack once bind_commands sets it.
        globals_["undo"] =
            py::cpp_function([this]() -> bool { return stack_ != nullptr && stack_->undo(); });
        globals_["redo"] =
            py::cpp_function([this]() -> bool { return stack_ != nullptr && stack_->redo(); });
        globals_["undo_depth"] = py::cpp_function(
            [this]() -> std::uint64_t
            { return stack_ != nullptr ? static_cast<std::uint64_t>(stack_->undo_depth()) : 0; });
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        globals_[py::str(name)] = value_to_py(value);
    }

    void bind_commands(mosaic::foundation::commands::CommandStack& stack) override
    {
        // undo/redo/undo_depth are installed in bind_world and read stack_ at call time, so
        // binding the stack here activates them for all subsequent script calls.
        stack_ = &stack;
    }

private:
    py::dict globals_; // this engine's isolated namespace (interpreter outlives it)
    mosaic::engine::World* world_ = nullptr;
    mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ = nullptr;
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
