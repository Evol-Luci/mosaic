#include "studio/inspector.hpp"

#include <string>

#include <imgui.h>

namespace mosaic::studio
{

void ImGuiFieldVisitor::field_float(std::string_view name, float& value)
{
    if (ImGui::DragFloat(std::string(name).c_str(), &value, 0.05f)) { changed_ = true; }
}

void ImGuiFieldVisitor::field_readonly(std::string_view name, const std::string& display)
{
    ImGui::LabelText(std::string(name).c_str(), "%s", display.c_str());
}

void ImGuiFieldVisitor::field_asset(std::string_view name,
                                    mosaic::foundation::serialization::AssetID& value)
{
    const std::string label(name);
    ImGui::PushID(label.c_str());
    ImGui::LabelText(label.c_str(), "%s", value.to_string().c_str());
    ImGui::BeginDisabled(!has_selection_);
    if (ImGui::Button("Assign selected"))
    {
        value = selected_;
        changed_ = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
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
