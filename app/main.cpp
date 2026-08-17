#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "app/cli/options.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "foundation/serialization/reference.hpp" // SerializationError

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

    engine::runtime::RuntimeLoop loop(1.0F / static_cast<float>(options.fixed_hz));

    std::signal(SIGINT, on_sigint);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    PumpDeps deps{
        [start] {
            return std::chrono::duration<double>(clock::now() - start).count();
        },
        [](double seconds) {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        },
        [] { return g_stop.load(); }};

    FramePump pump(loop, std::move(deps), *reporter, options);
    RunSummary summary = pump.run(world);
    reporter->summary(summary);
    return 0;
}
