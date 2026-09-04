#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_host.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using namespace mosaic::app;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::script::ScriptComponent;
using mosaic::engine::script::ScriptHost;
using mosaic::engine::transform::Transform;
using mosaic::engine::transform::TransformSystem;
using mosaic::foundation::reflection::TypeRegistry;
using mosaic::script::python::create_python_script_engine;

namespace
{
class NullReporter : public StatusReporter
{
public:
    void progress(std::uint64_t, double) override {}
    void summary(const RunSummary&) override {}
    void error(const std::string&) override {}
};
struct StepClock { double t=0.0, step=1.0; double operator()(){ double n=t; t+=step; return n; } };
} // namespace

TEST_CASE("a script calling stop() ends a headless run early", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);

    World world;
    world.add_system<TransformSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(
        e, ScriptComponent{"def on_update(dt):\n    stop()\n"});

    bool stop_flag = false;
    ScriptHost host;
    host.request_stop = [&] { stop_flag = true; };

    AssetRegistry registry;
    setup_runtime(world, registry, types,
                  [] { return create_python_script_engine(); }, host);

    RuntimeLoop loop(1.0F / 60.0F, 1);
    Options options; options.ticks = 100; options.fixed_hz = 60.0;
    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [&] { return stop_flag; }};
    NullReporter reporter;

    FramePump pump(loop, deps, reporter, options);
    const RunSummary summary = pump.run(world);

    REQUIRE(stop_flag);                       // the script drove the host callback
    REQUIRE(summary.stopped_by == "signal");  // and it ended the run
    REQUIRE(loop.tick_count() < 100);         // well before the 100-tick bound
}
