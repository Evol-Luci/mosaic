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
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/ecs/system.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/cube.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/render_device.hpp"
#include "engine/render/render_system.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
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
class SpinSystem : public mosaic::engine::ecs::System
{
public:
    void update(mosaic::engine::World& world, float dt) override
    {
        namespace m = mosaic::foundation::math;
        angle_ += dt * 0.9F;
        const float      half = angle_ * 0.5F;
        const m::Quat    spin{0.0F, std::sin(half), 0.0F, std::cos(half)};
        world.for_each<mosaic::engine::transform::Transform>(
            [&](mosaic::foundation::EntityID e, mosaic::engine::transform::Transform& t)
            {
                if (!world.has_component<mosaic::engine::render::MeshRenderer>(e))
                {
                    return; // only spin meshes (the cube), not the camera
                }
                t.rotation = spin;
            });
    }
private:
    float angle_ = 0.0F;
};
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

        namespace render = mosaic::engine::render;
        namespace transform = mosaic::engine::transform;
        using mosaic::foundation::math::Vec3;

        // Upload the cube and spawn one entity that carries it.
        // Demo-only: spin the cube. Registered here (not globally) so a headless
        // scene run never mutates authored rotations. Runs after TransformSystem,
        // so the spin lands one tick later — imperceptible for a continuous spin.
        world.add_system<SpinSystem>();

        std::vector<render::Vertex>        cube_v;
        std::vector<std::uint32_t>         cube_i;
        render::make_cube_mesh(cube_v, cube_i);
        const render::MeshHandle cube = render_device->create_mesh(cube_v, cube_i);

        const auto cube_entity = world.create_entity();
        world.add_component<transform::Transform>(cube_entity, transform::Transform{});
        world.add_component<transform::WorldTransform>(cube_entity,
            transform::WorldTransform{mosaic::foundation::math::Mat4::identity()});
        world.add_component<render::MeshRenderer>(cube_entity, render::MeshRenderer{cube});
        world.add_component<render::Material>(cube_entity, render::Material{Vec3{0.2F, 0.5F, 0.9F}});

        const auto light_entity = world.create_entity();
        world.add_component<render::DirectionalLight>(light_entity,
            render::DirectionalLight{Vec3{-0.5F, -1.0F, -0.3F}, Vec3{1.0F, 1.0F, 1.0F}});

        // Camera entity: placed at (0,0,4) looking down -Z at the cube at the origin.
        // Its Transform → WorldTransform (via TransformSystem) drives the view; the
        // Camera component is the lens. Replaces the RenderSystem's fallback camera.
        const auto camera_entity = world.create_entity();
        world.add_component<transform::Transform>(camera_entity,
            transform::Transform{Vec3{0.0F, 0.0F, 4.0F}, mosaic::foundation::math::Quat::identity(),
                                 Vec3{1.0F, 1.0F, 1.0F}, {}});
        world.add_component<transform::WorldTransform>(camera_entity,
            transform::WorldTransform{mosaic::foundation::math::Mat4::translation(Vec3{0.0F, 0.0F, 4.0F})});
        world.add_component<render::Camera>(camera_entity, render::Camera{});

        auto render_system = std::make_shared<render::RenderSystem>();

        mosaic::engine::assets::AssetRegistry asset_registry;

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
