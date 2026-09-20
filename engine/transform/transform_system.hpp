#pragma once

#include "engine/ecs/system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/math/mat4.hpp"

namespace mosaic::engine::transform
{

// The first concrete System: composes each entity's world transform from its local
// TRS and its parent chain, writing a WorldTransform. Hierarchies must be acyclic
// (a depth guard bounds a malformed cycle).
class TransformSystem : public ecs::System
{
public:
    TransformSystem() { access_.reads<Transform>().writes<WorldTransform>(); }

    void update(World& world, float /*dt*/) override
    {
        world.for_each<Transform>([&](mosaic::foundation::EntityID e, Transform&)
        {
            const mosaic::foundation::math::Mat4 world_matrix = compute_world(world, e, 0);
            if (WorldTransform* existing = world.get_component<WorldTransform>(e))
            {
                existing->matrix = world_matrix;
            }
            else
            {
                world.add_component<WorldTransform>(e, WorldTransform{world_matrix});
            }
        });
    }

    // Public (not just this class's own update() loop): the canonical way to get an
    // entity's current world matrix on demand, honoring its parent chain, without
    // waiting for update() to next run and write a WorldTransform. Needed because
    // TransformSystem is deliberately NOT part of World's regular system list (see
    // app/run/editor.cpp's own comment on why) -- headless sessions (e.g. `mosaic
    // mcp`) never call update() again after startup, so a live WorldTransform read
    // would silently go stale after any edit. Pure and stateless: safe to call from
    // any read-only query without an actual TransformSystem instance's update() ever
    // having run for the entity in question.
    static constexpr int kMaxDepth = 256;

    static mosaic::foundation::math::Mat4 compute_world(World& world,
                                                        mosaic::foundation::EntityID e, int depth)
    {
        using mosaic::foundation::math::Mat4;
        Transform* t = world.get_component<Transform>(e);
        if (t == nullptr)
        {
            return Mat4::identity();
        }
        const Mat4 local = Mat4::trs(t->position, t->rotation, t->scale);
        if (depth < kMaxDepth && static_cast<bool>(t->parent) && world.is_valid(t->parent)
            && world.get_component<Transform>(t->parent) != nullptr)
        {
            return compute_world(world, t->parent, depth + 1) * local;
        }
        return local;
    }
};

} // namespace mosaic::engine::transform
