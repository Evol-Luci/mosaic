#include "engine/world/world.hpp"

namespace mosaic::engine
{

World::EntityID World::create_entity()
{
    return entities_.create();
}

void World::destroy_entity(EntityID id)
{
    // Deferred: committed at the end of the next tick so systems iterating a
    // component set never have entities vanish mid-iteration.
    pending_destroy_.push_back(id);
}

void World::clear()
{
    std::vector<EntityID> all;
    entities_.for_each([&](EntityID id) { all.push_back(id); });
    for (EntityID id : all)
    {
        components_.on_destroy(id);
        entities_.destroy(id);
    }
    // Any not-yet-flushed destroy_entity() calls now name already-destroyed
    // (and possibly recycled) ids -- drop them rather than let a future tick()
    // act on stale entries.
    pending_destroy_.clear();
}

void World::tick(float dt)
{
    // 1. Deferred events emitted before this tick are delivered now, at the
    //    tick's start (the fixed sync point).
    events_.drain_deferred();

    // 2. Run systems in registration order (single-threaded for now).
    for (std::unique_ptr<ecs::System>& system : systems_)
    {
        system->update(*this, dt);
    }

    // 3. Commit deferred destructions: components first, then the entity slot.
    for (EntityID id : pending_destroy_)
    {
        if (entities_.is_valid(id))
        {
            components_.on_destroy(id);
            entities_.destroy(id);
        }
    }
    pending_destroy_.clear();
}

} // namespace mosaic::engine
