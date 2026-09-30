#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "engine/ecs/system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/script/script_event.hpp"
#include "engine/script/script_host.hpp"
#include "engine/script/script_worker.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"
#include "engine/assets/asset_registry.hpp"
#include "engine/script/script_source.hpp"

namespace mosaic::engine::script
{
// Runs each scripted entity's `on_update(dt)` per World::tick. Drives the NEUTRAL ScriptEngine via
// an injected factory (the CPython backend is supplied by the caller), so the engine tier stays
// Python-free. Caches one engine per scripted entity: the engine (with its own namespace) is created
// and the ScriptComponent's source compiled once, then `on_update(dt)` runs each tick. The cache is
// runtime-derived (rebuilt on demand); the authoritative source lives in the component.
// One row of per-entity on_update timing (see ScriptSystem::profile()).
struct ScriptProfileEntry
{
    std::uint64_t entity;
    std::uint64_t calls;
    double        total_ms;
    double        max_ms;
};

class ScriptSystem : public ecs::System
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;

    ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory,
                 ScriptHost host = {}, mosaic::engine::assets::AssetRegistry* registry = nullptr,
                 bool threaded = false)
        : types_(types), factory_(std::move(factory)), host_(std::move(host)), registry_(registry)
    {
        if (threaded)
        {
            worker_ = std::make_unique<ScriptWorker>(factory_, host_);
        }
    }

    void update(World& world, float dt) override
    {
        if (worker_) { update_threaded(world, dt); return; }
        update_inline(world, dt);
    }

    void update_inline(World& world, float dt)
    {
        // Subscribe once (deferred = one-tick, FIFO). The handler queues events delivered at each
        // tick's drain (before systems run); we deliver them below, before on_update.
        if (!subscribed_)
        {
            world.events().subscribe_deferred<ScriptEvent>(
                [this](const ScriptEvent& e) { pending_events_.push_back(e); });
            subscribed_ = true;
        }
        std::vector<ScriptEvent> events = std::move(pending_events_);
        pending_events_.clear();

        std::unordered_set<std::uint64_t> seen;
        world.for_each<ScriptComponent>(
            [&](mosaic::foundation::EntityID e, ScriptComponent& sc)
            {
                seen.insert(e.value());
                Entry& entry = engines_[e.value()];

                // Resolve the effective source: from the .py file if `path` is set (reloading on an
                // on-disk mtime change), else the inline `source`.
                std::string desired;
                if (!sc.script.is_nil() && registry_ != nullptr)
                {
                    entry.path.clear();
                    const auto res = registry_->load<ScriptSource>(sc.script);
                    desired = res ? res->text : entry.source; // missing asset -> keep last-good
                }
                else if (sc.path.empty())
                {
                    entry.path.clear();
                    desired = sc.source;
                }
                else
                {
                    std::error_code ec;
                    const auto mtime = std::filesystem::last_write_time(sc.path, ec);
                    const bool reload =
                        !ec && (!entry.engine || entry.path != sc.path || mtime != entry.mtime);
                    if (reload)
                    {
                        std::ifstream f(sc.path, std::ios::binary);
                        if (f)
                        {
                            desired = std::string(std::istreambuf_iterator<char>(f),
                                                  std::istreambuf_iterator<char>());
                            entry.path  = sc.path;
                            entry.mtime = mtime;
                        }
                        else { desired = entry.source; } // open failed -> keep last-good
                    }
                    else { desired = entry.source; }     // no change / missing file -> keep what we have
                }

                if (!entry.engine || entry.source != desired)
                {
                    entry.engine = factory_();
                    entry.source = desired;
                    entry.engine->bind_world(world, types_);
                    entry.engine->bind_host(host_);
                    entry.engine->set_global(
                        "entity",
                        mosaic::foundation::serialization::Value(
                            static_cast<std::uint64_t>(e.value())));
                    entry.engine->run_string(desired); // (re)compile: define on_update + top-level
                }
                // Deliver queued events (from the previous tick's emits) to on_event, if the
                // script defines it. Broadcast: every scripted entity sees every event.
                for (const ScriptEvent& ev : events)
                {
                    entry.engine->set_global(
                        "__evt_name", mosaic::foundation::serialization::Value(ev.name));
                    entry.engine->set_global("__evt_payload", ev.payload);
                    entry.engine->run_string(
                        "if 'on_event' in globals(): on_event(__evt_name, __evt_payload)\n");
                }
                entry.engine->set_global(
                    "dt", mosaic::foundation::serialization::Value(static_cast<double>(dt)));
                const auto t0 = std::chrono::steady_clock::now();
                entry.engine->run_string("on_update(dt)"); // non-fatal on error (result ignored)
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                        .count();
                entry.calls += 1;
                entry.total_ms += ms;
                entry.max_ms = std::max(entry.max_ms, ms);
            });

        // Prune engines for entities no longer scripted/alive (not visited this tick).
        for (auto it = engines_.begin(); it != engines_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : engines_.erase(it);
        }
    }

    void update_threaded(World& world, float dt)
    {
        // 1. Apply the previous job's writes (main thread).
        const ScriptWorker::WorkerResult batch = worker_->collect();
        for (const SnapshotContext::Write& w : batch.writes)
        {
            types_.set_value(world, mosaic::foundation::EntityID(w.entity), w.comp, w.value);
        }
        // 2. Snapshot every scripted entity's components + resolve its source.
        ScriptWorker::Job job;
        job.dt = dt;
        world.for_each<ScriptComponent>(
            [&](mosaic::foundation::EntityID e, ScriptComponent& sc)
            {
                const std::string source = resolve_source(sc); // asset id -> path -> inline
                if (source.empty()) { return; }
                std::unordered_map<std::string, mosaic::foundation::serialization::Value> comps;
                for (const std::string& name : types_.components_on(world, e))
                {
                    if (auto v = types_.component_value(world, e, name)) { comps[name] = *v; }
                }
                job.snapshot[e.value()] = std::move(comps);
                job.scripts.push_back(ScriptWorker::ScriptRef{e.value(), source});
            });
        // 3. Dispatch (non-blocking).
        worker_->dispatch(std::move(job));
    }

    std::string resolve_source(const ScriptComponent& sc)
    {
        if (!sc.script.is_nil() && registry_ != nullptr)
        {
            const auto res = registry_->load<ScriptSource>(sc.script);
            return res ? res->text : sc.source;
        }
        if (!sc.path.empty())
        {
            std::ifstream f(sc.path, std::ios::binary);
            if (f)
            {
                return std::string(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
            }
        }
        return sc.source;
    }

    // Per-entity on_update timing accumulated so far (inline mode). One row per scripted
    // entity that has run at least once; unordered.
    std::vector<ScriptProfileEntry> profile() const
    {
        std::vector<ScriptProfileEntry> out;
        for (const auto& [id, entry] : engines_)
        {
            if (entry.calls > 0)
            {
                out.push_back(ScriptProfileEntry{id, entry.calls, entry.total_ms, entry.max_ms});
            }
        }
        return out;
    }

private:
    struct Entry
    {
        std::unique_ptr<ScriptEngine>   engine;
        std::string                     source;  // last-compiled source
        std::string                     path;    // last-seen path (detect a path change / switch to inline)
        std::filesystem::file_time_type mtime{}; // last-seen file mtime (detect an on-disk edit)
        std::uint64_t                   calls    = 0;   // on_update calls (profiler; persists across recompiles)
        double                          total_ms = 0.0; // cumulative on_update wall-time
        double                          max_ms   = 0.0; // slowest single on_update
    };

    const reflection::ComponentReflection& types_;
    EngineFactory factory_;
    ScriptHost host_;
    mosaic::engine::assets::AssetRegistry* registry_ = nullptr;
    std::unordered_map<std::uint64_t, Entry> engines_;
    std::unique_ptr<ScriptWorker> worker_;
    std::vector<ScriptEvent>      pending_events_; // queued by the deferred bus handler
    bool                          subscribed_ = false;
};
} // namespace mosaic::engine::script
