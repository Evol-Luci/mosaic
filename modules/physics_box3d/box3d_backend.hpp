#pragma once

#include <memory>

#include "engine/physics/physics_backend.hpp"

namespace mosaic::physics::box3d
{
// Constructs a Box3D-backed PhysicsBackend. b3* stays inside the .cpp.
std::unique_ptr<mosaic::engine::physics::PhysicsBackend>
create_box3d_backend(const mosaic::engine::physics::WorldDef& def);
} // namespace mosaic::physics::box3d
