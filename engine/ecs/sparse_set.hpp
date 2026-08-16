#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ecs
{

// Entity-keyed sparse set. `sparse_` maps an entity's slot index to a position
// in the packed `dense_` / `components_` arrays. The full EntityID is stored in
// `dense_`, so a lookup with a stale generation does not match.
template <typename T>
class SparseSet
{
public:
    using EntityID = mosaic::foundation::EntityID;

    bool contains(EntityID id) const noexcept { return position_of(id) != kInvalid; }

    T& add(EntityID id, T value)
    {
        const std::uint32_t slot = id.index();
        if (slot >= sparse_.size())
        {
            sparse_.resize(slot + 1u, kInvalid);
        }

        const std::uint32_t pos = sparse_[slot];
        if (pos != kInvalid && pos < dense_.size() && dense_[pos].index() == slot)
        {
            // Overwrite in place; refresh the stored id in case the generation
            // advanced (slot reused without an intervening remove).
            dense_[pos] = id;
            components_[pos] = std::move(value);
            return components_[pos];
        }

        sparse_[slot] = static_cast<std::uint32_t>(dense_.size());
        dense_.push_back(id);
        components_.push_back(std::move(value));
        return components_.back();
    }

    T* get(EntityID id) noexcept
    {
        const std::uint32_t pos = position_of(id);
        return pos == kInvalid ? nullptr : &components_[pos];
    }

    const T* get(EntityID id) const noexcept
    {
        const std::uint32_t pos = position_of(id);
        return pos == kInvalid ? nullptr : &components_[pos];
    }

    bool remove(EntityID id)
    {
        const std::uint32_t pos = position_of(id);
        if (pos == kInvalid)
        {
            return false;
        }

        const std::uint32_t last = static_cast<std::uint32_t>(dense_.size() - 1u);
        if (pos != last)
        {
            dense_[pos] = dense_[last];
            components_[pos] = std::move(components_[last]);
            sparse_[dense_[pos].index()] = pos; // relink the moved element
        }
        dense_.pop_back();
        components_.pop_back();
        sparse_[id.index()] = kInvalid;
        return true;
    }

    std::size_t size() const noexcept { return dense_.size(); }

    // Visit every stored (entity, component) pair in dense order. Do not add or
    // remove elements of this set from within fn — that can reallocate the dense
    // arrays under the loop.
    template <typename Fn>
    void each(Fn&& fn)
    {
        for (std::size_t i = 0; i < dense_.size(); ++i)
        {
            fn(dense_[i], components_[i]);
        }
    }

private:
    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();

    // Returns the dense position for `id`, or kInvalid if absent or the stored
    // generation differs.
    std::uint32_t position_of(EntityID id) const noexcept
    {
        const std::uint32_t slot = id.index();
        if (slot >= sparse_.size())
        {
            return kInvalid;
        }
        const std::uint32_t pos = sparse_[slot];
        if (pos == kInvalid || pos >= dense_.size() || dense_[pos] != id)
        {
            return kInvalid;
        }
        return pos;
    }

    std::vector<std::uint32_t> sparse_;
    std::vector<EntityID> dense_;
    std::vector<T> components_;
};

} // namespace mosaic::engine::ecs
