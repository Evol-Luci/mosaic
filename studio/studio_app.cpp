#include "studio/studio_app.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include <imgui.h>

#include "studio/hierarchy.hpp"
#include "studio/inspector.hpp"
#include "studio/live_link.hpp"
#include "studio/pick.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_debugger.hpp"
#include "foundation/serialization/json_backend.hpp"

namespace mosaic::studio
{

void StudioApp::reload_plugins(mosaic::engine::World& world,
                               const mosaic::engine::reflection::ComponentReflection& reflection)
{
    for (const std::string& err :
        plugin_host_.load(plugins_dir_, world, reflection, commands_, disabled_plugin_files_))
    {
        mosaic::foundation::log::error("plugin", "{}", err);
    }
    for (const PluginPanel& panel : plugin_host_.panels())
    {
        layout_.add_panel(panel.label);
    }
}

void StudioApp::draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection,
                        const mosaic::engine::assets::AssetRegistry& registry)
{
    // Full-window dockspace host so panels can be docked/tabbed/floated.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // Startup-only "Recent Projects" picker -- offered when no project/scene was
    // named on the command line and there's something to pick from (see
    // app/studio_prefs.hpp's should_show_recent_projects_picker()). Dismissible via
    // the window's own close button (p_open below), in which case the editor just
    // continues with whatever scene it already loaded -- unchanged prior behavior.
    if (show_recent_projects_picker_)
    {
        ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Recent Projects", &show_recent_projects_picker_))
        {
            ImGui::TextWrapped(
                "No project was given on the command line. Pick a recent one to reopen, "
                "or close this window to continue with the current scene.");
            ImGui::Separator();
            for (const std::string& path : recent_projects_)
            {
                if (ImGui::Selectable(path.c_str()))
                {
                    reopen_requested_          = path;
                    show_recent_projects_picker_ = false;
                }
            }
        }
        ImGui::End();
    }

    if (!demo_entity_)
    {
        world.for_each<mosaic::engine::script::ScriptComponent>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::script::ScriptComponent& sc)
            {
                if (!demo_entity_) { demo_entity_ = e; demo_script_ = sc.source; }
            });
    }

    if (!plugins_loaded_)
    {
        plugins_loaded_ = true;
        reload_plugins(world, reflection);
        // The initial load establishes plugin_watcher_'s baseline too -- its very
        // first poll() call always reports "unchanged" regardless, so ordering here
        // doesn't matter, but doing it now keeps both plugin_loaded_ and the
        // watcher's baseline set together rather than one frame apart.
        plugin_watcher_.poll(plugins_dir_);
    }
    else if (plugin_watcher_.poll(plugins_dir_))
    {
        // Hot-reload: a *.py file under plugins_dir_ was added, removed, or edited
        // since the last frame -- rerun the same load the manual "Reload Plugins"
        // menu item triggers, so editing a plugin on disk takes effect without a
        // click. hot-reload deliberately does not bypass the per-plugin enable/
        // disable flag (disabled_plugin_files_ is unchanged): a disabled plugin's
        // edits are still skipped, exactly like a fresh load would skip them.
        mosaic::foundation::log::info("plugin", "plugins/ changed on disk, reloading");
        reload_plugins(world, reflection);
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save"))
            {
                scene_file_.request_save();
            }
            if (ImGui::MenuItem("Reload"))
            {
                if (scene_file_.dirty())
                {
                    // Defer OpenPopup to right before BeginPopupModal below (same
                    // ID-stack depth, outside the menu bar) -- calling OpenPopup
                    // from inside a menu computes a popup ID scoped to the menu's
                    // own floating window, which BeginPopupModal (called after
                    // EndMainMenuBar, a different window) would never match.
                    want_reload_confirm_ = true;
                }
                else
                {
                    scene_file_.request_reload();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
            {
                layout_.request_exit();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Plugins"))
        {
            if (ImGui::MenuItem("Reload Plugins"))
            {
                reload_plugins(world, reflection);
                // Re-baseline the watcher against what was just loaded, so the
                // manual reload doesn't leave a stale snapshot that hot-reload
                // would otherwise (harmlessly, but redundantly) fire on next frame.
                plugin_watcher_.poll(plugins_dir_);
            }
            ImGui::Separator();
            for (const PluginMenuItem& item : plugin_host_.menu_items())
            {
                if (ImGui::MenuItem(item.label.c_str()))
                {
                    const auto result = plugin_host_.invoke(item);
                    if (!result.ok)
                    {
                        mosaic::foundation::log::error("plugin", "{}: {}", item.plugin_name,
                                                       result.error);
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            // Every panel (built-in and plugin-registered) shows here with a checkmark for
            // its open state; clicking toggles it. Plugin panels are added closed (see
            // StudioLayout::add_panel), so this is the only way to ever open one.
            for (const Panel& panel : layout_.panels())
            {
                bool open = panel.open;
                if (ImGui::MenuItem(panel.name.c_str(), nullptr, &open))
                {
                    layout_.toggle(panel.name);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Theme"))
        {
            static const std::pair<const char*, mosaic::engine::render::UiTheme> kThemes[] = {
                {"Dark", mosaic::engine::render::UiTheme::Dark},
                {"Light", mosaic::engine::render::UiTheme::Light},
                {"Classic", mosaic::engine::render::UiTheme::Classic},
            };
            for (const auto& [label, theme] : kThemes)
            {
                if (ImGui::MenuItem(label, nullptr, current_theme_ == theme))
                {
                    current_theme_ = theme;
                    device.set_ui_theme(theme);
                }
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

        if (ImGui::Button(live_link_label(live_link_.state())))
        {
            if (live_link_.state() == LiveLinkState::Off) { live_link_.start(kDefaultLiveLinkPort); }
            else                                          { live_link_.stop(); }
        }

        ImGui::Text("  %s%s", scene_file_.path().c_str(), scene_file_.dirty() ? " *" : "");

        ImGui::EndMainMenuBar();
    }

    if (want_reload_confirm_)
    {
        ImGui::OpenPopup("Discard changes and reload?");
        want_reload_confirm_ = false;
    }
    if (ImGui::BeginPopupModal("Discard changes and reload?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("This scene has unsaved changes. Reloading discards them.");
        if (ImGui::Button("Reload", ImVec2(120, 0)))
        {
            scene_file_.request_reload();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (play_.is_playing())
    {
        // Studio is authoritative while simulating: push every named entity's
        // current (possibly physics-driven) Transform to Blender.
        world.for_each<mosaic::engine::Name>(
            [&](mosaic::engine::World::EntityID e, mosaic::engine::Name& name)
            {
                if (const auto* t = world.get_component<mosaic::engine::transform::Transform>(e))
                {
                    live_link_.send(format_live_link_transform_message(name, *t));
                }
            });
        live_link_.poll(); // drain the socket; inbound is ignored while playing
    }
    else
    {
        // Blender is authoritative while stopped (unchanged from the original slice).
        for (const std::string& line : live_link_.poll())
        {
            if (auto update = parse_live_link_message(line))
            {
                apply_live_link_update(world, *update);
            }
        }
    }

    for (const Panel& panel : layout_.panels())
    {
        if (!panel.open)
        {
            if (panel.name == "Viewport")
            {
                // Closed from the Window menu: same reasoning as the collapsed case below.
                viewport_.set_pointer_sample(ViewportPointerSample{}, play_.is_playing());
            }
            continue;
        }
        const bool begun = ImGui::Begin(panel.name.c_str());
        if (!begun && panel.name == "Viewport")
        {
            // The Viewport panel is collapsed, or an inactive dock tab, so draw_viewport
            // never runs and nothing would refresh the pointer sample. Without this, the
            // sample and the press latch freeze at their last values -- and if draws resume
            // while a NEW press is in flight, the latch's fresh-press branch never fires and
            // the previous press's verdict is reused. Found in review 2026-10-07.
            viewport_.set_pointer_sample(ViewportPointerSample{}, play_.is_playing());
        }
        if (begun)
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
            else if (panel.name == "Controls")
            {
                if (draw_controls(world))
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
            else if (const PluginPanel* p = plugin_host_.find_panel(panel.name))
            {
                const auto result = plugin_host_.invoke_panel(*p);
                if (!result.ok)
                {
                    mosaic::foundation::log::error("plugin", "{}: {}", p->plugin_name, result.error);
                }
            }
            else
            {
                ImGui::TextUnformatted("(empty)");
            }
        }
        ImGui::End();
    }
}

void StudioApp::draw_hierarchy(mosaic::engine::World& world)
{
    for (const HierarchyRow& row : build_hierarchy(world))
    {
        // Selectable's ID defaults to its label text, so two entities sharing a
        // Name (e.g. both "Cube") collide ("2 visible items with conflicting
        // ID"). Scope each row's ID by its entity index instead, independent
        // of the (possibly duplicate) label.
        ImGui::PushID(static_cast<int>(row.id.index()));
        if (ImGui::Selectable(row.label.c_str(), selection_.is_selected(row.id)))
        {
            selection_.select(row.id);
        }
        ImGui::PopID();
    }
}

void StudioApp::draw_viewport(mosaic::engine::render::RenderDevice& device,
                              mosaic::engine::World& world)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    viewport_.set_measured_size(avail.x, avail.y);

    const auto tex = device.viewport_texture();
    if (tex == 0)
    {
        // No offscreen texture yet (first frame, or a device that does not implement the
        // viewport hooks). There is no image to hover, so the scene must see NO pointer --
        // not the last one it saw. Recording an empty sample rather than returning early
        // keeps the "set_pointer_sample sees every frame draw_viewport runs" invariant true.
        viewport_.set_pointer_sample(ViewportPointerSample{}, play_.is_playing());
        ImGui::TextUnformatted("(no viewport)");
    }
    else
    {
        const ViewportSize s = viewport_.size();
        ImGui::Image(static_cast<ImTextureID>(tex),
                     ImVec2(static_cast<float>(s.width), static_cast<float>(s.height)));

        // Record the raw pointer facts for the scene's provider. Everything ImGui-specific
        // stops here: ViewportPanel applies the normalisation and the suppression rules, so
        // all of that policy is unit-tested with no window. These five reads are the part
        // no headless test can reach, which is why they are also pinned textually.
        //
        // ImGui::GetMousePos, not glfwGetCursorPos: ImGui's is frame-snapshotted, so
        // UiSystem and ClickNudgeSystem cannot see different positions within one tick --
        // which would have the UI-claim arbitration declining a different click than the one
        // ClickNudgeSystem acts on.
        //
        // DELIBERATELY NOT ImGui::GetIO().WantCaptureMouse. It reads like "ImGui wants the
        // mouse", but it is true whenever the cursor is over ANY ImGui window -- and the
        // Viewport panel is one -- so pairing it with IsItemHovered() made `down`
        // unreachable and shipped this feature dead. IsItemHovered() already returns false
        // for a slider drag elsewhere, for a window in front, and under a modal. See the
        // long note on ViewportPointerSample::hovered.
        mosaic::studio::ViewportPointerSample sample;
        const ImVec2 rect_min = ImGui::GetItemRectMin();
        const ImVec2 mouse    = ImGui::GetMousePos();
        sample.mouse_x         = mouse.x;
        sample.mouse_y         = mouse.y;
        sample.rect_min_x      = rect_min.x;
        sample.rect_min_y      = rect_min.y;
        sample.hovered         = ImGui::IsItemHovered();
        sample.button_down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        viewport_.set_pointer_sample(sample, play_.is_playing());

        // Click-to-select is the EDIT-mode behaviour, and it is gated so the two meanings of
        // a viewport click cannot both fire. Without !is_playing(), one play-mode click both
        // drives the game's pointer AND moves the selection, so the Inspector jumps to
        // whatever the user just poked. Selection stays reachable from the Hierarchy panel
        // while playing.
        if (!play_.is_playing() && ImGui::IsItemClicked())
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
        script_apply_result_ = script_editor_.apply(script_edit_buf_, world, reflection, e, commands_);
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

    // --- Debugger (tracepoint inspection) ---------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Debugger");
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("line", &script_bp_line_);
    if (script_bp_line_ < 1) { script_bp_line_ = 1; }
    ImGui::SameLine();
    if (ImGui::Button("Add BP")) { script_debugger_.set_breakpoint(script_bp_line_); }
    ImGui::SameLine();
    if (ImGui::Button("Clear BP")) { script_debugger_.clear_breakpoint(script_bp_line_); }
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) { script_debugger_.clear_breakpoints(); }

    {
        std::string bps;
        for (int line : script_debugger_.breakpoints())
        {
            if (!bps.empty()) { bps += ", "; }
            bps += std::to_string(line);
        }
        ImGui::TextDisabled("breakpoints: %s", bps.empty() ? "(none)" : bps.c_str());
    }

    if (ImGui::Button("Debug run"))
    {
        script_debug_result_ = script_editor_.debug(script_edit_buf_, world, reflection, e,
                                                     script_debugger_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(runs top-level + on_update once)");
    if (script_debug_result_.kind == ScriptApplyResult::Kind::Error)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", script_debug_result_.message.c_str());
        ImGui::PopStyleColor();
    }
    for (const mosaic::engine::script::BreakpointHit& hit : script_debugger_.hits())
    {
        if (ImGui::TreeNode(reinterpret_cast<const void*>(&hit), "line %d — %zu locals", hit.line,
                            hit.locals.size()))
        {
            for (const auto& [name, value] : hit.locals)
            {
                ImGui::BulletText("%s = %s", name.c_str(),
                                  mosaic::foundation::serialization::to_json(value).c_str());
            }
            ImGui::TreePop();
        }
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
    bool        changed = false;
    std::string to_remove;

    ImGuiFieldVisitor               visitor(assets_.selected(), assets_.has_selection());
    const std::vector<std::string>  present = reflection.components_on(world, entity);
    for (const std::string& name : present)
    {
        ImGui::PushID(name.c_str());
        if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            reflection.inspect(world, entity, name, visitor);
            if (ImGui::SmallButton("Remove")) { to_remove = name; }
        }
        ImGui::PopID();
    }
    if (!to_remove.empty())
    {
        reflection.remove_value(world, entity, to_remove); // applied after the loop (no mid-render mutate)
        changed = true;
    }

    ImGui::Separator();
    if (ImGui::BeginCombo("+ Add Component", "..."))
    {
        for (const std::string& name : reflection.registered_names())
        {
            if (std::find(present.begin(), present.end(), name) != present.end()) { continue; }
            if (ImGui::Selectable(name.c_str()))
            {
                reflection.add_default(world, entity, name);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    return visitor.changed() || changed;
}

bool StudioApp::draw_controls(mosaic::engine::World& world)
{
    using mosaic::engine::render::Material;
    using mosaic::engine::transform::Transform;

    if (!selection_.has_selection())
    {
        ImGui::TextUnformatted("(no selection)");
        return false;
    }
    const mosaic::foundation::EntityID e = selection_.selected();
    if (!world.has_component<Transform>(e))
    {
        ImGui::TextUnformatted("(selection has no Transform)");
        return false;
    }

    bool       changed = false;
    Transform& t       = *world.get_component<Transform>(e);
    controls_.sync(e, t.rotation);

    if (ImGui::DragFloat3("Position", &t.position.x, 0.05f)) { changed = true; }

    bool rot = false;
    rot |= ImGui::SliderAngle("Pitch (X)", &controls_.euler.pitch);
    rot |= ImGui::SliderAngle("Yaw (Y)", &controls_.euler.yaw);
    rot |= ImGui::SliderAngle("Roll (Z)", &controls_.euler.roll);
    if (rot)
    {
        t.rotation = euler_to_quat(controls_.euler);
        changed    = true;
    }

    if (ImGui::DragFloat3("Scale", &t.scale.x, 0.05f)) { changed = true; }

    if (world.has_component<Material>(e))
    {
        Material& m = *world.get_component<Material>(e);
        if (ImGui::ColorEdit3("Base Color", &m.base_color.x)) { changed = true; }
    }

    return changed;
}

} // namespace mosaic::studio
