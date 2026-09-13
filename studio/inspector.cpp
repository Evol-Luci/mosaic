#include "studio/inspector.hpp"

#include <algorithm>
#include <cstring>
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

void ImGuiFieldVisitor::field_string(std::string_view name, std::string& value)
{
    // ImGui::InputText edits a caller-owned fixed buffer; it manages its own active-
    // widget state internally (keyed by the widget's id, i.e. `label`), so re-seeding
    // the buffer from `value` at the top of every frame is safe — InputText only
    // reads it back out on the frame the widget first becomes active, and every
    // change is written back into `value` immediately below, so the two never drift.
    char buf[256];
    const std::size_t n = std::min(value.size(), sizeof(buf) - 1);
    std::memcpy(buf, value.data(), n);
    buf[n] = '\0';
    if (ImGui::InputText(std::string(name).c_str(), buf, sizeof(buf)))
    {
        value = buf;
        changed_ = true;
    }
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
