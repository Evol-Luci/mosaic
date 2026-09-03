#pragma once

#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/render_device.hpp"
#include "engine/world/world.hpp"
#include "studio/asset_browser.hpp"
#include "studio/console.hpp"
#include "studio/play_state.hpp"
#include "foundation/commands/command_stack.hpp"
#include "studio/script_console.hpp"
#include "studio/script_editor.hpp"
#include "studio/scene_file.hpp"
#include "studio/selection.hpp"
#include "studio/studio_layout.hpp"
#include "studio/viewport.hpp"
#include <optional>
#include <string>
#include "foundation/core/entity_id.hpp"
#include "studio/demo_physics.hpp"

namespace mosaic::studio
{

// Draws the editor shell each frame: a full-window dockspace, a menu bar (File > Exit),
// the scene Hierarchy (entities from the World, click to select), the Inspector (shows
// the current selection), and empty placeholder panels. ImGui glue only; the caller
// wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {})
        : scene_file_(std::move(scene_path)),
          script_console_(script_factory),
          script_editor_(script_factory)
    {
    }

    void draw_ui(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world,
                 const mosaic::engine::reflection::ComponentReflection& reflection,
                 const mosaic::engine::assets::AssetRegistry& registry);

    StudioLayout&                  layout() { return layout_; }
    SelectionModel&                selection() { return selection_; }
    ViewportPanel&                 viewport() { return viewport_; }
    PlayState&                     play_state() { return play_; }
    AssetBrowserModel&             assets() { return assets_; }
    SceneFileModel&                scene_file() { return scene_file_; }
    std::shared_ptr<ConsoleBuffer> console() { return console_; }
    bool                           exit_requested() const { return layout_.exit_requested(); }

private:
    void draw_hierarchy(const mosaic::engine::World& world);
    bool draw_inspector(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);
    void draw_viewport(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world);
    void draw_assets(const mosaic::engine::assets::AssetRegistry& registry);
    void draw_console();
    void draw_script_console(mosaic::engine::World& world,
                             const mosaic::engine::reflection::ComponentReflection& reflection);
    void draw_script_editor(mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection);

    StudioLayout                   layout_;
    SelectionModel                 selection_;
    ViewportPanel                  viewport_;
    PlayState                      play_;
    AssetBrowserModel              assets_;
    SceneFileModel                 scene_file_;
    std::shared_ptr<ConsoleBuffer> console_ = std::make_shared<ConsoleBuffer>();
    mosaic::foundation::commands::CommandStack commands_;
    ScriptConsoleModel                         script_console_;
    char                                       script_input_[8192] = {};
    std::optional<mosaic::foundation::EntityID> demo_entity_; // the scripted demo object, cached
    std::string                                 demo_script_; // its source (to restore on toggle-off)
    bool                                        demo_physics_ = false;
    ScriptEditorModel                           script_editor_;
    char                                        script_edit_buf_[8192] = {};
    std::optional<mosaic::foundation::EntityID> script_edit_entity_;
    ScriptApplyResult                           script_apply_result_;
};

} // namespace mosaic::studio
