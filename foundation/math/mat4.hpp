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

    // World-space forward/up of this matrix's own orientation (e.g. an object's
    // WorldTransform), matching the engine's camera convention: local +Z maps to
    // "backward", so forward is the negated 3rd column; up is the un-negated 2nd
    // column. Identity yields forward{0,0,-1}, up{0,1,0} -- matching
    // ListenerState's own defaults. Normalized so a non-unit scale on the matrix
    // doesn't leak into direction magnitude.
    Vec3 forward() const;
    Vec3 up() const;

    static Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z);
    static Mat4 look_at(const Vec3& eye, const Vec3& center, const Vec3& up);
    static Mat4 orthographic(float left, float right, float bottom, float top, float near_z, float far_z);
    static Mat4 transpose(const Mat4& m);
    static Mat4 inverse_affine(const Mat4& m);
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

// Apply m's upper-left 3x3 (linear part) to a direction vector, ignoring translation.
inline Vec3 transform_direction(const Mat4& m, const Vec3& v)
{
    return Vec3{
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z};
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

inline Mat4 Mat4::orthographic(float left, float right, float bottom, float top,
                               float near_z, float far_z)
{
    Mat4 out = identity();
    out.m[0] = 2.0f / (right - left);
    out.m[5] = 2.0f / (top - bottom);
    out.m[10] = 1.0f / (near_z - far_z);
    out.m[12] = -(right + left) / (right - left);
    out.m[13] = -(top + bottom) / (top - bottom);
    out.m[14] = -near_z / (far_z - near_z);
    return out;
}

inline Mat4 Mat4::transpose(const Mat4& m)
{
    Mat4 out;
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            out.m[col * 4 + row] = m.m[row * 4 + col];
        }
    }
    return out;
}

inline Vec3 Mat4::forward() const
{
    return normalize(Vec3{-m[8], -m[9], -m[10]});
}

inline Vec3 Mat4::up() const
{
    return normalize(Vec3{m[4], m[5], m[6]});
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

inline Mat4 Mat4::inverse_affine(const Mat4& m)
{
    // Upper-left 3x3 (column-major within the 4x4): element(row,col) = m[col*4+row].
    const float a = m.m[0], b = m.m[4], c = m.m[8];
    const float d = m.m[1], e = m.m[5], f = m.m[9];
    const float g = m.m[2], h = m.m[6], i = m.m[10];

    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det > -1e-8f && det < 1e-8f)
    {
        return identity(); // singular linear part (e.g. zero scale)
    }
    const float inv_det = 1.0f / det;

    // Inverse of the 3x3 (adjugate / det), as inv(row,col):
    const float i00 = (e * i - f * h) * inv_det;
    const float i01 = (c * h - b * i) * inv_det;
    const float i02 = (b * f - c * e) * inv_det;
    const float i10 = (f * g - d * i) * inv_det;
    const float i11 = (a * i - c * g) * inv_det;
    const float i12 = (c * d - a * f) * inv_det;
    const float i20 = (d * h - e * g) * inv_det;
    const float i21 = (b * g - a * h) * inv_det;
    const float i22 = (a * e - b * d) * inv_det;

    Mat4 out = identity();
    // Store the inverse 3x3 column-major: out.m[col*4+row] = inv(row,col).
    out.m[0] = i00; out.m[4] = i01; out.m[8] = i02;
    out.m[1] = i10; out.m[5] = i11; out.m[9] = i12;
    out.m[2] = i20; out.m[6] = i21; out.m[10] = i22;

    // Translation: t' = -inv3x3 * t, where t = (m[12], m[13], m[14]).
    const float tx = m.m[12], ty = m.m[13], tz = m.m[14];
    out.m[12] = -(i00 * tx + i01 * ty + i02 * tz);
    out.m[13] = -(i10 * tx + i11 * ty + i12 * tz);
    out.m[14] = -(i20 * tx + i21 * ty + i22 * tz);
    return out;
}

} // namespace mosaic::foundation::math
