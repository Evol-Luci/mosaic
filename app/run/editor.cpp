#include "app/run/editor.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include <GLFW/glfw3.h>

#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/material.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "studio/studio_app.hpp"

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

    mosaic::studio::StudioApp studio;

    mosaic::foundation::reflection::TypeRegistry     registry;
    mosaic::engine::reflection::ComponentReflection  reflection;
    mosaic::engine::reflection::register_builtin_components(registry, reflection);

    engine::World world; // idle (no systems); varied components so the inspector shows several types
    {
        const auto e0 = world.create_entity();
        world.add_component<engine::transform::Transform>(e0, engine::transform::Transform{});
        world.add_component<engine::render::Camera>(e0, engine::render::Camera{});

        const auto e1 = world.create_entity();
        world.add_component<engine::transform::Transform>(e1, engine::transform::Transform{});
        world.add_component<engine::render::Material>(e1, engine::render::Material{});

        const auto e2 = world.create_entity();
        world.add_component<engine::transform::Transform>(e2, engine::transform::Transform{});
        world.add_component<engine::render::PointLight>(e2, engine::render::PointLight{});

        const auto e3 = world.create_entity();
        world.add_component<engine::transform::Transform>(e3, engine::transform::Transform{});
    }
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
    deps.on_frame = [window, &device, &studio, &world, &reflection] {
        glfwPollEvents();
        device->begin_frame();
        device->clear(engine::render::Color{0.10f, 0.10f, 0.12f, 1.0f});
        device->begin_ui_frame();
        studio.draw_ui(world, reflection);
        device->end_ui_frame();
        device->end_frame();
    };

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    device.reset(); // device (and ImGui) destroyed before the window
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace mosaic::app
