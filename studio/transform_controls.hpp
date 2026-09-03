#pragma once

#include <algorithm>
#include <cmath>

#include "foundation/core/entity_id.hpp"
#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::studio
{

using mosaic::foundation::math::Quat;
using mosaic::foundation::math::Vec3;

struct EulerAngles
{
    float pitch = 0.0f; // rotation about X, radians
    float yaw   = 0.0f; // rotation about Y, radians
    float roll  = 0.0f; // rotation about Z, radians
};

// Intrinsic composition Ry(yaw) * Rx(pitch) * Rz(roll).
inline Quat euler_to_quat(const EulerAngles& e)
{
    using mosaic::foundation::math::from_axis_angle;
    const Quat qy = from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, e.yaw);
    const Quat qx = from_axis_angle(Vec3{1.0f, 0.0f, 0.0f}, e.pitch);
    const Quat qz = from_axis_angle(Vec3{0.0f, 0.0f, 1.0f}, e.roll);
    return normalized(qy * qx * qz);
}

// Inverse of euler_to_quat (YXZ order). Derived from the rotation matrix of q.
inline EulerAngles quat_to_euler(const Quat& qin)
{
    const Quat  q = normalized(qin);
    const float x = q.x, y = q.y, z = q.z, w = q.w;

    // For R = Ry(yaw) * Rx(pitch) * Rz(roll), the standard quaternion rotation
    // matrix gives: R[1][2] = 2(yz - wx) = -sin(pitch); R[0][2]/R[2][2] isolate
    // yaw; R[1][0]/R[1][1] isolate roll.
    const float sin_pitch = std::clamp(2.0f * (w * x - y * z), -1.0f, 1.0f);
    EulerAngles e;
    e.pitch = std::asin(sin_pitch);

    const float cos_pitch = std::cos(e.pitch);
    if (std::abs(cos_pitch) > 1e-4f)
    {
        e.yaw  = std::atan2(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y));
        e.roll = std::atan2(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z));
    }
    else
    {
        // Gimbal lock (pitch = ±90°): roll and yaw fold together; pin roll = 0.
        e.yaw  = std::atan2(2.0f * (w * y - x * z), 1.0f - 2.0f * (y * y + z * z));
        e.roll = 0.0f;
    }
    return e;
}

// Working slider state: reseed the Euler triple from the stored quat only when
// the selected entity changes, so dragging one slider doesn't get overwritten.
struct TransformControlsModel
{
    mosaic::foundation::EntityID entity{};
    EulerAngles                  euler{};
    bool                         active = false;

    void sync(mosaic::foundation::EntityID e, const Quat& stored)
    {
        if (!active || e != entity)
        {
            entity = e;
            euler  = quat_to_euler(stored);
            active = true;
        }
    }
};

} // namespace mosaic::studio
