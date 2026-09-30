#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_worker.hpp"
#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"
#include "foundation/math/vec3.hpp"

using mosaic::engine::script::ScriptWorker;
using mosaic::foundation::serialization::Value;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("ScriptWorker runs a job on its thread and returns the write batch", "[script]")
{
    ScriptWorker worker([] { return create_python_script_engine(); });

    ScriptWorker::Job job;
    job.dt = 0.5f;
    job.snapshot[1]["Score"] = Value(2.0);
    job.scripts.push_back(ScriptWorker::ScriptRef{
        1, "def on_update(dt):\n"
           "    v = get_component(entity, 'Score')\n"
           "    set_component(entity, 'Score', v + dt)\n"});

    worker.dispatch(std::move(job));
    const auto result = worker.collect();

    REQUIRE(result.writes.size() == 1);
    REQUIRE(result.writes[0].entity == 1);
    REQUIRE(result.writes[0].comp == "Score");
    // value 2.0 + dt 0.5 = 2.5 — marshal back to double and check.
    REQUIRE(result.writes[0].value.as<double>() == 2.5);
}

TEST_CASE("ScriptWorker buffers apply_impulse instead of calling the host", "[script]")
{
    // The whole point: on the worker thread the call must NOT reach the host, which
    // on the real path writes into the live Box3D body while the main thread may be
    // mid-step. It is recorded and replayed on the main thread instead.
    bool host_called = false;
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [&](std::uint64_t, mosaic::foundation::math::Vec3,
                             mosaic::foundation::math::Vec3) -> bool
    { host_called = true; return true; };

    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[7] = {{"Transform", mosaic::foundation::serialization::Value{}},
                       {"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"SphereCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({7, "def on_update(dt):\n"
                              "    ok = apply_impulse(7, [2.0, 0.0, -1.0], [0.0, 0.5, 0.0])\n"
                              "    assert ok is True\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE_FALSE(host_called); // never reached the live world
    REQUIRE(result.impulses.size() == 1);
    REQUIRE(result.impulses[0].entity == 7);
    REQUIRE(result.impulses[0].impulse.x == 2.0f);
    REQUIRE(result.impulses[0].impulse.z == -1.0f);
    REQUIRE(result.impulses[0].point.y == 0.5f);
}

TEST_CASE("ScriptWorker reports False for an entity with a RigidBody but no collider", "[script]")
{
    // Review Focus 1: checking only RigidBody is the natural shortcut and over-reports.
    // Body creation needs a RigidBody AND a collider, so this entity never gets one.
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [](std::uint64_t, mosaic::foundation::math::Vec3,
                            mosaic::foundation::math::Vec3) -> bool { return true; };
    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[3] = {{"RigidBody", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({3, "def on_update(dt):\n"
                              "    ok = apply_impulse(3, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty()); // nothing buffered for a body that cannot exist
}

TEST_CASE("ScriptWorker reports False for an entity with no RigidBody", "[script]")
{
    // A distinct early return from the no-collider case above: this entity is in the
    // snapshot and has a collider, but nothing makes it simulated.
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [](std::uint64_t, mosaic::foundation::math::Vec3,
                            mosaic::foundation::math::Vec3) -> bool { return true; };
    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[4] = {{"SphereCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({4, "def on_update(dt):\n"
                              "    ok = apply_impulse(4, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty());
}

TEST_CASE("ScriptWorker keeps both impulses when one entity is pushed twice in a tick", "[script]")
{
    // Review Focus 3: a map keyed by entity would silently drop one. Impulses are
    // additive in physics, so both must survive.
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [](std::uint64_t, mosaic::foundation::math::Vec3,
                            mosaic::foundation::math::Vec3) -> bool { return true; };
    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[5] = {{"Transform", mosaic::foundation::serialization::Value{}},
                       {"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"CapsuleCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({5, "def on_update(dt):\n"
                              "    apply_impulse(5, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    apply_impulse(5, [0.0, 2.0, 0.0], [0.0, 0.0, 0.0])\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.size() == 2);
    REQUIRE(result.impulses[0].impulse.x == 1.0f);
    REQUIRE(result.impulses[1].impulse.y == 2.0f);
}

TEST_CASE("ScriptWorker buffers a push aimed at an entity outside the snapshot", "[script]")
{
    // A controller script pushing some OTHER object: the target has a body but no
    // ScriptComponent, so it is absent from the snapshot and the worker cannot judge it.
    // It must buffer optimistically rather than silently refuse -- the main-thread
    // replay re-checks has_body and is the authority.
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [](std::uint64_t, mosaic::foundation::math::Vec3,
                            mosaic::foundation::math::Vec3) -> bool { return true; };
    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[1] = {{"Transform", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({1, "def on_update(dt):\n"
                              "    ok = apply_impulse(42, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is True\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.size() == 1);
    REQUIRE(result.impulses[0].entity == 42);
}

TEST_CASE("ScriptWorker reports False for a snapshotted entity with no Transform", "[script]")
{
    // PhysicsSystem::update bails without a Transform, so no body is ever created.
    // Answering True here would tell a script the push landed when the main thread
    // will drop it.
    mosaic::engine::script::ScriptHost host;
    host.apply_impulse = [](std::uint64_t, mosaic::foundation::math::Vec3,
                            mosaic::foundation::math::Vec3) -> bool { return true; };
    ScriptWorker worker([] { return create_python_script_engine(); }, host);

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[6] = {{"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"SphereCollider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({6, "def on_update(dt):\n"
                              "    ok = apply_impulse(6, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty());
}

TEST_CASE("ScriptWorker refuses to buffer when the host has no apply_impulse", "[script]")
{
    // ScriptHost documents an unset hook as a no-op returning false. Buffering here
    // would return True and then discard the impulse at replay, since nothing can
    // apply it.
    ScriptWorker worker([] { return create_python_script_engine(); },
                        mosaic::engine::script::ScriptHost{});

    ScriptWorker::Job job;
    job.dt = 1.0f / 60.0f;
    job.snapshot[2] = {{"Transform", mosaic::foundation::serialization::Value{}},
                       {"RigidBody", mosaic::foundation::serialization::Value{}},
                       {"Collider", mosaic::foundation::serialization::Value{}}};
    job.scripts.push_back({2, "def on_update(dt):\n"
                              "    ok = apply_impulse(2, [1.0, 0.0, 0.0], [0.0, 0.0, 0.0])\n"
                              "    assert ok is False\n"});
    worker.dispatch(std::move(job));

    const auto result = worker.collect();
    REQUIRE(result.impulses.empty());
}
