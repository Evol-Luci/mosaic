#include "studio/inspector.hpp"

#include <string>

#include <imgui.h>

namespace mosaic::studio
{

void ImGuiFieldVisitor::field_float(std::string_view name, float& value)
{
    ImGui::DragFloat(std::string(name).c_str(), &value, 0.05f);
}

void ImGuiFieldVisitor::field_readonly(std::string_view name, const std::string& display)
{
    ImGui::LabelText(std::string(name).c_str(), "%s", display.c_str());
}

void ImGuiFieldVisitor::begin_group(std::string_view name)
{
    const std::string label(name);
    ImGui::PushID(label.c_str());
    ImGui::TextUnformatted(label.c_str());
    ImGui::Indent();
}

void ImGuiFieldVisitor::end_group()
{
    ImGui::Unindent();
    ImGui::PopID();
}

} // namespace mosaic::studio
