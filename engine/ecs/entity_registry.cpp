#include "engine/ecs/entity_registry.hpp"

namespace mosaic::engine::ecs
{

EntityRegistry::EntityID EntityRegistry::create()
{
    std::uint32_t index;
    if (!free_list_.empty())
    {
        index = free_list_.back();
        free_list_.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1u); // generations start at 1
    }
    ++alive_;
    return EntityID{index, generations_[index]};
}

bool EntityRegistry::destroy(EntityID id)
{
    if (!is_valid(id))
    {
        return false;
    }

    const std::uint32_t index = id.index();
    const std::uint32_t next = generations_[index] + 1u;
    if (next == kRetiredGeneration)
    {
        // Exhausted: retire the slot. Mark it with the sentinel and do not
        // return it to the free-list, so it is never handed out again.
        generations_[index] = kRetiredGeneration;
    }
    else
    {
        generations_[index] = next;
        free_list_.push_back(index);
    }
    --alive_;
    return true;
}

bool EntityRegistry::is_valid(EntityID id) const noexcept
{
    if (!id)
    {
        return false;
    }
    const std::uint32_t index = id.index();
    if (index >= generations_.size())
    {
        return false;
    }
    return generations_[index] == id.generation();
}

} // namespace mosaic::engine::ecs
