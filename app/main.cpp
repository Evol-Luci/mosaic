#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <GLFW/glfw3.h> // windowing only; Vulkan surface creation lives in the backend

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/editor.hpp"
#include "app/run/scene_load.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/ecs/system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/image.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp" // SerializationError
#include "modules/render_vulkan/vulkan_render_device.hpp"

namespace
{
std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }
} // namespace

namespace mosaic::app
{
// Demo-only: spins every entity that has a Transform around Y, so the cube visibly
// rotates and its WorldTransform flows to the screen.
} // namespace mosaic::app

int main(int argc, char** argv)
{
    using namespace mosaic::app;
    namespace engine = mosaic::engine;

    ParseResult pr = parse(argc, argv);
    if (pr.exit_code)
    {
        return *pr.exit_code;
    }

    // Install a stderr log sink for every command. Without it the process-global
    // logger has no sinks and all log output (StatusReporter, Vulkan validation) is
    // dropped. The JSON reporter writes its structured output to stdout separately,
    // so stdout stays pure JSON while human log lines go to stderr. The editor adds
    // its own Console-panel sink on top of this.
    mosaic::foundation::log::add_sink(std::make_shared<mosaic::foundation::log::ConsoleSink>());

    if (pr.options.command == Command::Editor)
    {
        return mosaic::app::run_editor(pr.options);
    }

    const Options& options = pr.options;

    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    engine::World world;
    world.add_system<engine::transform::TransformSystem>();

    mosaic::foundation::reflection::TypeRegistry registry;
    engine::reflection::ComponentReflection types;
    engine::reflection::register_builtin_components(registry, types);

    if (!populate_world(world, options, types))
    {
        return 1; // populate_world already logged the reason
    }

    engine::runtime::RuntimeLoop loop(1.0F / static_cast<float>(options.fixed_hz),
                                      options.ticks ? 1 : 8);
    std::signal(SIGINT, on_sigint);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    PumpDeps deps;
    deps.now = [start] {
        return std::chrono::duration<double>(clock::now() - start).count();
    };
    deps.sleep = [](double seconds) {
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
    };

    GLFWwindow*                                            glfw_window = nullptr;
    std::unique_ptr<mosaic::engine::render::RenderDevice>  render_device;
    // Function-scoped so it outlives pump.run() below: the on_frame lambda captures it by
    // reference and resolves textures from it every frame. (Populated inside the window block.)
    mosaic::engine::assets::AssetRegistry                  asset_registry;

    if (options.window)
    {
        if (glfwInit() != GLFW_TRUE)
        {
            reporter->error("glfwInit failed");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan, no GL context
        glfw_window = glfwCreateWindow(1280, 720, "Mosaic", nullptr, nullptr);
        if (glfw_window == nullptr)
        {
            reporter->error("glfwCreateWindow failed");
            glfwTerminate();
            return 1;
        }
        try
        {
            render_device = mosaic::render::vk::create_vulkan_render_device(glfw_window);
        }
        catch (const std::exception& e)
        {
            reporter->error(std::string("render init failed: ") + e.what());
            glfwDestroyWindow(glfw_window);
            glfwTerminate();
            return 1;
        }

        namespace render = mosaic::engine::render;
        namespace transform = mosaic::engine::transform;
        using mosaic::foundation::math::Vec3;

        auto render_system = setup_runtime(world, asset_registry).render_system;

        auto* device = render_device.get();
        deps.on_frame = [glfw_window, device, &world, render_system, &asset_registry] {
            glfwPollEvents();
            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(glfw_window, &fbw, &fbh);
            if (fbh > 0)
            {
                render_system->set_aspect(static_cast<float>(fbw) / static_cast<float>(fbh));
            }
            device->begin_frame();
            device->clear(render::Color{0.1F, 0.1F, 0.12F, 1.0F});
            render_system->render(world, *device, asset_registry);
            device->end_frame();
        };
        deps.stop_requested = [glfw_window] {
            return g_stop.load() || glfwWindowShouldClose(glfw_window) == GLFW_TRUE;
        };
    }
    else
    {
        deps.stop_requested = [] { return g_stop.load(); };
    }

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);

    if (options.window)
    {
        render_device.reset();          // device destroyed before the window
        glfwDestroyWindow(glfw_window);
        glfwTerminate();
    }
    return 0;
}
