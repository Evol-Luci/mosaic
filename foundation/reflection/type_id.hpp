#pragma once

#include <cstdint>

namespace mosaic::foundation::reflection
{

// A stable *within-run* type handle. Assignment order is unspecified across runs;
// the durable identity of a type is its registered NAME, not this integer. This
// subsumes the minimal ComponentTypeID shim the ECS core (#9) shipped: there is
// now one id source, re-exported by engine/ecs/component_type_id.hpp.
using TypeID = std::uint32_t;

namespace detail
{
// Not thread-safe; assumes single-threaded type registration.
inline TypeID next_type_id() noexcept
{
    static TypeID counter = 0;
    return counter++;
}
} // namespace detail

template <typename T>
TypeID type_id() noexcept
{
    static const TypeID id = detail::next_type_id();
    return id;
}

} // namespace mosaic::foundation::reflection
