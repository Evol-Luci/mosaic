#include <catch2/catch_test_macros.hpp>

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/frame_pump.hpp"
#include "app/run/status_reporter.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/reflection/builtin_components.hpp"
#include "engine/runtime/runtime_loop.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/reflection/type_registry.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::app::FramePump;
using mosaic::app::Options;
using mosaic::app::PumpDeps;
using mosaic::app::RunSummary;
using mosaic::app::setup_runtime;
using mosaic::app::StatusReporter;
using mosaic::engine::World;
using mosaic::engine::assets::AssetRegistry;
using mosaic::engine::reflection::ComponentReflection;
using mosaic::engine::reflection::register_builtin_components;
using mosaic::engine::runtime::RuntimeLoop;
using mosaic::engine::script::ScriptComponent;
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

struct StepClock
{
    double t = 0.0;
    double step = 1.0;
    double operator()() { double now = t; t += step; return now; }
};
} // namespace

TEST_CASE("a headless run (no window) ticks scripts through the frame pump", "[app]")
{
    TypeRegistry reg; ComponentReflection types; register_builtin_components(reg, types);

    World world;
    world.add_system<TransformSystem>();
    const auto e = world.create_entity();
    world.add_component<Transform>(e, Transform{});
    world.add_component<ScriptComponent>(e, ScriptComponent{
        "def on_update(dt):\n"
        "    t = get_component(entity, 'Transform')\n"
        "    t['position']['x'] += dt\n"
        "    set_component(entity, 'Transform', t)\n"});

    AssetRegistry registry;
    setup_runtime(world, registry, types, [] { return create_python_script_engine(); });

    RuntimeLoop loop(1.0F / 60.0F, 1); // one tick per advance -> exact tick count
    Options options; options.ticks = 4; options.fixed_hz = 60.0;
    StepClock clock{0.0, 1.0};
    PumpDeps deps{std::ref(clock), [](double) {}, [] { return false; }};
    NullReporter reporter;

    FramePump pump(loop, deps, reporter, options);
    const RunSummary summary = pump.run(world);

    REQUIRE(summary.stopped_by == "ticks");
    REQUIRE(loop.tick_count() == 4);
    // 4 ticks at fixed dt = 1/60 -> position.x advanced by 4/60.
    REQUIRE(world.get_component<Transform>(e)->position.x > 0.0f);
}
