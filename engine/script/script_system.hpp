#pragma once

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "engine/ecs/system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/script/script_host.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
// Runs each scripted entity's `on_update(dt)` per World::tick. Drives the NEUTRAL ScriptEngine via
// an injected factory (the CPython backend is supplied by the caller), so the engine tier stays
// Python-free. Caches one engine per scripted entity: the engine (with its own namespace) is created
// and the ScriptComponent's source compiled once, then `on_update(dt)` runs each tick. The cache is
// runtime-derived (rebuilt on demand); the authoritative source lives in the component.
class ScriptSystem : public ecs::System
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;

    ScriptSystem(const reflection::ComponentReflection& types, EngineFactory factory,
                 ScriptHost host = {})
        : types_(types), factory_(std::move(factory)), host_(std::move(host))
    {
    }

    void update(World& world, float dt) override
    {
        std::unordered_set<std::uint64_t> seen;
        world.for_each<ScriptComponent>(
            [&](mosaic::foundation::EntityID e, ScriptComponent& sc)
            {
                seen.insert(e.value());
                Entry& entry = engines_[e.value()];

                // Resolve the effective source: from the .py file if `path` is set (reloading on an
                // on-disk mtime change), else the inline `source`.
                std::string desired;
                if (sc.path.empty())
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
                entry.engine->set_global(
                    "dt", mosaic::foundation::serialization::Value(static_cast<double>(dt)));
                entry.engine->run_string("on_update(dt)"); // non-fatal on error (result ignored)
            });

        // Prune engines for entities no longer scripted/alive (not visited this tick).
        for (auto it = engines_.begin(); it != engines_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : engines_.erase(it);
        }
    }

private:
    struct Entry
    {
        std::unique_ptr<ScriptEngine>   engine;
        std::string                     source;  // last-compiled source
        std::string                     path;    // last-seen path (detect a path change / switch to inline)
        std::filesystem::file_time_type mtime{}; // last-seen file mtime (detect an on-disk edit)
    };

    const reflection::ComponentReflection& types_;
    EngineFactory factory_;
    ScriptHost host_;
    std::unordered_map<std::uint64_t, Entry> engines_;
};
} // namespace mosaic::engine::script
