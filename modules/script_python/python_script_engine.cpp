#include "modules/script_python/python_script_engine.hpp"

#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <string>

#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include "engine/commands/set_component_command.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_debugger.hpp"
#include "engine/script/script_event.hpp"
#include "engine/script/script_host.hpp"
#include "engine/script/script_context.hpp"
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
    std::call_once(flag, [] {
#ifdef MOSAIC_PYTHON_HOME
        // Point CPython at its standard library: PyConfig.home defaults to the linked
        // Python's install prefix (baked in by CMake), overridable at runtime via the
        // PYTHONHOME env var. Without this, embedded CPython can't locate its stdlib
        // relative to the host binary — it warns and pure-Python imports fail.
        PyConfig config;
        PyConfig_InitPythonConfig(&config);
        const char* const env_home = std::getenv("PYTHONHOME");
        const std::string home =
            (env_home != nullptr && env_home[0] != '\0') ? env_home : MOSAIC_PYTHON_HOME;
        const PyStatus status = PyConfig_SetBytesString(&config, &config.home, home.c_str());
        if (PyStatus_Exception(status) == 0) { py::initialize_interpreter(&config); }
        else { py::initialize_interpreter(); } // bad home -> fall back to default init
        PyConfig_Clear(&config);
#else
        py::initialize_interpreter();
#endif
    });
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

            // Install the non-blocking debugger trace hook while a debugger is attached.
            const bool traced = install_trace(sys);

            // RAII guard: restore sys.stdout/stderr (and uninstall the trace hook) on every exit
            // path, including any unexpected C++ exception that bypasses the inner try/catch.
            struct StreamGuard
            {
                py::module_& sys_;
                py::object& old_out_;
                py::object& old_err_;
                bool        traced_;
                ~StreamGuard() noexcept
                {
                    try
                    {
                        sys_.attr("stdout") = old_out_;
                        sys_.attr("stderr") = old_err_;
                        if (traced_) { sys_.attr("settrace")(py::none()); }
                    }
                    catch (...)
                    {
                    }
                }
            } guard{sys, old_out, old_err, traced};

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
                    const mosaic::engine::reflection::ComponentReflection& types) override
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
        // Component authoring + queries (reflection-driven; inline mode only). These let a
        // script furnish a freshly created entity and inspect/query composition by name.
        globals_["add_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> bool
            { return types_->add_default(*world_, mosaic::foundation::EntityID(eid), name); });
        globals_["remove_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> bool
            { return types_->remove_value(*world_, mosaic::foundation::EntityID(eid), name); });
        globals_["has_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> bool
            {
                const auto e = mosaic::foundation::EntityID(eid);
                for (const std::string& n : types_->components_on(*world_, e))
                {
                    if (n == name) { return true; }
                }
                return false;
            });
        globals_["component_names"] = py::cpp_function(
            [this](std::uint64_t eid) -> py::list
            {
                py::list out;
                for (const std::string& n :
                     types_->components_on(*world_, mosaic::foundation::EntityID(eid)))
                {
                    out.append(py::str(n));
                }
                return out;
            });
        globals_["entities_with"] = py::cpp_function(
            [this](const std::string& name) -> py::list
            {
                py::list out;
                world_->for_each_entity(
                    [&](mosaic::foundation::EntityID e)
                    {
                        for (const std::string& n : types_->components_on(*world_, e))
                        {
                            if (n == name) { out.append(py::int_(e.value())); break; }
                        }
                    });
                return out;
            });
        // emit(name, payload=None): publish a dynamic ScriptEvent on the world bus. Deferred
        // subscribers (ScriptSystem) deliver it to on_event(name, payload) one tick later.
        globals_["emit"] = py::cpp_function(
            [this](const std::string& name, py::object payload)
            {
                world_->events().emit(
                    mosaic::engine::script::ScriptEvent{name, py_to_value(payload)});
            },
            py::arg("name"), py::arg("payload") = py::none());
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

    void bind_host(const mosaic::engine::script::ScriptHost& host) override
    {
        host_ = host;
        globals_["play"] = py::cpp_function(
            [this] { if (host_.set_playing) { host_.set_playing(true); } });
        globals_["pause"] = py::cpp_function(
            [this] { if (host_.set_playing) { host_.set_playing(false); } });
        globals_["stop"] = py::cpp_function(
            [this] { if (host_.request_stop) { host_.request_stop(); } });
        // raycast(origin, direction, max_distance) -> dict. origin/direction are 3-element
        // sequences; result is {hit, entity, distance, point:{x,y,z}, normal:{x,y,z}}.
        globals_["raycast"] = py::cpp_function(
            [this](py::sequence origin, py::sequence direction, float max_distance) -> py::dict
            {
                using mosaic::foundation::math::Vec3;
                mosaic::engine::script::ScriptRayHit h;
                if (host_.raycast)
                {
                    const Vec3 o{origin[0].cast<float>(), origin[1].cast<float>(),
                                 origin[2].cast<float>()};
                    const Vec3 d{direction[0].cast<float>(), direction[1].cast<float>(),
                                 direction[2].cast<float>()};
                    h = host_.raycast(o, d, max_distance);
                }
                py::dict point;
                point["x"] = h.point.x; point["y"] = h.point.y; point["z"] = h.point.z;
                py::dict normal;
                normal["x"] = h.normal.x; normal["y"] = h.normal.y; normal["z"] = h.normal.z;
                py::dict out;
                out["hit"] = h.hit;
                out["entity"] = h.entity;
                out["distance"] = h.distance;
                out["point"] = point;
                out["normal"] = normal;
                return out;
            });
    }

    void bind_context(mosaic::engine::script::ScriptContext& context) override
    {
        context_ = &context;
        globals_["get_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> py::object
            {
                auto v = context_->get(mosaic::foundation::EntityID(eid), name);
                return v ? value_to_py(*v) : py::none();
            });
        globals_["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                return context_->set(mosaic::foundation::EntityID(eid), name,
                                     py_to_value(value));
            });
    }

    void bind_debugger(mosaic::engine::script::ScriptDebugger* debugger) override
    {
        debugger_ = debugger;
    }

private:
    // Install a non-blocking sys.settrace hook that records breakpoint hits into debugger_.
    // Returns true if a hook was installed (i.e. a debugger is attached); the caller uninstalls
    // it (settrace(None)) on exit. The trace fn returns itself so it traces every frame's lines.
    bool install_trace(py::module_& sys)
    {
        if (debugger_ == nullptr) { return false; }
        if (!trace_fn_)
        {
            trace_fn_ = py::cpp_function(
                [this](py::object frame, py::object event, py::object /*arg*/) -> py::object
                {
                    if (debugger_ != nullptr && event.cast<std::string>() == "line")
                    {
                        const int line = frame.attr("f_lineno").cast<int>();
                        if (debugger_->has_breakpoint(line))
                        {
                            mosaic::engine::script::BreakpointHit hit;
                            hit.line = line;
                            py::dict locals = frame.attr("f_locals").cast<py::dict>();
                            for (const auto& item : locals)
                            {
                                std::string key = py::str(item.first).cast<std::string>();
                                try
                                {
                                    hit.locals.emplace_back(key, py_to_value(item.second));
                                }
                                catch (...)
                                {
                                    hit.locals.emplace_back(
                                        key, mosaic::foundation::serialization::Value{});
                                }
                            }
                            debugger_->record_hit(std::move(hit));
                        }
                    }
                    return trace_fn_;
                });
        }
        sys.attr("settrace")(trace_fn_);
        return true;
    }

    py::dict globals_; // this engine's isolated namespace (interpreter outlives it)
    mosaic::engine::World* world_ = nullptr;
    const mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ = nullptr;
    mosaic::engine::script::ScriptHost host_;
    mosaic::engine::script::ScriptContext* context_ = nullptr;
    mosaic::engine::script::ScriptDebugger* debugger_ = nullptr;
    py::object trace_fn_;
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>();
}
} // namespace mosaic::script::python
