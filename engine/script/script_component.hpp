#pragma once

#include <string>
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::script
{
// A Python behavior attached to an entity. `source` is a script that defines `on_update(dt)`;
// ScriptSystem (engine/script/script_system.hpp) runs it each tick. Reflected, so it serializes
// with the scene and appears in the inspector (a read-only string for v0.1). No Python types here.
struct ScriptComponent
{
    std::string                                 source; // inline Python (fallback)
    std::string                                 path;   // optional .py file (fallback)
    mosaic::foundation::serialization::AssetID  script; // script asset (nil = unset), takes precedence
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
    visitor.member("path", c.path);
    visitor.member("script", c.script);
}

} // namespace mosaic::engine::script
