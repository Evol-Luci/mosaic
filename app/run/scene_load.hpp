#pragma once

#include "app/cli/options.hpp"
#include "app/run/demo_scene.hpp"
#include "app/run/scene_io.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/scene/scene.hpp"
#include "engine/transform/transform_system.hpp"
#include "engine/world/world.hpp"
#include "foundation/logging/log.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::app
{
// Populate `world` for a run/editor session: from options.scene if set, else the built-in
// default demo. Derives WorldTransform (not serialized) so a loaded scene renders. Returns
// false only on a genuine load failure (missing/unparseable --scene); an absent --scene is not
// an error (it selects the demo).
inline bool populate_world(mosaic::engine::World& world, const Options& options,
                           const mosaic::engine::reflection::ComponentReflection& types)
{
    if (options.scene)
    {
        const std::optional<std::string> text = load_scene_file(*options.scene);
        if (!text)
        {
            mosaic::foundation::log::error("scene", "scene not found: {}", *options.scene);
            return false;
        }
        try
        {
            mosaic::engine::scene::load_from_json(*text, world, types);
        }
        catch (const mosaic::foundation::serialization::SerializationError& e)
        {
            mosaic::foundation::log::error("scene", "failed to load scene: {}", e.what());
            return false;
        }
    }
    else
    {
        build_default_scene(world);
    }
    // WorldTransform is derived, not serialized: one pass so loaded/default scenes render.
    mosaic::engine::transform::TransformSystem{}.update(world, 0.0f);
    return true;
}
} // namespace mosaic::app
