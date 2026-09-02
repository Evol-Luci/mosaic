#pragma once

#include <string>

#include "engine/physics/physics_components.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/script/script_component.hpp"
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
    types.register_component<mosaic::engine::render::Camera>(registry, "Camera");
    types.register_component<mosaic::engine::render::Material>(registry, "Material");
    types.register_component<mosaic::engine::render::MeshRenderer>(registry, "MeshRenderer");
    types.register_component<mosaic::engine::render::DirectionalLight>(registry, "DirectionalLight");
    types.register_component<mosaic::engine::render::PointLight>(registry, "PointLight");
    types.register_component<mosaic::engine::physics::RigidBody>(registry, "RigidBody");
    types.register_component<mosaic::engine::physics::Collider>(registry, "Collider");
    types.register_component<mosaic::engine::physics::SphereCollider>(registry, "SphereCollider");
    types.register_component<mosaic::engine::physics::CapsuleCollider>(registry, "CapsuleCollider");
    types.register_component<mosaic::engine::script::ScriptComponent>(registry, "ScriptComponent");
}

} // namespace mosaic::engine::reflection
