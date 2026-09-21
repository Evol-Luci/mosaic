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
#include <vector>
#include "foundation/core/entity_id.hpp"
#include "studio/demo_physics.hpp"
#include "studio/transform_controls.hpp"
#include "engine/transform/transform.hpp"
#include "engine/render/material.hpp"
#include "studio/live_link_server.hpp"
#include "studio/plugin_file_watcher.hpp"
#include "studio/plugin_host.hpp"

namespace mosaic::studio
{

inline const char* live_link_label(LiveLinkState s)
{
    switch (s)
    {
    case LiveLinkState::Off:       return "Live Link: Off";
    case LiveLinkState::Listening: return "Live Link: Listening";
    case LiveLinkState::Connected: return "Live Link: Connected";
    }
    return "Live Link";
}

// Draws the editor shell each frame: a full-window dockspace, a menu bar (File > Exit),
// the scene Hierarchy (entities from the World, click to select), the Inspector (shows
// the current selection), and empty placeholder panels. ImGui glue only; the caller
// wraps draw_ui() between the device's begin_ui_frame()/end_ui_frame().
class StudioApp
{
public:
    // `plugins_dir` defaults to the bare relative name "plugins" (resolved against
    // whatever the process's current working directory happens to be) so every
    // existing call site keeps compiling and behaving unchanged. A real project
    // session should pass the project-rooted path instead (see app/run/editor.cpp) --
    // CWD-relative resolution silently breaks for any launch whose CWD isn't the
    // project root (a desktop shortcut, a packaged distribution's launcher, `mosaic
    // editor` invoked from elsewhere with a project-dir argument).
    // `disabled_plugin_files` names *.py files (bare filenames) PluginHost should skip on
    // every load -- the caller (app/run/editor.cpp) derives this from the project's
    // ProjectManifest.installed_plugins' `enabled` flags, since Studio itself doesn't know
    // about app/'s ProjectManifest (Studio sits below app/ in the dependency direction:
    // app/run/editor.cpp constructs and drives StudioApp, not the reverse). Fixed at
    // construction: a plugin enabled/disabled via the CLI/MCP while this editor session is
    // already open won't be picked up by "Reload Plugins" until the editor restarts --
    // an accepted limitation, not a bug, matching this project's precedent of naming
    // known follow-ups rather than silently shipping around them.
    // `initial_theme` is the theme already applied to the ImGui context via
    // RenderDevice::init_ui() before this constructor runs (see app/run/editor.cpp) --
    // stored here purely so the Theme menu can show which one is active and
    // current_theme() can report it back for persistence at exit. Constructing a
    // StudioApp never itself calls set_ui_theme(); the caller is expected to have
    // already applied it via init_ui().
    // `recent_projects` and `show_recent_projects_picker` back the startup "Recent
    // Projects" window: the caller (app/run/editor.cpp) decides whether to show it
    // (via app/studio_prefs.hpp's should_show_recent_projects_picker(), since that
    // decision needs Options::open_path -- an app-level type Studio can't reference)
    // and passes the already-persisted list to display. Selecting an entry sets
    // reopen_requested() rather than switching projects in-process (StudioApp has no
    // notion of "reload with an entirely different project's assets/plugins/scene" --
    // that would need its own design); the caller relaunches the editor process.
    explicit StudioApp(std::string scene_path = "scene.json",
                       ScriptConsoleModel::EngineFactory script_factory = {},
                       PluginHost::EngineFactory plugin_factory = {},
                       std::string plugins_dir = "plugins",
                       std::vector<std::string> disabled_plugin_files = {},
                       mosaic::engine::render::UiTheme initial_theme =
                           mosaic::engine::render::UiTheme::Dark,
                       std::vector<std::string> recent_projects = {},
                       bool show_recent_projects_picker = false)
        : scene_file_(std::move(scene_path)),
          script_console_(script_factory),
          script_editor_(script_factory),
          plugin_host_(plugin_factory),
          plugins_dir_(std::move(plugins_dir)),
          disabled_plugin_files_(std::move(disabled_plugin_files)),
          current_theme_(initial_theme),
          recent_projects_(std::move(recent_projects)),
          show_recent_projects_picker_(show_recent_projects_picker)
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
    // The theme most recently selected via the Theme menu (or initial_theme if
    // never changed this session) -- read by the caller right before exit to
    // persist alongside window size, mirroring the existing glfwGetWindowSize
    // read-back-at-exit pattern.
    mosaic::engine::render::UiTheme current_theme() const { return current_theme_; }
    // Set once the user clicks an entry in the "Recent Projects" startup picker;
    // the caller (app/run/editor.cpp) checks this each frame and, once set, tears
    // down this session and relaunches the editor process pointed at the chosen
    // project directory.
    std::optional<std::string> reopen_requested() const { return reopen_requested_; }

    // Called by the editor loop right after a File > Reload has replaced the live
    // World's contents from disk: drops every Studio-side cache that names an
    // entity from the now-gone previous scene (undo history, selection, the demo-
    // physics toggle target, the Script Editor's current target) -- all now
    // meaningless or outright dangling, since reload doesn't preserve entity ids.
    void on_scene_reloaded()
    {
        commands_.clear();
        selection_.clear();
        demo_entity_.reset();
        demo_script_.clear();
        demo_physics_ = false;
        script_edit_entity_.reset();
    }

private:
    // Runs plugin_host_.load() and refreshes the layout's registered panels --
    // shared by the initial load, the manual "Reload Plugins" menu item, and hot-
    // reload's automatic trigger, so all three stay in sync by construction.
    void reload_plugins(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);

    void draw_hierarchy(mosaic::engine::World& world);
    bool draw_inspector(mosaic::engine::World& world,
                        const mosaic::engine::reflection::ComponentReflection& reflection);
    bool draw_controls(mosaic::engine::World& world);
    void draw_viewport(mosaic::engine::render::RenderDevice& device, mosaic::engine::World& world);
    void draw_assets(const mosaic::engine::assets::AssetRegistry& registry);
    void draw_console();
    void draw_script_console(mosaic::engine::World& world,
                             const mosaic::engine::reflection::ComponentReflection& reflection);
    void draw_script_editor(mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection);

    StudioLayout                   layout_;
    SelectionModel                 selection_;
    TransformControlsModel         controls_;
    ViewportPanel                  viewport_;
    PlayState                      play_;
    AssetBrowserModel              assets_;
    SceneFileModel                 scene_file_;
    bool                           want_reload_confirm_ = false; // set by the File menu, consumed
                                                                  // right before BeginPopupModal
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
    mosaic::engine::script::ScriptDebugger      script_debugger_;      // breakpoints persist across frames
    int                                         script_bp_line_ = 1;   // breakpoint line input
    ScriptApplyResult                           script_debug_result_;  // outcome of the last Debug run
    LiveLinkServer                              live_link_;            // Blender Live-Link socket
    PluginHost               plugin_host_;       // project-local *.py plugins
    std::string              plugins_dir_;           // where plugin_host_ loads from -- see constructor comment
    std::vector<std::string> disabled_plugin_files_; // filenames plugin_host_.load() should skip
    bool                     plugins_loaded_ = false; // true after the first load() this session
    PluginFileWatcher        plugin_watcher_; // polled once per frame; a change triggers reload_plugins()
    mosaic::engine::render::UiTheme current_theme_; // see current_theme()/the Theme menu in draw_ui
    std::vector<std::string>   recent_projects_;             // shown by the startup picker
    bool                       show_recent_projects_picker_; // drawn once at startup; dismissible
    std::optional<std::string> reopen_requested_;            // see reopen_requested()
};

} // namespace mosaic::studio
