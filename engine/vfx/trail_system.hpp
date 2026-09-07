#pragma once

#include <cstdint>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ecs/system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::vfx
{
// Samples each TrailRenderer entity's Transform.position into a bounded history so the
// renderer can draw a fading tail. Transient state keyed by entity id (the ParticleSystem
// pattern); never serialized; writes nothing back to components.
class TrailSystem : public ecs::System
{
public:
    void update(World& world, float dt) override
    {
        world_ = &world;

        std::unordered_set<std::uint64_t> seen;
        world.for_each<TrailRenderer>(
            [&](mosaic::foundation::EntityID e, TrailRenderer& tr)
            {
                auto* tf = world.get_component<transform::Transform>(e);
                if (tf == nullptr) { return; }
                const std::uint64_t id  = e.value();
                seen.insert(id);
                const int           cap = tr.max_points > 0 ? tr.max_points : 1;

                auto it = trails_.find(id);
                if (it == trails_.end())
                {
                    TrailState st;
                    st.points.push_back(tf->position); // seed the head; sample from next tick
                    trails_.emplace(id, std::move(st));
                    return;
                }
                TrailState& st = it->second;

                st.accumulator += dt;
                if (st.accumulator >= tr.sample_interval)
                {
                    st.accumulator -= tr.sample_interval;
                    st.points.push_back(tf->position);
                }
                while (static_cast<int>(st.points.size()) > cap) { st.points.erase(st.points.begin()); }
            });

        for (auto it = trails_.begin(); it != trails_.end();)
        {
            it = (seen.count(it->first) != 0) ? std::next(it) : trails_.erase(it);
        }
    }

    // Position history oldest -> newest for `e`, or nullptr if none / the entity was destroyed.
    const std::vector<mosaic::foundation::math::Vec3>* points_for(
        mosaic::foundation::EntityID e) const
    {
        if (world_ && !world_->is_valid(e)) { return nullptr; }
        auto it = trails_.find(e.value());
        return it == trails_.end() ? nullptr : &it->second.points;
    }

private:
    struct TrailState
    {
        std::vector<mosaic::foundation::math::Vec3> points;
        float                                       accumulator = 0.0f;
    };

    std::unordered_map<std::uint64_t, TrailState> trails_;
    const World* world_ = nullptr;
};
} // namespace mosaic::engine::vfx
