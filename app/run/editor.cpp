#include "app/run/editor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include <GLFW/glfw3.h>

#include "app/asset/asset_io.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/scene_io.hpp"
#include "app/run/scene_load.hpp"
#include "app/run/status_reporter.hpp"
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

int run_editor(const Options& options)
{
    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    if (glfwInit() != GLFW_TRUE)
    {
        reporter->error("glfwInit failed");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Mosaic Studio", nullptr, nullptr);
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
        device->init_ui();
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

    mosaic::studio::StudioApp studio(options.scene.value_or("scene.json"));

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
    auto render_system = setup_runtime(world, asset_registry).render_system;

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
        return g_editor_stop.load() || studio.exit_requested()
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
    };

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    device.reset(); // device (and ImGui) destroyed before the window
    glfwDestroyWindow(window);
    glfwTerminate();
    mosaic::foundation::log::clear_sinks(); // drop the console sink before the buffer dies
    return 0;
}

} // namespace mosaic::app
