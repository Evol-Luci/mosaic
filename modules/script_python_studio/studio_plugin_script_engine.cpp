#include "modules/script_python_studio/studio_plugin_script_engine.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <imgui.h>
#include <pybind11/embed.h>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python/restricted_builtins.hpp"
#include "modules/script_python/value_marshal.hpp"
#include "modules/script_python/world_globals.hpp"
#include "modules/script_python_studio/imgui_globals.hpp"

namespace py = pybind11;

namespace mosaic::script::python_studio
{
namespace
{
struct InterpreterInit
{
    InterpreterInit() { mosaic::script::python::ensure_interpreter(); }
};

class StudioPluginScriptEngine final
    : private InterpreterInit // must be first — the ctor body's gil_scoped_acquire needs a live interpreter
    , public mosaic::engine::script::ScriptEngine
{
public:
    StudioPluginScriptEngine()
    {
        // No thread holds the GIL implicitly (see ensure_interpreter), so take it before
        // touching CPython -- including constructing globals_, which is why that member is an
        // optional rather than a plain py::dict: members are built before the constructor
        // body, leaving no scope in which to guard PyDict_New.
        const py::gil_scoped_acquire gil;
        globals_.emplace();
        // On a throw, locals die before member subobjects, so `gil` would release before
        // globals_ is destroyed -- an unguarded decref, the same abort this change removes.
        // install_restricted_builtins throws py::error_already_set if PyModule_New fails.
        try
        {
            mosaic::script::python::install_restricted_builtins(*globals_);
        }
        catch (...)
        {
            globals_.reset();
            throw;
        }
    }

    // Dropping Python references needs the GIL exactly as acquiring them did.
    ~StudioPluginScriptEngine() override
    {
        const py::gil_scoped_acquire gil;
        globals_.reset();
    }

    mosaic::engine::script::ScriptResult run_string(const std::string& code) override
    {
        const py::gil_scoped_acquire gil;
        mosaic::engine::script::ScriptResult result;
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
        catch (const std::exception& e)
        {
            result.ok = false;
            result.error = e.what();
        }
        return result;
    }

    void bind_world(mosaic::engine::World& world,
                    const mosaic::engine::reflection::ComponentReflection& types) override
    {
        const py::gil_scoped_acquire gil;
        world_ = &world;
        types_ = &types;
        mosaic::script::python::install_world_globals(*globals_, &world_, &types_, &stack_);
        mosaic::script::python_studio::install_imgui_globals(
            *globals_, [this] { require_drawing(); }, &child_depth_);
    }

    void bind_commands(mosaic::foundation::commands::CommandStack& stack) override
    {
        const py::gil_scoped_acquire gil;  // uniformity, not necessity: no Python here
        stack_ = &stack;
    }

    void set_global(const std::string& name,
                    const mosaic::foundation::serialization::Value& value) override
    {
        const py::gil_scoped_acquire gil;
        (*globals_)[py::str(name)] = mosaic::script::python::value_to_py(value);
    }

    std::optional<mosaic::foundation::serialization::Value>
    get_global(const std::string& name) override
    {
        const py::gil_scoped_acquire gil;
        if (!globals_->contains(name)) { return std::nullopt; }
        try
        {
            return mosaic::script::python::py_to_value((*globals_)[py::str(name)]);
        }
        catch (const std::exception&)
        {
            return std::nullopt;
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
        child_depth_ = 0;
        drawing_ = true;
        struct DrawingGuard
        {
            bool& flag;
            ~DrawingGuard() noexcept { flag = false; }
        } drawing_guard{drawing_};
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

        if (child_depth_ > 0)
        {
            const int unclosed = child_depth_;
            while (child_depth_ > 0) { ImGui::EndChild(); --child_depth_; }
            result.ok = false;
            result.error = (result.error.empty() ? std::string() : result.error + "; ") +
                           std::to_string(unclosed) +
                           " imgui_begin_child() call(s) were never matched by imgui_end_child()";
        }
        return result;
    }

private:
    // Every imgui_* global calls this first: outside an active panel draw (call_function
    // sets drawing_ only for the duration of the plugin's draw function), there is no
    // current ImGui window, and calling straight into ImGui would null-deref and crash
    // Studio instead of reporting a normal plugin error.
    void require_drawing() const
    {
        if (!drawing_)
        {
            throw std::runtime_error(
                "imgui_* functions may only be called from inside a MOSAIC_PANELS draw function");
        }
    }

    std::optional<py::dict> globals_; // explicitly managed so the GIL can cover it
    mosaic::engine::World* world_ = nullptr;
    const mosaic::engine::reflection::ComponentReflection* types_ = nullptr;
    mosaic::foundation::commands::CommandStack* stack_ = nullptr;
    int child_depth_ = 0;
    bool drawing_ = false;
};
} // namespace

std::unique_ptr<mosaic::engine::script::ScriptEngine> create_studio_plugin_script_engine()
{
    return std::make_unique<StudioPluginScriptEngine>();
}
} // namespace mosaic::script::python_studio
