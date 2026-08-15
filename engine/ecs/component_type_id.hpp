#pragma once

#include <cstdint>

namespace mosaic::engine::ecs
{

// Minimal reflection shim: a stable, per-type integer id assigned on first use
// within a run. This is type identity only — NOT field reflection. The real
// reflection system (see docs/design/reflection.md) will supply reflection-
// derived TypeIDs and reconcile with this; see the design spec's Risks section.
using ComponentTypeID = std::uint32_t;

namespace detail
{
// Not thread-safe; assumes single-threaded component-type registration.
inline ComponentTypeID next_component_type_id() noexcept
{
    static ComponentTypeID counter = 0;
    return counter++;
}
} // namespace detail

template <typename T>
ComponentTypeID component_type_id() noexcept
{
    static const ComponentTypeID id = detail::next_component_type_id();
    return id;
}

} // namespace mosaic::engine::ecs
