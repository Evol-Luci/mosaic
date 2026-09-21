#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <GLFW/glfw3.h> // windowing only; Vulkan surface creation lives in the backend

#include "app/asset/asset_io.hpp"
#include "app/cli/options.hpp"
#include "app/project/project_io.hpp"
#include "app/run/build.hpp"
#include "app/run/import.hpp"
#include "app/run/mcp.hpp"
#include "app/run/plugin.hpp"
#include "app/run/create.hpp"
#include "app/run/docs.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/editor.hpp"
#include "app/run/net_host_demo.hpp"
#include "app/run/scene_load.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_metadata.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/ecs/system.hpp"
#include "engine/net/net_replicated.hpp"
#include "engine/net/transport.hpp"
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
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/script/script_system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/math/vec3.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/render_vulkan/vulkan_render_device.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "engine/audio/audio_backend.hpp"
#include "modules/audio_miniaudio/miniaudio_backend.hpp"
#include "modules/net_enet/enet_transport.hpp"

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

    if (pr.options.command == Command::Create)
    {
        return run_create(pr.options);
    }

    if (pr.options.command == Command::Build)
    {
        return run_build(pr.options);
    }

    if (pr.options.command == Command::Docs)
    {
        return run_docs(pr.options);
    }

    if (pr.options.command == Command::Import)
    {
        return run_import(pr.options);
    }

    if (pr.options.command == Command::PluginList)
    {
        return run_plugin_list(pr.options);
    }

    if (pr.options.command == Command::PluginInstall)
    {
        return run_plugin_install(pr.options);
    }

    if (pr.options.command == Command::PluginRemove)
    {
        return run_plugin_remove(pr.options);
    }

    if (pr.options.command == Command::PluginEnable)
    {
        return run_plugin_enable(pr.options);
    }

    if (pr.options.command == Command::PluginDisable)
    {
        return run_plugin_disable(pr.options);
    }

    if (pr.options.command == Command::PluginUpdates)
    {
        return run_plugin_updates(pr.options);
    }

    if (pr.options.command == Command::PluginSourceAdd)
    {
        return run_plugin_source_add(pr.options);
    }

    if (pr.options.command == Command::PluginSourceList)
    {
        return run_plugin_source_list(pr.options);
    }

    if (pr.options.command == Command::PluginSourceRemove)
    {
        return run_plugin_source_remove(pr.options);
    }

    if (pr.options.command == Command::PluginSourcePull)
    {
        return run_plugin_source_pull(pr.options);
    }

    if (pr.options.command == Command::Mcp)
    {
        return run_mcp(pr.options);
    }

    // Smart positional: resolve a project dir / scene file into the scene to load, so run and
    // editor both feed T2's populate_world through options.scene.
    if (pr.options.open_path)
    {
        const std::optional<std::string> resolved =
            resolve_startup_scene(*pr.options.open_path);
        if (!resolved)
        {
            mosaic::foundation::log::error(
                "project", "not a Mosaic project or scene file: {}", *pr.options.open_path);
            return 1;
        }
        pr.options.scene = *resolved;
        std::error_code aec;
        if (std::filesystem::is_directory(*pr.options.open_path, aec))
        {
            pr.options.assets_dir =
                (std::filesystem::path(*pr.options.open_path) / "assets").string();
        }
        pr.options.plugins_dir = resolve_plugins_dir(*pr.options.open_path);
    }

    if (pr.options.command == Command::Editor)
    {
        // Real audio device for the editor too -- it's always an interactive window,
        // the same condition `mosaic run` uses to decide whether to construct one.
        return mosaic::app::run_editor(pr.options, mosaic::audio::miniaudio::create_miniaudio_backend());
    }

    const Options& options = pr.options;

    std::unique_ptr<StatusReporter> reporter =
        options.json ? std::unique_ptr<StatusReporter>(std::make_unique<JsonStatusReporter>())
                     : std::unique_ptr<StatusReporter>(std::make_unique<TextStatusReporter>());

    mosaic::engine::net::Role net_role = mosaic::engine::net::Role::Client;
    std::shared_ptr<mosaic::engine::net::Transport> net_transport;
    if (options.host_port)
    {
        net_role = mosaic::engine::net::Role::Server;
        net_transport = mosaic::net::enet::create_enet_server(*options.host_port, /*max_peers=*/8);
        if (net_transport == nullptr)
        {
            reporter->error("failed to host on port " + std::to_string(*options.host_port));
            return 1;
        }
    }
    else if (options.connect_address)
    {
        const auto [connect_host, connect_port] = split_host_port(*options.connect_address);
        net_transport = mosaic::net::enet::create_enet_client(connect_host, connect_port);
        if (net_transport == nullptr)
        {
            reporter->error("failed to connect to " + *options.connect_address);
            return 1;
        }
    }

    engine::World world;
    world.add_system<engine::transform::TransformSystem>();

    mosaic::foundation::reflection::TypeRegistry registry;
    engine::reflection::ComponentReflection types;
    engine::reflection::register_builtin_components(registry, types);

    if (!options.connect_address)
    {
        if (!populate_world(world, options, types))
        {
            reporter->error("failed to load scene: " + options.scene.value_or(std::string{}));
            return 1;
        }
    }
    else if (options.scene || options.open_path)
    {
        mosaic::foundation::log::info(
            "net", "--connect starts from an empty world; ignoring the given scene/path");
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

    namespace render = mosaic::engine::render;
    namespace transform = mosaic::engine::transform;
    using mosaic::foundation::math::Vec3;

    mosaic::engine::script::ScriptHost script_host;
    script_host.request_stop = [] { g_stop.store(true); };
    script_host.set_playing  = [](bool playing) { if (!playing) { g_stop.store(true); } };

    // Real audio device only for the interactive windowed run; headless runs stay silent.
    std::unique_ptr<mosaic::engine::audio::AudioBackend> audio =
        options.window ? mosaic::audio::miniaudio::create_miniaudio_backend() : nullptr;
    mosaic::engine::audio::AudioBackend* audio_raw = audio.get();

    std::function<std::vector<std::string>()> net_input_provider;
    if (net_role == mosaic::engine::net::Role::Client && net_transport != nullptr)
    {
        net_input_provider = [&glfw_window]() -> std::vector<std::string>
        {
            if (glfw_window == nullptr) { return {}; }
            float dx = 0.0f, dz = 0.0f;
            constexpr float speed = 0.05f;
            if (glfwGetKey(glfw_window, GLFW_KEY_D) == GLFW_PRESS) { dx += speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_A) == GLFW_PRESS) { dx -= speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_S) == GLFW_PRESS) { dz += speed; }
            if (glfwGetKey(glfw_window, GLFW_KEY_W) == GLFW_PRESS) { dz -= speed; }
            if (dx == 0.0f && dz == 0.0f) { return {}; }
            return {std::to_string(dx) + "," + std::to_string(dz)};
        };
    }

    auto render_system =
        setup_runtime(world, asset_registry, types,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      script_host, options.script_thread, std::move(audio),
                      [&glfw_window]() -> mosaic::engine::ui::PointerState
                      {
                          if (glfw_window == nullptr) { return {}; }
                          int ww = 0, wh = 0;
                          glfwGetWindowSize(glfw_window, &ww, &wh);
                          if (ww <= 0 || wh <= 0) { return {}; }
                          double mx = 0.0, my = 0.0;
                          glfwGetCursorPos(glfw_window, &mx, &my);
                          mosaic::engine::ui::PointerState p;
                          p.x = std::clamp(static_cast<float>(mx / ww), 0.0f, 1.0f);
                          p.y = std::clamp(static_cast<float>(my / wh), 0.0f, 1.0f);
                          p.down = glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                          return p;
                      },
                      net_role, net_transport, net_input_provider)
            .render_system;

    if (net_role == mosaic::engine::net::Role::Server && net_transport != nullptr)
    {
        const auto host_entity = world.create_entity();
        world.add_component<mosaic::engine::net::NetReplicated>(
            host_entity, mosaic::engine::net::NetReplicated{0});
        mosaic::engine::transform::Transform ht;
        ht.position = {0.0f, 3.0f, 0.0f};
        world.add_component<mosaic::engine::transform::Transform>(host_entity, ht);
        mosaic::engine::render::Material hm;
        hm.base_color = {0.9f, 0.7f, 0.2f};
        world.add_component<mosaic::engine::render::Material>(host_entity, hm);
        world.add_component<mosaic::engine::script::ScriptComponent>(
            host_entity, mosaic::engine::script::ScriptComponent{mosaic::app::kOrbitScript});
        mosaic::app::wire_networked_demo_host(world);
    }

    if (options.assets_dir)
    {
        const int n = load_project_assets(asset_registry, *options.assets_dir);
        if (n > 0) { mosaic::foundation::log::info("import", "loaded {} project asset(s)", n); }
        if (audio_raw != nullptr)
        {
            const int a = mosaic::app::register_audio_clips(*audio_raw, asset_registry);
            if (a > 0) { mosaic::foundation::log::info("audio", "registered {} audio clip(s)", a); }
        }
    }

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

    if (options.script_profile)
    {
        if (auto* scripts = world.get_system<engine::script::ScriptSystem>())
        {
            auto rows = scripts->profile();
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) { return a.total_ms > b.total_ms; });
            mosaic::foundation::log::info("profile", "script on_update timing ({} entities):",
                                          rows.size());
            for (const auto& r : rows)
            {
                mosaic::foundation::log::info(
                    "profile", "  entity {}: {} calls, total {:.3f} ms, max {:.3f} ms", r.entity,
                    r.calls, r.total_ms, r.max_ms);
            }
        }
    }

    if (options.window)
    {
        render_device.reset();          // device destroyed before the window
        glfwDestroyWindow(glfw_window);
        glfwTerminate();
    }
    return 0;
}
