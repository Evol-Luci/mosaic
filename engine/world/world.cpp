#include "engine/world/world.hpp"

namespace mosaic::engine
{

World::EntityID World::create_entity()
{
    return entities_.create();
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
}

} // namespace mosaic::engine
