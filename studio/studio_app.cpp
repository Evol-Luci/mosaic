#include "studio/studio_app.hpp"

#include <string>

#include <imgui.h>

#include "studio/hierarchy.hpp"
#include "studio/inspector.hpp"

namespace mosaic::studio
{

void StudioApp::draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection)
{
    // Full-window dockspace host so panels can be docked/tabbed/floated.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Exit"))
            {
                layout_.request_exit();
            }
            ImGui::EndMenu();
        }

        // Simulation toolbar: Play/Stop toggles running; Step ticks once while paused.
        if (ImGui::Button(play_.is_playing() ? "Stop" : "Play"))
        {
            play_.toggle();
        }
        ImGui::BeginDisabled(play_.is_playing());
        if (ImGui::Button("Step"))
        {
            play_.request_step();
        }
        ImGui::EndDisabled();

        ImGui::EndMainMenuBar();
    }

    for (const Panel& panel : layout_.panels())
    {
        if (!panel.open)
        {
            continue;
        }
        if (ImGui::Begin(panel.name.c_str()))
        {
            if (panel.name == "Hierarchy")
            {
                draw_hierarchy(world);
            }
            else if (panel.name == "Viewport")
            {
                draw_viewport(device);
            }
            else if (panel.name == "Inspector")
            {
                draw_inspector(world, reflection);
            }
            else if (panel.name == "Console")
            {
                draw_console();
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
        }
        ImGui::End();
    }
}

void StudioApp::draw_hierarchy(const mosaic::engine::World& world)
{
    for (const HierarchyRow& row : build_hierarchy(world))
    {
        if (ImGui::Selectable(row.label.c_str(), selection_.is_selected(row.id)))
        {
            selection_.select(row.id);
        }
    }
}

void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex != 0)
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(static_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));
    }
    else
    {
        ImGui::TextUnformatted("(no viewport)");
    }
}

void StudioApp::draw_console()
{
    for (const ConsoleLine& line : console_->snapshot())
    {
        std::string text = "[" + std::string(mosaic::foundation::log::to_string(line.level)) +
                           "] " + line.category + ": " + line.message;
        ImGui::TextUnformatted(text.c_str());
    }
    // Auto-scroll to the newest line when the view is already pinned to the bottom.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
}

void StudioApp::draw_inspector(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(nothing selected)");
        return;
    }
    const auto entity = selection_.selected();
    ImGuiFieldVisitor visitor;
    for (const std::string& name : reflection.components_on(world, entity))
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
        }
        ImGui::PopID();
    }
}

} // namespace mosaic::studio
