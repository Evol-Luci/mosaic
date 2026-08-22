#pragma once

namespace mosaic::foundation::math
{
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

inline bool operator==(const Vec2& a, const Vec2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vec2& a, const Vec2& b) { return !(a == b); }

template <class V>
void reflect(V& visitor, Vec2& v)
{
    visitor.member("x", v.x);
    visitor.member("y", v.y);
}
} // namespace mosaic::foundation::math
