#include "app/run/editor.hpp"

#include <atomic>
#include <chrono>
#include <climits> // PATH_MAX
#include <csignal>
#include <cstdio>  // fprintf
#include <filesystem>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h> // readlink, execl -- Linux-specific relaunch path, see the reopen_path comment below

#include <GLFW/glfw3.h>

#include "app/asset/asset_io.hpp"
#include "app/project/project_io.hpp"
#include "app/run/demo_scene.hpp"
#include "engine/physics/physics_system.hpp"
#include "app/run/windowed_systems.hpp"
#include "engine/ui/ui_system.hpp"
#include "app/run/editor_play_policy.hpp"
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
#include "engine/render/image.hpp"
#include "engine/ui/ui_fit.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_order_check.hpp"
#include "studio/console.hpp"
#include "studio/order_warnings.hpp"
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
    // ONE provider, shared by UiSystem and ClickNudgeSystem. Both must see the same value
    // within a tick or the UI-claim arbitration ends up declining a different click than the
    // one ClickNudgeSystem acts on. Reading ViewportPanel's CACHED ImGui sample makes that
    // impossible by construction; reading glfwGetCursorPos live would not, because it is not
    // frame-snapshotted.
    //
    // All the policy -- panel-relative coordinates, the play-mode gate, hover, ImGui's mouse
    // claim, and the press-began-while-ineligible latch -- lives in ViewportPanel and is
    // unit-tested headlessly. This lambda only asks.
    auto viewport_pointer = [&studio] {
        return studio.viewport().pointer_state(studio.play_state().is_playing());
    };

    auto render_system =
        setup_runtime(world, asset_registry, reflection,
                      [] { return mosaic::script::python::create_python_script_engine(); },
                      /*host=*/{}, /*script_thread=*/false, std::move(audio_backend),
                      viewport_pointer)
            .render_system;

    // The editor always has a window, so `windowed` is unconditionally true. Registered
    // AFTER setup_runtime so ClickNudgeSystem ticks after UiSystem and sees its verdict on
    // the same press -- World ticks systems in registration order.
    //
    // Before this, register_windowed_systems had exactly one caller (main.cpp), so the
    // editor had no ClickNudgeSystem at all.
    {
        mosaic::app::WindowedSystemDeps pointer_deps;
        pointer_deps.pointer = viewport_pointer;
        // The PANEL's aspect, not the window's. The scene renders into a panel-sized
        // offscreen target, so a window-derived aspect makes ClickNudgeSystem's camera ray
        // diverge from the image the user clicked -- picking a different object the further
        // the cursor is from centre, and wrong whenever the viewport is not the whole
        // window, which is always.
        pointer_deps.aspect = [&studio] { return studio.viewport().aspect(); };
        pointer_deps.ui_claimed = [&world]() -> bool {
            auto* ui = world.get_system<mosaic::engine::ui::UiSystem>();
            return ui != nullptr && ui->press_consumed();
        };
        mosaic::app::register_windowed_systems(world, /*windowed=*/true,
                                               std::move(pointer_deps));
    }

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

    // The editor opens in authoring mode (PlayState defaults to stopped). A tick-bounded
    // run is non-interactive, and the tick counter cannot advance while stopped, so such a
    // run would hang -- see editor_play_policy.hpp for the full reasoning.
    if (should_start_playing(options))
    {
        studio.play_state().play();
    }

    engine::runtime::RuntimeLoop loop(static_cast<float>(1.0 / options.fixed_hz));

    // Editor-time only: nothing installs this in `mosaic run`, which is what makes the
    // order-disagreement diagnostic a warning to an AUTHOR rather than noise in a shipped
    // game's log. Lives out here so it survives across frames -- it is what keeps an
    // every-frame check from warning sixty times a second.
    mosaic::studio::OrderWarningModel order_warnings;
    // A second ledger rather than a second model: a pairwise finding has two entities and
    // four numbers, so it builds its own key and signature. Editor-time only, like the
    // per-entity lint -- nothing installs either in `mosaic run`.
    mosaic::studio::WarningLedger     conflict_warnings;
    mosaic::studio::WarningLedger     sub_rect_warnings;

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
                     &loop, &transform_system, &order_warnings, &conflict_warnings,
                     &sub_rect_warnings] {
        glfwPollEvents();

        // Toolbar-driven simulation control: pause = stop, resume = start, step = one tick.
        mosaic::studio::PlayState& play = studio.play_state();
        if (play.is_playing()) { loop.start(); } else { loop.stop(); }
        if (play.consume_step_request()) { loop.step(world); }

        // Refresh derived transforms every frame (even paused) so inspector edits show.
        transform_system.update(world, 0.0f);

        // Reconcile physics with the ECS every frame too, for the same reason and with the
        // same shape: while stopped, `loop.stop()` above means world.tick never runs, so
        // PhysicsSystem::update never runs -- and bodies are created lazily inside it. The
        // editor now STARTS stopped, so a freshly opened editor had no bodies at all:
        // click-to-select raycasts the physics world (studio/pick.hpp), so every viewport
        // click cleared the selection instead of making one, and inspector edits to a
        // RigidBody's Transform never reached the backend.
        //
        // sync_to_world reconciles WITHOUT stepping. Calling update(world, 0.0f) instead
        // would step the backend with dt 0 and then harvest contact events, so a stopped
        // editor would emit contact-begin events for anything already touching.
        //
        // Unconditional rather than gated on !is_playing(): while playing, world.tick runs
        // this same reconcile as its first phase, so the extra call is idempotent -- and a
        // gate here would be one more thing to get wrong at the play/stop boundary.
        if (auto* physics = world.get_system<mosaic::engine::physics::PhysicsSystem>())
        {
            physics->sync_to_world(world);
        }

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
        render_system->render_shadows(world, *device, asset_registry);
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

        // AFTER the reload block, so a scene that loads with a disagreement is reported on
        // the frame it loads rather than one frame later.
        //
        // A button's hit-test `order` and its own panel's draw `order` are independent (see
        // ui_button.hpp), so they can drift apart -- and the symptom, once a second
        // overlapping button exists, is a click landing on something the user cannot see.
        // A heuristic lint rather than a correctness check; find_order_disagreements says
        // why. The model warns on the transition, so this is silent on every frame where
        // nothing changed.
        for (const auto& d : order_warnings.emit(mosaic::engine::ui::find_order_disagreements(world)))
        {
            mosaic::foundation::log::warn(
                "ui",
                "entity {} has UiButton order {} but UiPanel order {} -- the hit region and "
                "the panel a user sees are stacked independently",
                d.e.value(), d.button_order, d.panel_order);
        }

        // The CORRECTNESS check the per-entity lint above stands in for: two overlapping
        // buttons whose click ranking disagrees with their visual ranking. Both run -- the
        // lint catches the authoring slip BEFORE a second button exists, which is precisely
        // when this has nothing to say.
        {
            const auto conflicts = mosaic::engine::ui::find_order_conflicts(world);
            std::vector<mosaic::studio::WarningLedger::Entry> entries;
            entries.reserve(conflicts.size());
            for (const auto& c : conflicts)
            {
                // `a` is already the lower entity value, so the key is stable per pair
                // however the two were reached.
                entries.push_back(mosaic::studio::WarningLedger::Entry{
                    std::to_string(c.a.value()) + "/" + std::to_string(c.b.value()),
                    std::to_string(static_cast<int>(c.kind)) + ":"
                        + std::to_string(c.a_button_order) + ":"
                        + std::to_string(c.a_panel_order) + ":"
                        + std::to_string(c.b_button_order) + ":"
                        + std::to_string(c.b_panel_order)});
            }
            for (const std::size_t i : conflict_warnings.emit(entries))
            {
                const auto& c = conflicts[i];
                if (c.kind == mosaic::engine::ui::OrderConflictKind::Inverted)
                {
                    mosaic::foundation::log::warn(
                        "ui",
                        "entities {} and {} overlap, but the click goes to the one drawn "
                        "underneath (UiButton orders {} and {}; UiPanel orders {} and {})",
                        c.a.value(), c.b.value(), c.a_button_order, c.b_button_order,
                        c.a_panel_order, c.b_panel_order);
                }
                else
                {
                    mosaic::foundation::log::warn(
                        "ui",
                        "entities {} and {} overlap and one of their orders is tied "
                        "(UiButton {} and {}; UiPanel {} and {}) -- which receives the click "
                        "falls to iteration order and can change when an unrelated button is "
                        "destroyed",
                        c.a.value(), c.b.value(), c.a_button_order, c.b_button_order,
                        c.a_panel_order, c.b_panel_order);
                }
            }
        }

        // A UiImage whose sub-rect starts past the far edge of its texture. Without this a
        // mistyped `sub_rect_pos` is silent: resolve_image_quad clamps both halves onto the
        // edge and the sprite becomes a one-texel smear, which looks like a broken asset
        // rather than a wrong number.
        //
        // The predicate is pure and lives in engine/ui/ui_fit.hpp beside the clamp it
        // describes; the texture lookup happens HERE because only this tier has the
        // registry. Deliberately not a warning inside resolve_image_quad itself -- that is a
        // header-only function the render pass calls for every image every frame, with no
        // ledger within reach to de-duplicate it.
        {
            struct OffTexture
            {
                mosaic::foundation::EntityID e;
                mosaic::foundation::math::Vec2 pos;
                std::uint32_t                  tw = 0;
                std::uint32_t                  th = 0;
            };
            std::vector<OffTexture> found;
            world.for_each<mosaic::engine::ui::UiImage>(
                [&](mosaic::foundation::EntityID e, mosaic::engine::ui::UiImage& im)
                {
                    // load() is a cache lookup after the first call, and resolves nothing
                    // for an unset or unknown AssetID -- which is silent by design, since a
                    // missing texture is a different problem with its own diagnostics.
                    const auto img = asset_registry.load<mosaic::engine::render::Image>(im.texture);
                    if (!img)
                    {
                        return;
                    }
                    if (mosaic::engine::ui::sub_rect_off_texture(img->width, img->height,
                                                                 im.sub_rect_pos,
                                                                 im.sub_rect_size))
                    {
                        found.push_back(OffTexture{e, im.sub_rect_pos, img->width, img->height});
                    }
                });

            std::vector<mosaic::studio::WarningLedger::Entry> entries;
            entries.reserve(found.size());
            for (const auto& f : found)
            {
                // The texture size is part of the signature as well as the position: the
                // same authored sub-rect can go from valid to off-texture when the image
                // behind it is replaced by a smaller one, and that is worth saying again.
                entries.push_back(mosaic::studio::WarningLedger::Entry{
                    std::to_string(f.e.value()),
                    std::to_string(f.pos.x) + ":" + std::to_string(f.pos.y) + ":"
                        + std::to_string(f.tw) + ":" + std::to_string(f.th)});
            }
            for (const std::size_t i : sub_rect_warnings.emit(entries))
            {
                const auto& f = found[i];
                mosaic::foundation::log::warn(
                    "ui",
                    "entity {} has a UiImage sub_rect_pos of ({}, {}), outside its {}x{} "
                    "texture -- the sub-rect collapses to the edge and the sprite draws as a "
                    "one-texel smear",
                    f.e.value(), f.pos.x, f.pos.y, f.tw, f.th);
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
