#pragma once

#include <cstdint>
#include <vector>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::engine::physics
{
using BodyHandle = std::uint32_t; // opaque; 0 == invalid

enum class BodyType { Static, Dynamic };

enum class ShapeKind { Box, Sphere, Capsule };

struct Shape
{
    ShapeKind                      kind = ShapeKind::Box;
    mosaic::foundation::math::Vec3 half_extents{0.5f, 0.5f, 0.5f}; // box
    float                          radius = 0.5f;                  // sphere / capsule
    float                          half_height = 0.5f;             // capsule half-length (local Y)
};

struct BodyDef
{
    BodyType                       type = BodyType::Dynamic;
    mosaic::foundation::math::Vec3 position{};
    Shape                          shape{};
    float                          density = 1.0f; // ignored for static bodies
};

struct Pose
{
    mosaic::foundation::math::Vec3 position{};
    mosaic::foundation::math::Quat rotation{}; // P1: identity for valid bodies
};

struct WorldDef
{
    mosaic::foundation::math::Vec3 gravity{0.0f, -9.81f, 0.0f}; // Y-up, meters, m/s^2
};

struct RaycastResult
{
    bool                           hit = false;
    BodyHandle                     body = 0;        // valid only when hit
    mosaic::foundation::math::Vec3 point{};          // world hit point
    mosaic::foundation::math::Vec3 normal{};         // world surface normal at the hit
    float                          distance = 0.0f;  // distance from origin along the ray
};

enum class ContactPhase { Begin, End };

struct BodyContact
{
    ContactPhase phase;
    BodyHandle   a;
    BodyHandle   b;
};

// The physics world contract; the Box3D backend implements it behind the wall. Stepping is
// deterministic: identical construction + calls yield identical poses across runs.
class PhysicsBackend
{
public:
    virtual ~PhysicsBackend() = default;
    virtual BodyHandle create_body(const BodyDef& def) = 0;
    virtual void       step(float dt, int substeps) = 0;
    virtual Pose       body_pose(BodyHandle body) const = 0;

    // Teleport a body to a new pose, resetting its velocity. Used to push external
    // (e.g. editor/inspector) Transform edits into the simulation.
    virtual void set_body_transform(BodyHandle body,
                                    const mosaic::foundation::math::Vec3& position,
                                    const mosaic::foundation::math::Quat& rotation) = 0;

    // Begin/end touch contacts produced by the most recent step().
    virtual std::vector<BodyContact> contact_events() const = 0;

    // Closest-hit raycast along [origin, origin + normalize(direction) * max_distance].
    virtual RaycastResult raycast(const mosaic::foundation::math::Vec3& origin,
                                  const mosaic::foundation::math::Vec3& direction,
                                  float max_distance) const = 0;
};
} // namespace mosaic::engine::physics
