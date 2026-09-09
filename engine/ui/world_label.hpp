#pragma once

#include <string>

#include "foundation/math/vec3.hpp"
#include "foundation/serialization/reference.hpp" // AssetID

namespace mosaic::engine::ui
{
using mosaic::foundation::math::Vec3;

// Camera-facing text floating at the entity's WorldTransform position (+ offset).
// size is the world-space line height. The entity needs a WorldTransform. font is a
// "font" AssetID (FontAtlas). Script-mutable (change `text`).
struct WorldLabel
{
    mosaic::foundation::serialization::AssetID font;
    std::string text;
    float size   = 1.0f;
    Vec3  color  = Vec3{1.0f, 1.0f, 1.0f};
    float alpha  = 1.0f;
    Vec3  offset = Vec3{0.0f, 0.0f, 0.0f};
};

template <class V>
void reflect(V& visitor, WorldLabel& l)
{
    visitor.member("font", l.font);
    visitor.member("text", l.text);
    visitor.member("size", l.size);
    visitor.member("color", l.color);
    visitor.member("alpha", l.alpha);
    visitor.member("offset", l.offset);
}
} // namespace mosaic::engine::ui
