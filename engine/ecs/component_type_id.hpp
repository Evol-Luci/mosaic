#pragma once

#include "foundation/reflection/type_id.hpp"

namespace mosaic::engine::ecs
{

// ComponentTypeID is the ECS's declared-access identity for a component type
// (ComponentAccess::reads()/writes(), for future job-scheduling use) -- an alias of
// reflection::TypeID, and component_type_id<T>() forwards to type_id<T>() directly,
// so a component's declared access identity and its reflected TypeInfo::id are the
// same integer within a run. One identity source, deliberately: two diverging id
// spaces for the same T is exactly the "#9 identity drift" this consolidation
// removed, and it must not come back.
//
// Issue #25 (storages_ vector waste) is fixed WITHOUT touching this identity:
// ComponentRegistry indexes its own storages_ vector by a separate, purely internal
// dense slot (engine/ecs/component_registry.hpp's detail::component_storage_slot<T>()),
// assigned only when a component type is actually first stored -- never exposed,
// never compared against ComponentTypeID. type_id<T>() still draws from one global
// counter shared with every reflected type (Vec3, Quat, and non-component types
// generally), but that no longer sizes or indexes any ECS storage.
using ComponentTypeID = mosaic::foundation::reflection::TypeID;

template <typename T>
ComponentTypeID component_type_id() noexcept
{
    return mosaic::foundation::reflection::type_id<T>();
}

} // namespace mosaic::engine::ecs
