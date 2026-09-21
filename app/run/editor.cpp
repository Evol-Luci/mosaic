#include "app/run/editor.hpp"

#include <atomic>
#include <chrono>
#include <climits> // PATH_MAX
#include <csignal>
#include <cstdio>  // fprintf
#include <filesystem>
#include <memory>
#include <thread>

#include <unistd.h> // readlink, execl -- Linux-specific relaunch path, see the reopen_path comment below

#include <GLFW/glfw3.h>

#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/scene_load.hpp"
#include "app/run/status_reporter.hpp"
#include "app/studio_prefs.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/scene/scene.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/material.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "modules/script_python_studio/studio_plugin_script_engine.hpp"
#include "studio/console.hpp"
#include "studio/studio_app.hpp"
#include "studio/viewport.hpp"

namespace mosaic::app
{
namespace
{
std::atomic<bool> g_editor_stop{false};
void on_editor_sigint(int) { g_editor_stop.store(true); }
} // namespace

int run_editor(const Options& options,
               std::unique_ptr<mosaic::engine::audio::AudioBackend> audio_backend)
{
    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    if (glfwInit() != GLFW_TRUE)
    {
        reporter->error("glfwInit failed");
        return 1;
    }
    StudioPrefs prefs = load_studio_prefs();
    // Track a real project directory the moment it's confirmed openable (main.cpp
    // already resolved options.scene from it, so this is a real project, not a bare
    // scene file -- only project directories go in the recent list, since that's
    // what the picker below reopens). Recorded/saved before StudioApp exists so a
    // crash later in startup still leaves the list updated.
    if (options.open_path)
    {
        std::error_code dir_ec;
        if (std::filesystem::is_directory(*options.open_path, dir_ec))
        {
            std::error_code canon_ec;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(*options.open_path, canon_ec);
            record_recent_project(prefs,
                                  canon_ec ? *options.open_path : canonical.string());
            save_studio_prefs(prefs);
        }
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(prefs.window_width, prefs.window_height, "Mosaic Studio", nullptr, nullptr);
    if (window == nullptr)
    {
        reporter->error("glfwCreateWindow failed");
        glfwTerminate();
        return 1;
    }

    std::unique_ptr<engine::render::RenderDevice> device;
    try
    {
        device = mosaic::render::vk::create_vulkan_render_device(window);
    }
    catch (const std::exception& e)
    {
        reporter->error(std::string("render init failed: ") + e.what());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    try
    {
        device->init_ui(studio_layout_ini_path().string(), studio_theme_to_ui_theme(prefs.theme));
    }
    catch (const std::exception& e)
    {
        reporter->error(std::string("editor UI init failed: ") + e.what());
        device.reset(); // ImGui/Vulkan torn down before the window
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    std::signal(SIGINT, on_editor_sigint);
    g_editor_stop.store(false);

    // NOTE: the Script Console's persistent engine, and every plugin's persistent engine
    // (StudioApp's plugin_host_ holds one per loaded plugin), bind raw pointers to
    // `world`/`reflection` (below). Those outlive `studio` is NOT guaranteed — they are declared
    // after `studio` and so destroyed first — but this is safe because neither
    // PythonScriptEngine's nor StudioPluginScriptEngine's destructor ever dereferences them (they
    // only drop their py::dict; the interpreter is never finalized). Keep it that way: an engine
    // dtor that touches the bound world/reflection would become a use-after-free here.
    mosaic::studio::StudioApp studio(
        options.scene.value_or("scene.json"),
        [] { return mosaic::script::python::create_python_script_engine(); },
        [] { return mosaic::script::python_studio::create_studio_plugin_script_engine(); },
        options.plugins_dir.value_or("plugins"),
        mosaic::app::resolve_disabled_plugin_files(options.open_path.value_or("")),
        studio_theme_to_ui_theme(prefs.theme),
        prefs.recent_projects,
        should_show_recent_projects_picker(options.open_path.has_value(), prefs.recent_projects));

    // Route all logging (StatusReporter, Vulkan validation, ...) into the Console panel.
    mosaic::foundation::log::add_sink(
        std::make_shared<mosaic::studio::ConsoleSink>(studio.console()));

    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  reflection;
    mosaic::engine::reflection::register_builtin_components(registry, reflection);

    engine::assets::AssetRegistry asset_registry;
    engine::World                 world;
    if (!populate_world(world, options, reflection))
    {
        reporter->error("failed to load scene");
        device.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        mosaic::foundation::log::clear_sinks();
        return 1;
    }
    auto render_system =
        setup_runtime(world, asset_registry, reflection,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      /*host=*/{}, /*script_thread=*/false, std::move(audio_backend))
            .render_system;

    if (options.assets_dir)
    {
        const int n = load_project_assets(asset_registry, *options.assets_dir);
        if (n > 0) { mosaic::foundation::log::info("import", "loaded {} project asset(s)", n); }
    }

    // Transform propagation (Transform -> WorldTransform) is *derived state*, not
    // simulation: run it every frame regardless of play/pause so inspector edits appear
    // in the viewport even while paused. Only gameplay systems (e.g. the PhysicsSystem
    // added by setup_runtime) are gated behind the RuntimeLoop, so pausing freezes
    // simulation but still reflects edits. It is deliberately NOT added via world.add_system().
    engine::transform::TransformSystem transform_system;

    engine::runtime::RuntimeLoop loop(static_cast<float>(1.0 / options.fixed_hz));

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    PumpDeps deps;
    deps.now = [start] {
        return std::chrono::duration<double>(clock::now() - start).count();
    };
    deps.sleep = [](double seconds) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    };
    deps.stop_requested = [window, &studio] {
        return g_editor_stop.load() || studio.exit_requested() || studio.reopen_requested()
               || glfwWindowShouldClose(window) == GLFW_TRUE;
    };
    deps.on_frame = [window, &device, &studio, &world, &reflection, render_system, &asset_registry,
                     &loop, &transform_system] {
        glfwPollEvents();

        // Toolbar-driven simulation control: pause = stop, resume = start, step = one tick.
        mosaic::studio::PlayState& play = studio.play_state();
        if (play.is_playing()) { loop.start(); } else { loop.stop(); }
        if (play.consume_step_request()) { loop.step(world); }

        // Refresh derived transforms every frame (even paused) so inspector edits show.
        transform_system.update(world, 0.0f);

        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});

        // Pass 1: scene -> panel-sized offscreen target.
        mosaic::studio::ViewportPanel& vp = studio.viewport();
        if (vp.needs_resize())
        {
            device->set_viewport_size(vp.size().width, vp.size().height);
            vp.mark_applied();
        }
        render_system->set_aspect(vp.aspect());
        mosaic::studio::render_scene_to_viewport(
            *device, vp, engine::render::Color{0.05f, 0.05f, 0.07f, 1.0f},
            [&](engine::render::RenderDevice& d) { render_system->render(world, d, asset_registry); });

        // Pass 2: ImGui UI (samples the offscreen target in the Viewport panel).
        device->begin_ui_frame();
        studio.draw_ui(*device, world, reflection, asset_registry);
        device->end_ui_frame();
        device->end_frame();

        if (studio.scene_file().consume_save_request())
        {
            const std::string json = mosaic::engine::scene::save_to_json(world, reflection);
            if (mosaic::app::save_scene_file(studio.scene_file().path(), json))
            {
                studio.scene_file().mark_clean();
                mosaic::foundation::log::info("scene", "saved {}", studio.scene_file().path());
            }
            else
            {
                mosaic::foundation::log::error("scene", "save failed: {}",
                                               studio.scene_file().path());
            }
        }

        if (studio.scene_file().consume_reload_request())
        {
            if (mosaic::app::reload_scene_from_disk(world, studio.scene_file().path(), reflection))
            {
                studio.scene_file().mark_clean();
                studio.on_scene_reloaded();
                mosaic::foundation::log::info("scene", "reloaded {}", studio.scene_file().path());
            }
            else
            {
                mosaic::foundation::log::error("scene", "reload failed: {}",
                                               studio.scene_file().path());
            }
        }
    };

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    const std::optional<std::string> reopen_path = studio.reopen_requested();

    // Window size persists across sessions; panel layout is ImGui's own concern
    // (already writing to studio_layout_ini_path(), passed to init_ui() above).
    glfwGetWindowSize(window, &prefs.window_width, &prefs.window_height);
    prefs.theme = ui_theme_to_studio_theme(studio.current_theme());
    save_studio_prefs(prefs);

    device.reset(); // device (and ImGui) destroyed before the window
    glfwDestroyWindow(window);
    glfwTerminate();

    // A "Recent Projects" picker selection: relaunch the editor process pointed at
    // the chosen project rather than switching projects in-process (StudioApp has
    // no notion of reloading with a different project's assets/plugins/scene set --
    // that's its own future design). /proc/self/exe is Linux-specific; a portable
    // equivalent (argv[0] search, or a platform API) is a follow-up if this ever
    // needs to run on Windows/macOS.
    if (reopen_path)
    {
        char    exe_path[PATH_MAX];
        ssize_t len = ::readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0)
        {
            exe_path[len] = '\0';
            mosaic::foundation::log::clear_sinks();
            ::execl(exe_path, exe_path, "editor", reopen_path->c_str(), static_cast<char*>(nullptr));
            // execl only returns on failure -- the log sink is already gone, so this
            // goes straight to stderr.
            std::fprintf(stderr, "mosaic: failed to relaunch editor for %s\n", reopen_path->c_str());
            return 1;
        }
        mosaic::foundation::log::error("project", "could not resolve the running executable's "
                                                  "path to relaunch for {}", *reopen_path);
    }

    mosaic::foundation::log::clear_sinks(); // drop the console sink before the buffer dies
    return 0;
}

} // namespace mosaic::app
