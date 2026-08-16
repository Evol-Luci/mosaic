#pragma once

#include <vector>

#include "engine/ecs/component_type_id.hpp"

namespace mosaic::engine { class World; }

namespace mosaic::engine::ecs
{

// Declares which component types a system reads and writes. Declaration only —
// scheduling and execution are a later concern (the job system). Systems hold no
// per-entity state; state lives in components.
class ComponentAccess
{
public:
    template <typename T>
    ComponentAccess& reads()
    {
        reads_.push_back(component_type_id<T>());
        return *this;
    }

    template <typename T>
    ComponentAccess& writes()
    {
        writes_.push_back(component_type_id<T>());
        return *this;
    }

    const std::vector<ComponentTypeID>& read_set() const noexcept { return reads_; }
    const std::vector<ComponentTypeID>& write_set() const noexcept { return writes_; }

private:
    std::vector<ComponentTypeID> reads_;
    std::vector<ComponentTypeID> writes_;
};

class System
{
public:
    virtual ~System() = default;

    // Advance this system one tick. Default is a no-op so declaration-only
    // systems (access sets but no per-tick work) still compile.
    virtual void update(mosaic::engine::World&, float /*dt*/) {}

    const ComponentAccess& access() const noexcept { return access_; }

protected:
    ComponentAccess access_; // subclasses declare their access in their constructor
};

} // namespace mosaic::engine::ecs
