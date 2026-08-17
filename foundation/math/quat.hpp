#pragma once

#include <cmath>

#include "foundation/math/vec3.hpp"

namespace mosaic::foundation::math
{

struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat identity() { return Quat{0.0f, 0.0f, 0.0f, 1.0f}; }
};

inline Quat normalized(const Quat& q)
{
    const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (len == 0.0f)
    {
        return Quat::identity();
    }
    const float inv = 1.0f / len;
    return Quat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

inline Quat from_axis_angle(const Vec3& axis, float radians)
{
    const float len = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (len == 0.0f)
    {
        return Quat::identity();
    }
    const float inv = 1.0f / len;
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    return Quat{axis.x * inv * s, axis.y * inv * s, axis.z * inv * s, std::cos(half)};
}

// Hamilton product: (a * b) applies b then a.
inline Quat operator*(const Quat& a, const Quat& b)
{
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

template <class V>
void reflect(V& visitor, Quat& q)
{
    visitor.member("x", q.x);
    visitor.member("y", q.y);
    visitor.member("z", q.z);
    visitor.member("w", q.w);
}

} // namespace mosaic::foundation::math
