#pragma once

#include <string>

#include "engine/animation/animation_player.hpp"
#include "engine/audio/audio_components.hpp"
#include "engine/navigation/nav_agent.hpp"
#include "engine/particles/particle_emitter.hpp"
#include "engine/physics/physics_components.hpp"
#include "engine/sequencer/sequence.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/render/camera.hpp"
#include "engine/render/directional_light.hpp"
#include "engine/render/material.hpp"
#include "engine/render/mesh_renderer.hpp"
#include "engine/render/point_light.hpp"
#include "engine/script/script_component.hpp"
#include "engine/transform/transform.hpp"
#include "engine/vfx/decal.hpp"
#include "engine/vfx/screen_effect.hpp"
#include "engine/vfx/trail_renderer.hpp"
#include "engine/ui/ui_image.hpp"
#include "engine/ui/ui_panel.hpp"
#include "engine/ui/ui_text.hpp"
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
    types.register_component<mosaic::engine::animation::AnimationPlayer>(registry, "AnimationPlayer");
    types.register_component<mosaic::engine::sequencer::Sequence>(registry, "Sequence");
    types.register_component<mosaic::engine::navigation::NavAgent>(registry, "NavAgent");
    types.register_component<mosaic::engine::particles::ParticleEmitter>(registry, "ParticleEmitter");
    types.register_component<mosaic::engine::audio::AudioSource>(registry, "AudioSource");
    types.register_component<mosaic::engine::audio::AudioListener>(registry, "AudioListener");
    types.register_component<mosaic::engine::vfx::TrailRenderer>(registry, "TrailRenderer");
    types.register_component<mosaic::engine::vfx::Decal>(registry, "Decal");
    types.register_component<mosaic::engine::vfx::ScreenEffect>(registry, "ScreenEffect");
    types.register_component<mosaic::engine::ui::UiPanel>(registry, "UiPanel");
    types.register_component<mosaic::engine::ui::UiImage>(registry, "UiImage");
    types.register_component<mosaic::engine::ui::UiText>(registry, "UiText");
}

} // namespace mosaic::engine::reflection
