#pragma once

#include <cmath>

namespace mosaic::foundation::math
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline bool operator==(const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
inline bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }

template <class V>
void reflect(V& visitor, Vec3& v)
{
    visitor.member("x", v.x);
    visitor.member("y", v.y);
    visitor.member("z", v.z);
}

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v)
{
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

} // namespace mosaic::foundation::math
