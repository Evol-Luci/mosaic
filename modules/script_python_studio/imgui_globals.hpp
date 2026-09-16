#pragma once

#include <functional>

#include <pybind11/pybind11.h>

namespace mosaic::script::python_studio
{
namespace py = pybind11;

// Installs the 10 imgui_* Studio-panel-drawing globals (imgui_text, imgui_separator,
// imgui_same_line, imgui_spacing, imgui_button, imgui_checkbox, imgui_input_text,
// imgui_slider_float, imgui_begin_child, imgui_end_child) into `globals`. Every
// registered function calls `require_drawing()` first (the caller's guard against
// calling into ImGui outside an active panel draw); `child_depth` tracks
// imgui_begin_child/imgui_end_child balance exactly as StudioPluginScriptEngine's
// child_depth_ member did before this was extracted. Installing never invokes any
// of the registered functions or `require_drawing` itself -- safe to call with a
// no-op callback purely to read the registrations' names/docstrings (see
// app/docs/scripting_globals_reference.cpp).
void install_imgui_globals(py::dict& globals, std::function<void()> require_drawing,
                           int* child_depth);
} // namespace mosaic::script::python_studio
