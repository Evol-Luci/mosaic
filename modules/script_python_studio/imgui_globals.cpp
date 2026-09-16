#include "modules/script_python_studio/imgui_globals.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

namespace mosaic::script::python_studio
{
void install_imgui_globals(py::dict& globals, std::function<void()> require_drawing,
                           int* child_depth)
{
    globals["imgui_text"] = py::cpp_function(
        [require_drawing](const std::string& text)
        {
            require_drawing();
            ImGui::TextUnformatted(text.c_str());
        },
        py::doc("Draw unformatted text in the current panel."));
    globals["imgui_separator"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::Separator(); },
        py::doc("Draw a horizontal separator line."));
    globals["imgui_same_line"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::SameLine(); },
        py::doc("Keep the next widget on the same line as the previous one."));
    globals["imgui_spacing"] = py::cpp_function(
        [require_drawing]() { require_drawing(); ImGui::Spacing(); },
        py::doc("Add a small vertical gap."));
    globals["imgui_button"] = py::cpp_function(
        [require_drawing](const std::string& label) -> bool
        {
            require_drawing();
            return ImGui::Button(label.c_str());
        },
        py::doc("Draw a clickable button; returns True on the frame it's clicked."));
    globals["imgui_checkbox"] = py::cpp_function(
        [require_drawing](const std::string& label, bool value) -> bool
        {
            require_drawing();
            ImGui::Checkbox(label.c_str(), &value);
            return value;
        },
        py::doc("Draw a checkbox with the given label and current value; returns the "
                "(possibly toggled) value."));
    globals["imgui_input_text"] = py::cpp_function(
        [require_drawing](const std::string& label, const std::string& value) -> std::string
        {
            require_drawing();
            char buf[1024];
            std::snprintf(buf, sizeof(buf), "%s", value.c_str());
            ImGui::InputText(label.c_str(), buf, sizeof(buf));
            return std::string(buf);
        },
        py::doc("Draw a single-line text field with the given label and current value; "
                "returns the (possibly edited) value."));
    globals["imgui_slider_float"] = py::cpp_function(
        [require_drawing](const std::string& label, float value, float lo, float hi) -> float
        {
            require_drawing();
            ImGui::SliderFloat(label.c_str(), &value, lo, hi);
            return value;
        },
        py::doc("Draw a float slider between lo and hi with the given label and current "
                "value; returns the (possibly changed) value."));
    globals["imgui_begin_child"] = py::cpp_function(
        [require_drawing, child_depth](const std::string& id, float height)
        {
            require_drawing();
            ImGui::BeginChild(id.c_str(), ImVec2(0.0f, height));
            ++(*child_depth);
        },
        py::doc("Begin a scrollable child region with the given id and height; must be "
                "matched by imgui_end_child."));
    globals["imgui_end_child"] = py::cpp_function(
        [require_drawing, child_depth]()
        {
            require_drawing();
            if (*child_depth > 0)
            {
                ImGui::EndChild();
                --(*child_depth);
            }
        },
        py::doc("End the most recently opened child region started by imgui_begin_child."));
}
} // namespace mosaic::script::python_studio
