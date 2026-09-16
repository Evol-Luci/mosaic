#include <functional>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <pybind11/embed.h>

#include "modules/script_python/python_script_engine.hpp" // ensure_interpreter
#include "modules/script_python_studio/imgui_globals.hpp"

namespace py = pybind11;

TEST_CASE("install_imgui_globals registers all 10 names without invoking them", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");

    bool require_drawing_called = false;
    int child_depth = 0;
    mosaic::script::python_studio::install_imgui_globals(
        globals, [&] { require_drawing_called = true; }, &child_depth);

    const char* names[] = {"imgui_text", "imgui_separator", "imgui_same_line", "imgui_spacing",
                           "imgui_button", "imgui_checkbox", "imgui_input_text",
                           "imgui_slider_float", "imgui_begin_child", "imgui_end_child"};
    for (const char* name : names)
    {
        REQUIRE(globals.contains(name));
    }
    REQUIRE_FALSE(require_drawing_called);
}

TEST_CASE("install_imgui_globals: every global has a non-empty docstring", "[script]")
{
    mosaic::script::python::ensure_interpreter();

    py::dict globals;
    globals["__builtins__"] = py::module_::import("builtins");
    int child_depth = 0;
    mosaic::script::python_studio::install_imgui_globals(globals, [] {}, &child_depth);

    const char* names[] = {"imgui_text", "imgui_separator", "imgui_same_line", "imgui_spacing",
                           "imgui_button", "imgui_checkbox", "imgui_input_text",
                           "imgui_slider_float", "imgui_begin_child", "imgui_end_child"};
    for (const char* name : names)
    {
        const std::string doc = globals[name].attr("__doc__").cast<std::string>();
        INFO("global: " << name);
        REQUIRE(doc.find('\n') != std::string::npos);
    }
}
