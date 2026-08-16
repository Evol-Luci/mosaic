#pragma once

#include "foundation/reflection/type_id.hpp"

namespace mosaic::engine::ecs
{

// The ECS keys component storage by a per-type integer id. That identity now
// comes from the reflection system's single source (foundation/reflection):
// ComponentTypeID is an alias of reflection::TypeID and component_type_id<T>()
// forwards to type_id<T>(), so a component's storage slot and its reflected
// TypeInfo::id are the same integer within a run.
//
// v0.1 trade-off (expand/fix later — see issue #25): type_id<T>() draws from a
// single global counter shared with ALL reflected types, not just components. Since
// ComponentRegistry sizes its storages_ vector to max_component_id + 1, non-component
// reflected types inflate that vector with null gaps. Bounded, correctness-safe waste
// for now; a later version should give components a dense id space (or map storages_
// by TypeID) without reintroducing the #9 identity drift.
using ComponentTypeID = mosaic::foundation::reflection::TypeID;

template <typename T>
ComponentTypeID component_type_id() noexcept
{
    return mosaic::foundation::reflection::type_id<T>();
}

} // namespace mosaic::engine::ecs
