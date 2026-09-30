#include "modules/script_python/python_script_engine.hpp"

#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <pybind11/embed.h>
#include <pybind11/functional.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_debugger.hpp"
#include "engine/script/script_host.hpp"
#include "engine/script/script_context.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "modules/script_python/restricted_builtins.hpp"
#include "modules/script_python/value_marshal.hpp"
#include "modules/script_python/world_globals.hpp"
#include "modules/script_python/host_globals.hpp"

namespace py = pybind11;

namespace mosaic::script::python
{
// CPython allows a single interpreter and is fragile across finalize/re-init, so initialize once
// per process and never finalize (the process reclaims it at exit). After init, the calling
// (main) thread holds the GIL; v0.1 scripting is single-threaded, so no explicit GIL handoff.
// Exported (declared in python_script_engine.hpp) so modules/script_python_studio's
// StudioPluginScriptEngine can share the SAME process-wide std::call_once guard --
// calling py::initialize_interpreter() from two independent std::call_once flags would
// double-initialize CPython and assert.
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

namespace
{
// Helper base class: initializes CPython before any pybind11 member objects are
// constructed. py::dict's default constructor calls PyDict_New() immediately, so
// the interpreter must be running before the member initializers run.
// C++ initializes base classes (in declaration order) before member variables,
// so placing InterpreterInit first guarantees the interpreter is up in time.
struct InterpreterInit
{
    InterpreterInit() { ensure_interpreter(); }
};

// Reads a 3-element Python sequence into a Vec3. Returns false instead of throwing
// for a short sequence or a non-numeric element, so one malformed script argument
// fails that call rather than aborting the tick.
bool to_vec3(const py::sequence& s, mosaic::foundation::math::Vec3& out)
{
    if (py::len(s) < 3) { return false; }
    try
    {
        out.x = s[0].cast<float>();
        out.y = s[1].cast<float>();
        out.z = s[2].cast<float>();
    }
    catch (const py::cast_error&)
    {
        return false;
    }
    return true;
}

void install_host_globals(py::dict& globals, const mosaic::engine::script::ScriptHost& host)
{
    globals["play"] = py::cpp_function(
        [host] { if (host.set_playing) { host.set_playing(true); } },
        py::doc("Resume the simulation clock. Has no effect if already playing."));
    globals["pause"] = py::cpp_function(
        [host] { if (host.set_playing) { host.set_playing(false); } },
        py::doc("Pause the simulation clock. Has no effect if already paused."));
    globals["stop"] = py::cpp_function(
        [host] { if (host.request_stop) { host.request_stop(); } },
        py::doc("Stop the simulation and revert world state to the initial snapshot."));
    
    // raycast(origin, direction, max_distance) -> dict. origin/direction are 3-element
    // sequences; result is {hit, entity, distance, point:{x,y,z}, normal:{x,y,z}}.
    globals["raycast"] = py::cpp_function(
        [host](py::sequence origin, py::sequence direction, float max_distance) -> py::dict
        {
            using mosaic::foundation::math::Vec3;
            mosaic::engine::script::ScriptRayHit h;
            if (host.raycast)
            {
                const Vec3 o{origin[0].cast<float>(), origin[1].cast<float>(),
                             origin[2].cast<float>()};
                const Vec3 d{direction[0].cast<float>(), direction[1].cast<float>(),
                             direction[2].cast<float>()};
                h = host.raycast(o, d, max_distance);
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
        },
        py::doc("Cast a ray into the scene and return hit data as a dictionary."));

    // apply_impulse(entity, impulse, point) -> bool. impulse/point are 3-element
    // sequences (matching raycast). Malformed input returns False rather than
    // raising: a raising binding would abort the whole tick over one bad call.
    globals["apply_impulse"] = py::cpp_function(
        [host](std::uint64_t entity, py::sequence impulse, py::sequence point) -> bool
        {
            using mosaic::foundation::math::Vec3;
            if (!host.apply_impulse) { return false; }
            Vec3 i{}, p{};
            if (!to_vec3(impulse, i) || !to_vec3(point, p)) { return false; }
            return host.apply_impulse(entity, i, p);
        },
        py::doc("Apply an instantaneous impulse to a dynamic body at a world-space "
                "point; impulse and point are 3-element sequences. Applying it away "
                "from the body's centre induces spin, which is what makes a sphere "
                "roll rather than slide. Returns False if there is no physics "
                "system, the entity has no simulated body, or an argument is "
                "malformed."));
}

class PythonScriptEngine final
    : private InterpreterInit // must be first — initializes CPython before py::dict globals_
    , public mosaic::engine::script::ScriptEngine
{
public:
    explicit PythonScriptEngine(bool restricted = false)
    {
        // Interpreter is already running (InterpreterInit ran first).
        if (restricted)
        {
            // allow_open=true: this constructor path is only ever reached via
            // create_restricted_python_script_engine(), used exclusively for asset
            // importers, whose entire purpose is reading their own source file.
            install_restricted_builtins(globals_, /*allow_open=*/true);
        }
        else
        {
            // Give the namespace access to builtins (print, etc.).
            globals_["__builtins__"] = py::module_::import("builtins");
        }
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
        install_world_globals(globals_, &world_, &types_, &stack_);
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        globals_[py::str(name)] = value_to_py(value);
    }

    std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) override
    {
        if (!globals_.contains(name)) { return std::nullopt; }
        try
        {
            return py_to_value(globals_[py::str(name)]);
        }
        catch (const std::exception&)
        {
            return std::nullopt; // e.g. a function/class/other unmarshalable object
        }
    }

    mosaic::engine::script::ScriptResult call_function(const std::string& name) override
    {
        mosaic::engine::script::ScriptResult result;
        if (!globals_.contains(name) || !py::isinstance<py::function>(globals_[py::str(name)]))
        {
            result.error = "'" + name + "' is not a defined function";
            return result;
        }
        try
        {
            py::module_ sys = py::module_::import("sys");
            py::module_ io = py::module_::import("io");
            py::object old_out = sys.attr("stdout");
            py::object old_err = sys.attr("stderr");
            py::object buffer = io.attr("StringIO")();
            sys.attr("stdout") = buffer;
            sys.attr("stderr") = buffer;

            struct StreamGuard
            {
                py::module_& sys_;
                py::object& old_out_;
                py::object& old_err_;
                ~StreamGuard() noexcept
                {
                    try { sys_.attr("stdout") = old_out_; sys_.attr("stderr") = old_err_; }
                    catch (...) {}
                }
            } guard{sys, old_out, old_err};

            try
            {
                globals_[py::str(name)]();
                result.ok = true;
            }
            catch (py::error_already_set& e)
            {
                result.ok = false;
                result.error = e.what();
            }
            result.output = buffer.attr("getvalue")().cast<std::string>();
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            if (result.error.empty()) { result.error = e.what(); }
        }
        return result;
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
        install_host_globals(globals_, host_);
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

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_restricted_python_script_engine()
{
    return std::make_unique<PythonScriptEngine>(/*restricted=*/true);
}

void install_host_globals_for_docs(py::dict& globals)
{
    static const mosaic::engine::script::ScriptHost dummy_host{};
    install_host_globals(globals, dummy_host);
}
} // namespace mosaic::script::python
