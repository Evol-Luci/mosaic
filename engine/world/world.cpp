#include "engine/world/world.hpp"

namespace mosaic::engine
{

World::EntityID World::create_entity()
{
    return entities_.create();
}

} // namespace mosaic::engine
