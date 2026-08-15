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

private:
    friend struct detail::RegistryTestAccess;

    std::vector<std::uint32_t> generations_; // current generation per slot
    std::vector<std::uint32_t> free_list_;   // indices available for recycling
    std::size_t alive_ = 0;
};

} // namespace mosaic::engine::ecs
