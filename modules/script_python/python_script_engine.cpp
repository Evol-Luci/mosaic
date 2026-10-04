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
// per process and never finalize (the process reclaims it at exit). After init this function
// RELEASES the GIL, so no thread owns it implicitly and every entry into CPython must acquire
// it -- see the block below. This comment used to say the opposite ("the calling (main) thread
// holds the GIL ... no explicit GIL handoff"), and that was the bug: whoever won the
// std::call_once race kept the GIL, so an engine built on a worker thread called PyDict_New()
// with no GIL and segfaulted.
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
        // Hand the GIL back so NO thread owns it implicitly. initialize_interpreter() leaves
        // it held by whoever called it, and for a long time nothing handed it off -- so an
        // engine created on a worker thread called PyDict_New() with no GIL and segfaulted,
        // but only when some earlier engine had already been created on the main thread.
        // Releasing here makes the rule stateable and uniform: every entry into CPython
        // acquires, and no caller is privileged by having won the std::call_once race.
        //
        // Deliberately leaked: this interpreter is never finalized (see above), so the saved
        // thread state must outlive every engine, and there is nothing to restore it to.
        static py::gil_scoped_release* const release_gil_forever = new py::gil_scoped_release();
        (void)release_gil_forever;

    });
}

namespace
{
// Helper base class: initializes CPython before the engine's constructor body runs.
// The requirement is still real but the reason has changed: globals_ is now an optional
// emplaced inside the body, so this is no longer about beating a py::dict member
// initializer -- it is that the body's very first act is py::gil_scoped_acquire, which
// needs a live interpreter. Base classes are constructed before the body, so placing
// InterpreterInit first guarantees that.
struct InterpreterInit
{
    InterpreterInit() { ensure_interpreter(); }
};

// Reads a 3-element Python sequence into a Vec3. Returns false instead of throwing for a
// non-sequence, a short sequence, or a non-numeric element, so one malformed script argument
// fails that call rather than aborting the tick.
//
// Takes py::handle rather than py::sequence deliberately. With py::sequence in a binding's
// signature, pybind11's dispatcher rejects a non-sequence BEFORE the binding body runs, so
// this guard never got a chance and the TypeError aborted the tick. A dict is the likeliest
// wrong argument -- component values on this same scripting surface are dicts
// (tr['position']['x']) -- so it is a reasonable mistake, not a perverse one.
//
// Note that str passes PySequence_Check, so a string falls through to the element casts
// below and fails there instead. Two layers, both ending in false.
bool to_vec3(const py::handle& obj, mosaic::foundation::math::Vec3& out)
{
    if (!py::isinstance<py::sequence>(obj)) { return false; }
    const auto s = py::reinterpret_borrow<py::sequence>(obj);
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
        [host](py::object origin, py::object direction, float max_distance) -> py::dict
        {
            using mosaic::foundation::math::Vec3;
            mosaic::engine::script::ScriptRayHit h;
            if (host.raycast)
            {
                // Guarded, like apply_impulse's: a short sequence or a non-numeric element
                // used to raise out of pybind11 and -- uncaught -- abort the whole tick over
                // one bad call. On malformed input `h` stays default-constructed, so this
                // returns exactly the miss dict it already returns when no host callback is
                // set, rather than inventing a second failure shape. The host is deliberately
                // not called at all: filling the missing components with zeros and casting
                // anyway would hand the simulation a ray the script never asked for.
                Vec3 o{}, d{};
                if (to_vec3(origin, o) && to_vec3(direction, d))
                {
                    h = host.raycast(o, d, max_distance);
                }
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
        py::doc("Cast a ray into the scene and return hit data as a dictionary. origin and "
                "direction are 3-element sequences. Reports a miss (hit False) if there is "
                "no physics system or an argument is malformed -- not a sequence at all "
                "(a dict, for instance), too short, or holding a non-number -- rather than "
                "raising, since a raising binding would abort the whole tick over one bad "
                "call."));

    // apply_impulse(entity, impulse, point) -> bool. impulse/point are 3-element
    // sequences (matching raycast). Malformed input returns False rather than
    // raising: a raising binding would abort the whole tick over one bad call.
    globals["apply_impulse"] = py::cpp_function(
        [host](std::uint64_t entity, py::object impulse, py::object point) -> bool
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
                "malformed -- not a sequence at all (a dict, for instance), too "
                "short, or holding a non-number.\n\n"
                "Under --script-thread, True means 'accepted for replay on the main "
                "thread', not 'a body was pushed'. The worker can judge only "
                "entities in its own job snapshot; a push aimed at anything else -- "
                "including an entity id that does not exist -- is buffered and "
                "returns True, and the main-thread replay drops it if there is no "
                "body. Pushing some OTHER object is the normal case this protects, "
                "since answering False there would make a working single-threaded "
                "script silently do nothing under the flag."));
}

class PythonScriptEngine final
    : private InterpreterInit // must be first — the ctor body's gil_scoped_acquire needs a live interpreter
    , public mosaic::engine::script::ScriptEngine
{
public:
    explicit PythonScriptEngine(bool restricted = false)
    {
        // Interpreter is already running (InterpreterInit ran first), but no thread holds the
        // GIL, so take it before touching CPython at all -- including constructing globals_.
        const py::gil_scoped_acquire gil;
        globals_.emplace();
        // On a throw from here, locals are destroyed before member subobjects -- so `gil`
        // would release and only THEN would globals_ be destroyed, decref'ing with no GIL:
        // structurally the same abort this whole change exists to remove. Reset it while the
        // guard is still alive. Practically unreachable (it needs OOM, or an interpreter
        // already wedged) -- which is precisely how the original bug stayed hidden.
        try
        {
        if (restricted)
        {
            // allow_open=true: this constructor path is only ever reached via
            // create_restricted_python_script_engine(), used exclusively for asset
            // importers, whose entire purpose is reading their own source file.
            install_restricted_builtins(*globals_, /*allow_open=*/true);
        }
        else
        {
            // Give the namespace access to builtins (print, etc.).
            (*globals_)["__builtins__"] = py::module_::import("builtins");
        }
        }
        catch (...)
        {
            globals_.reset();
            throw;
        }
    }

    // Releasing Python references needs the GIL just as much as acquiring them did, and a
    // py:: member is destroyed AFTER this body runs -- by which point any guard declared here
    // is gone. So every py:: member must be emptied HERE, under the guard: resetting leaves a
    // null handle whose later destruction decrefs nothing.
    //
    // If you add a py:: member to this class, empty it here too. Forgetting trace_fn_ cost an
    // abort ("dec_ref() PyGILState_Check() failure" on a builtin_function_or_method) that only
    // showed up once a debugger had been attached, because the member is otherwise null.
    ~PythonScriptEngine() override
    {
        const py::gil_scoped_acquire gil;
        trace_fn_ = py::object{};
        globals_.reset();
    }

    mosaic::engine::script::ScriptResult run_string(const std::string& code) override
    {
        const py::gil_scoped_acquire gil;
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
                py::exec(code, *globals_);
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
        const py::gil_scoped_acquire gil;
        world_ = &world;
        types_ = &types;
        install_world_globals(*globals_, &world_, &types_, &stack_);
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        const py::gil_scoped_acquire gil;
        (*globals_)[py::str(name)] = value_to_py(value);
    }

    std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) override
    {
        const py::gil_scoped_acquire gil;
        if (!globals_->contains(name)) { return std::nullopt; }
        try
        {
            return py_to_value((*globals_)[py::str(name)]);
        }
        catch (const std::exception&)
        {
            return std::nullopt; // e.g. a function/class/other unmarshalable object
        }
    }

    mosaic::engine::script::ScriptResult call_function(const std::string& name) override
    {
        const py::gil_scoped_acquire gil;
        mosaic::engine::script::ScriptResult result;
        if (!globals_->contains(name) || !py::isinstance<py::function>((*globals_)[py::str(name)]))
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
                (*globals_)[py::str(name)]();
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
        const py::gil_scoped_acquire gil;  // uniformity, not necessity: no Python here
        // undo/redo/undo_depth are installed in bind_world and read stack_ at call time, so
        // binding the stack here activates them for all subsequent script calls.
        stack_ = &stack;
    }

    void bind_host(const mosaic::engine::script::ScriptHost& host) override
    {
        const py::gil_scoped_acquire gil;
        host_ = host;
        install_host_globals(*globals_, host_);
    }

    void bind_context(mosaic::engine::script::ScriptContext& context) override
    {
        const py::gil_scoped_acquire gil;
        context_ = &context;
        (*globals_)["get_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name) -> py::object
            {
                auto v = context_->get(mosaic::foundation::EntityID(eid), name);
                return v ? value_to_py(*v) : py::none();
            });
        (*globals_)["set_component"] = py::cpp_function(
            [this](std::uint64_t eid, const std::string& name, py::handle value) -> bool
            {
                return context_->set(mosaic::foundation::EntityID(eid), name,
                                     py_to_value(value));
            });
    }

    void bind_debugger(mosaic::engine::script::ScriptDebugger* debugger) override
    {
        const py::gil_scoped_acquire gil;  // uniformity, not necessity: no Python here
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

    // Explicitly managed rather than a plain py::dict member, and the reason is ordering:
    // py::dict's constructor calls PyDict_New(), members are constructed BEFORE the
    // constructor body runs, and destroyed AFTER the destructor body -- so a plain member
    // leaves no scope in which to hold the GIL for its own construction and destruction,
    // which are exactly the calls that used to crash. An optional moves both into ordinary
    // statements this class can wrap. A GIL-holding base class would get the ordering right
    // but would hold the GIL for the engine's whole lifetime and block every other thread.
    std::optional<py::dict> globals_; // this engine's isolated namespace (interpreter outlives it)
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
