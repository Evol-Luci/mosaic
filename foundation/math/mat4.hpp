#pragma once

#include <array>
#include <cmath>

#include "foundation/math/quat.hpp"
#include "foundation/math/vec3.hpp"

namespace mosaic::foundation::math
{

// Column-major 4x4: element(row, col) = m[col * 4 + row].
struct Mat4
{
    std::array<float, 16> m{};

    static Mat4 identity()
    {
        Mat4 out;
        out.m[0] = 1.0f;
        out.m[5] = 1.0f;
        out.m[10] = 1.0f;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 translation(const Vec3& t)
    {
        Mat4 out = identity();
        out.m[12] = t.x;
        out.m[13] = t.y;
        out.m[14] = t.z;
        return out;
    }

    static Mat4 scale(const Vec3& s)
    {
        Mat4 out;
        out.m[0] = s.x;
        out.m[5] = s.y;
        out.m[10] = s.z;
        out.m[15] = 1.0f;
        return out;
    }

    static Mat4 rotation(const Quat& q)
    {
        const float x = q.x;
        const float y = q.y;
        const float z = q.z;
        const float w = q.w;
        Mat4 out;
        out.m[0] = 1.0f - 2.0f * (y * y + z * z);
        out.m[1] = 2.0f * (x * y + w * z);
        out.m[2] = 2.0f * (x * z - w * y);
        out.m[3] = 0.0f;
        out.m[4] = 2.0f * (x * y - w * z);
        out.m[5] = 1.0f - 2.0f * (x * x + z * z);
        out.m[6] = 2.0f * (y * z + w * x);
        out.m[7] = 0.0f;
        out.m[8] = 2.0f * (x * z + w * y);
        out.m[9] = 2.0f * (y * z - w * x);
        out.m[10] = 1.0f - 2.0f * (x * x + y * y);
        out.m[11] = 0.0f;
        out.m[15] = 1.0f;
        return out;
    }

    // Defined out-of-line below (needs operator*).
    static Mat4 trs(const Vec3& t, const Quat& r, const Vec3& s);

    static Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z);
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
};

inline Mat4 operator*(const Mat4& a, const Mat4& b)
{
    Mat4 out;
    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

inline Mat4 Mat4::trs(const Vec3& t, const Quat& r, const Vec3& s)
{
    return translation(t) * rotation(r) * scale(s);
}

inline Vec3 transform_point(const Mat4& m, const Vec3& p)
{
    return Vec3{
        m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

inline Mat4 Mat4::perspective(float fovy_radians, float aspect, float near_z, float far_z)
{
    const float tan_half = std::tan(fovy_radians * 0.5f);
    Mat4 out; // all-zero
    out.m[0] = 1.0f / (aspect * tan_half);                  // [0][0]
    out.m[5] = 1.0f / tan_half;                             // [1][1]
    out.m[10] = far_z / (near_z - far_z);                   // [2][2]
    out.m[11] = -1.0f;                                      // [2][3]
    out.m[14] = -(far_z * near_z) / (far_z - near_z);       // [3][2]
    return out;
}

inline Mat4 Mat4::look_at(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 f = normalize(center - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 out = identity();
    out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;   // row 0 = right
    out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;   // row 1 = up
    out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z; // row 2 = -forward
    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    return out;
}

} // namespace mosaic::foundation::math
