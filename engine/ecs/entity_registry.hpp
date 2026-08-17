#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ecs
{

namespace detail { struct RegistryTestAccess; }

// Slot table of generational entities. create() hands out a slot (recycled from
// a free-list or freshly grown); destroy() bumps the slot's generation so old
// handles no longer validate. On generation exhaustion the slot is retired
// (never recycled) so generational checks can never alias.
class EntityRegistry
{
public:
    // Generation value marking a retired slot; never issued to a live entity.
    static constexpr std::uint32_t kRetiredGeneration = 0xFFFFFFFFu;

    using EntityID = mosaic::foundation::EntityID;

    EntityID create();
    bool destroy(EntityID id);
    bool is_valid(EntityID id) const noexcept;
    std::size_t alive_count() const noexcept { return alive_; }

    // Visit each live entity in ascending slot order. Skips retired slots and
    // slots currently on the free-list. fn is invoked as fn(EntityID).
    template <typename Fn>
    void for_each(Fn&& fn) const
    {
        std::vector<bool> is_free(generations_.size(), false);
        for (std::uint32_t slot : free_list_)
        {
            is_free[slot] = true;
        }
        for (std::size_t i = 0; i < generations_.size(); ++i)
        {
            if (generations_[i] == kRetiredGeneration || is_free[i])
            {
                continue;
            }
            fn(EntityID{static_cast<std::uint32_t>(i), generations_[i]});
        }
    }

private:
    friend struct detail::RegistryTestAccess;

    std::vector<std::uint32_t> generations_; // current generation per slot
    std::vector<std::uint32_t> free_list_;   // indices available for recycling
    std::size_t alive_ = 0;
};

} // namespace mosaic::engine::ecs
