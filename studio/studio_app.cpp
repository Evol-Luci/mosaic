#include "studio/studio_app.hpp"

#include <cstdio>
#include <string>

#include <imgui.h>

#include "studio/hierarchy.hpp"
#include "studio/inspector.hpp"
#include "studio/pick.hpp"
#include "engine/script/script_component.hpp"

namespace mosaic::studio
{

void StudioApp::draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection,
                        const mosaic::engine::assets::AssetRegistry& registry)
{
    // Full-window dockspace host so panels can be docked/tabbed/floated.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (!demo_entity_)
    {
        world.for_each<mosaic::engine::script::ScriptComponent>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::script::ScriptComponent& sc)
            {
                if (!demo_entity_) { demo_entity_ = e; demo_script_ = sc.source; }
            });
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save"))
            {
                scene_file_.request_save();
            }
            ImGui::Separator();
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

        ImGui::BeginDisabled(!demo_entity_.has_value());
        if (ImGui::Checkbox("Demo: physics", &demo_physics_))
        {
            set_demo_physics(world, *demo_entity_, demo_script_, demo_physics_);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !demo_entity_.has_value())
        {
            ImGui::SetTooltip("no scripted demo object in this scene");
        }

        ImGui::Text("  %s%s", scene_file_.path().c_str(), scene_file_.dirty() ? " *" : "");

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
                draw_viewport(device, world);
            }
            else if (panel.name == "Inspector")
            {
                if (draw_inspector(world, reflection))
                {
                    scene_file_.mark_dirty();
                }
            }
            else if (panel.name == "Assets")
            {
                draw_assets(registry);
            }
            else if (panel.name == "Console")
            {
                draw_console();
            }
            else if (panel.name == "Script Console")
            {
                draw_script_console(world, reflection);
            }
            else if (panel.name == "Script Editor")
            {
                draw_script_editor(world, reflection);
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

void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device,
                              mosaic::engine::World& world)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex != 0)
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(static_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));
        if (ImGui::IsItemClicked())
        {
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mp = ImGui::GetMousePos();
            const float  w  = static_cast<float>(s.width);
            const float  h  = static_cast<float>(s.height);
            const float  ndc_x = 2.0f * (mp.x - mn.x) / w - 1.0f;
            const float  ndc_y = 1.0f - 2.0f * (mp.y - mn.y) / h; // screen y-down -> ndc y-up
            const mosaic::engine::physics::RaycastHit hit = pick_entity(world, w / h, ndc_x, ndc_y);
            if (hit.hit) { selection_.select(hit.entity); }
            else         { selection_.clear(); }
        }
    }
    else
    {
        ImGui::TextUnformatted("(no viewport)");
    }
}

void StudioApp::draw_assets(const mosaic::engine::assets::AssetRegistry& registry)
{
    assets_.refresh(registry);
    for (const AssetEntry& entry : assets_.entries())
    {
        const std::string label =
            entry.type + "  " + (entry.source.empty() ? entry.id.to_string() : entry.source);
        const bool selected = assets_.has_selection() && assets_.selected() == entry.id;
        if (ImGui::Selectable(label.c_str(), selected))
        {
            assets_.select(entry.id);
        }
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

void StudioApp::draw_script_console(
    mosaic::engine::World& world,
    const mosaic::engine::reflection::ComponentReflection& reflection)
{
    ImGui::InputTextMultiline("##script_input", script_input_, sizeof(script_input_),
                              ImVec2(-1.0f, 120.0f));
    if (ImGui::Button("Run"))
    {
        script_console_.run(script_input_, world, reflection, commands_);
        script_input_[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { script_console_.clear_history(); }
    ImGui::Separator();
    ImGui::BeginChild("##script_history");
    for (const mosaic::studio::ConsoleEntry& entry : script_console_.history())
    {
        ImVec4 color(0.9f, 0.9f, 0.9f, 1.0f); // Output default
        if (entry.kind == mosaic::studio::ConsoleEntry::Kind::Input)
        {
            color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        }
        else if (entry.kind == mosaic::studio::ConsoleEntry::Kind::Error)
        {
            color = ImVec4(0.95f, 0.4f, 0.4f, 1.0f);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(entry.text.c_str());
        ImGui::PopStyleColor();
    }
    // Auto-scroll to the newest line when the view is already pinned to the bottom.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void StudioApp::draw_script_editor(
    mosaic::engine::World& world,
    const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("Select a scripted entity to edit its script.");
        return;
    }
    const mosaic::foundation::EntityID e = selection_.selected();
    mosaic::engine::script::ScriptComponent* sc =
        world.get_component<mosaic::engine::script::ScriptComponent>(e);
    if (sc == nullptr)
    {
        ImGui::TextUnformatted("Selected entity has no ScriptComponent.");
        return;
    }
    // Load the source into the edit buffer when the selected scripted entity changes.
    if (script_edit_entity_ != e)
    {
        script_edit_entity_ = e;
        std::snprintf(script_edit_buf_, sizeof(script_edit_buf_), "%s", sc->source.c_str());
        script_apply_result_ = ScriptApplyResult{};
    }
    ImGui::InputTextMultiline("##script_edit", script_edit_buf_, sizeof(script_edit_buf_),
                              ImVec2(-1.0f, 200.0f));
    if (ImGui::Button("Apply"))
    {
        script_apply_result_ = script_editor_.apply(script_edit_buf_, world, reflection, e);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(applies live on Play)");
    if (script_apply_result_.kind == ScriptApplyResult::Kind::Ok)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "compiled OK");
    }
    else if (script_apply_result_.kind == ScriptApplyResult::Kind::Error)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", script_apply_result_.message.c_str());
        ImGui::PopStyleColor();
    }
}

bool StudioApp::draw_inspector(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(nothing selected)");
        return false;
    }
    const auto entity = selection_.selected();
    ImGuiFieldVisitor visitor(assets_.selected(), assets_.has_selection());
    for (const std::string& name : reflection.components_on(world, entity))
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
        }
        ImGui::PopID();
    }
    return visitor.changed();
}

} // namespace mosaic::studio
