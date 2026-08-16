#pragma once

#include "foundation/reflection/type_id.hpp"

namespace mosaic::engine::ecs
{

// The ECS keys component storage by a per-type integer id. That identity now
// comes from the reflection system's single source (foundation/reflection):
// ComponentTypeID is an alias of reflection::TypeID and component_type_id<T>()
// forwards to type_id<T>(), so a component's storage slot and its reflected
// TypeInfo::id are the same integer within a run.
using ComponentTypeID = mosaic::foundation::reflection::TypeID;

template <typename T>
ComponentTypeID component_type_id() noexcept
{
    return mosaic::foundation::reflection::type_id<T>();
}

} // namespace mosaic::engine::ecs
