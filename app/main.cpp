#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <GLFW/glfw3.h> // windowing only; Vulkan surface creation lives in the backend

#include "app/cli/options.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/render_device.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp" // SerializationError
#include "modules/render_vulkan/vulkan_render_device.hpp"

namespace
{
std::atomic<bool> g_stop{false};
void on_sigint(int) { g_stop.store(true); }
} // namespace

int main(int argc, char** argv)
{
    using namespace mosaic::app;
    namespace engine = mosaic::engine;

    ParseResult pr = parse(argc, argv);
    if (pr.exit_code)
    {
        return *pr.exit_code;
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

    if (options.scene)
    {
        std::ifstream file(*options.scene);
        if (!file)
        {
            reporter->error("scene not found: " + *options.scene);
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        try
        {
            engine::scene::load_from_json(buffer.str(), world, types);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            reporter->error(std::string("failed to load scene: ") + e.what());
            return 1;
        }
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

        auto* device = render_device.get();
        deps.on_frame = [glfw_window, device] {
            glfwPollEvents();
            device->begin_frame();
            device->clear(mosaic::engine::render::Color{0.1F, 0.1F, 0.12F, 1.0F});
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
