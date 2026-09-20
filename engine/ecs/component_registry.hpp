#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "engine/ecs/entity_registry.hpp"
#include "engine/ecs/sparse_set.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::ecs
{

namespace detail
{
// A dense, storage-only slot index -- deliberately NOT ComponentTypeID (issue #25:
// naively giving component storage its own separate identity counter would
// reintroduce the exact "#9 identity drift" the reflection consolidation removed --
// two diverging integer spaces both claiming to identify the same T). This stays
// completely private to ComponentRegistry's own storages_ vector and is never
// compared against ComponentTypeID/TypeID anywhere; it only exists so storages_ is
// sized by "how many distinct component types have ever been stored," not by the
// much larger, sparser space of every reflected type ever assigned a TypeID
// (Vec3, Quat, and every other non-component reflected struct included).
// Not thread-safe, mirroring foundation::reflection::type_id<T>()'s own contract.
inline std::size_t next_component_storage_slot() noexcept
{
    static std::size_t counter = 0;
    return counter++;
}

template <typename T>
std::size_t component_storage_slot() noexcept
{
    static const std::size_t slot = next_component_storage_slot();
    return slot;
}
} // namespace detail

// Type-erased handle to one component type's storage, so the registry can hold
// heterogeneous storages and erase an entity from all of them on destroy.
class IComponentStorage
{
public:
    virtual ~IComponentStorage() = default;
    virtual bool remove(mosaic::foundation::EntityID id) = 0;
    virtual bool contains(mosaic::foundation::EntityID id) const = 0;
};

template <typename T>
class ComponentStorage final : public IComponentStorage
{
public:
    using EntityID = mosaic::foundation::EntityID;

    T& add(EntityID id, T value) { return set_.add(id, std::move(value)); }
    T* get(EntityID id) noexcept { return set_.get(id); }

    bool remove(EntityID id) override { return set_.remove(id); }
    bool contains(EntityID id) const override { return set_.contains(id); }

    template <typename Fn>
    void each(Fn&& fn) { set_.each(std::forward<Fn>(fn)); }

private:
    SparseSet<T> set_;
};

// Coordinates per-type component storage with entity validity. Every operation
// rejects handles the owning EntityRegistry no longer considers valid.
class ComponentRegistry
{
public:
    using EntityID = mosaic::foundation::EntityID;

    explicit ComponentRegistry(const EntityRegistry& entities) : entities_(&entities) {}

    // Non-copyable/non-movable: holds a non-owning back-pointer to the EntityRegistry
    // it was constructed from, so its address must stay stable.
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;
    ComponentRegistry(ComponentRegistry&&) = delete;
    ComponentRegistry& operator=(ComponentRegistry&&) = delete;

    template <typename T>
    bool add(EntityID id, T value)
    {
        if (!entities_->is_valid(id))
        {
            return false;
        }
        storage<T>().add(id, std::move(value));
        return true;
    }

    template <typename T>
    T* get(EntityID id)
    {
        if (!entities_->is_valid(id))
        {
            return nullptr;
        }
        ComponentStorage<T>* s = find_storage<T>();
        return s ? s->get(id) : nullptr;
    }

    template <typename T>
    bool has(EntityID id) const
    {
        if (!entities_->is_valid(id))
        {
            return false;
        }
        const std::size_t slot = detail::component_storage_slot<T>();
        return slot < storages_.size() && storages_[slot] && storages_[slot]->contains(id);
    }

    template <typename T>
    bool remove(EntityID id)
    {
        if (!entities_->is_valid(id))
        {
            return false;
        }
        ComponentStorage<T>* s = find_storage<T>();
        return s ? s->remove(id) : false;
    }

    // Visit (EntityID, T&) for every entity holding a T, in dense order. No-op if
    // no T has ever been added. Do not add/remove T within fn (see SparseSet::each).
    template <typename T, typename Fn>
    void for_each(Fn&& fn)
    {
        if (ComponentStorage<T>* s = find_storage<T>())
        {
            s->each(std::forward<Fn>(fn));
        }
    }

    // Erase an entity from every storage. Safe to call before or after
    // EntityRegistry::destroy, as long as the slot has not been recycled and re-added
    // (storage matches on the stored EntityID directly, not via the registry).
    void on_destroy(EntityID id)
    {
        for (std::unique_ptr<IComponentStorage>& storage : storages_)
        {
            if (storage)
            {
                storage->remove(id);
            }
        }
    }

private:
    template <typename T>
    ComponentStorage<T>& storage()
    {
        const std::size_t slot = detail::component_storage_slot<T>();
        if (slot >= storages_.size())
        {
            storages_.resize(slot + 1u);
        }
        if (!storages_[slot])
        {
            storages_[slot] = std::make_unique<ComponentStorage<T>>();
        }
        return static_cast<ComponentStorage<T>&>(*storages_[slot]);
    }

    template <typename T>
    ComponentStorage<T>* find_storage()
    {
        const std::size_t slot = detail::component_storage_slot<T>();
        if (slot >= storages_.size() || !storages_[slot])
        {
            return nullptr;
        }
        return static_cast<ComponentStorage<T>*>(storages_[slot].get());
    }

    const EntityRegistry* entities_; // non-owning; must outlive this ComponentRegistry
    // Indexed by component_storage_slot<T>() (a dense, storage-only index -- see
    // namespace detail above), NOT ComponentTypeID: sized by how many distinct
    // component types have actually been stored, not by every reflected type's
    // (much larger, sparser) TypeID space.
    std::vector<std::unique_ptr<IComponentStorage>> storages_;
};

} // namespace mosaic::engine::ecs
