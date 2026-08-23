#include "studio/studio_app.hpp"

#include <imgui.h>

#include "studio/hierarchy.hpp"

namespace mosaic::studio
{

void StudioApp::draw_ui(const mosaic::engine::World& world)
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
            else if (panel.name == "Inspector")
            {
                draw_inspector();
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

void StudioApp::draw_inspector()
{
    if (selection_.has_selection())
    {
        ImGui::Text("Selected: Entity %u", selection_.selected().index());
    }
    else
    {
        ImGui::TextUnformatted("(nothing selected)");
    }
}

} // namespace mosaic::studio
