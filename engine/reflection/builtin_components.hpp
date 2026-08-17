#pragma once

#include <string>

#include "engine/reflection/component_reflection.hpp"
#include "engine/transform/transform.hpp"
#include "foundation/reflection/type_registry.hpp"

namespace mosaic::engine::reflection
{

// Registers every built-in engine component (by stable name) into `types`,
// recording type metadata in `registry`. The app and the future editor call
// this so they share one component set and cannot drift. The caller owns both
// `registry` and `types` and keeps them alive for as long as `types` is used.
inline void register_builtin_components(
    mosaic::foundation::reflection::TypeRegistry& registry, ComponentReflection& types)
{
    types.register_component<mosaic::engine::transform::Transform>(registry, "Transform");
    // future: Camera, Mesh, ...
}

} // namespace mosaic::engine::reflection
