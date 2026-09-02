#pragma once

#include <string>

namespace mosaic::engine::script
{
// A Python behavior attached to an entity. `source` is a script that defines `on_update(dt)`;
// ScriptSystem (engine/script/script_system.hpp) runs it each tick. Reflected, so it serializes
// with the scene and appears in the inspector (a read-only string for v0.1). No Python types here.
struct ScriptComponent
{
    std::string source;
};

template <class V>
void reflect(V& visitor, ScriptComponent& c)
{
    visitor.member("source", c.source);
}

} // namespace mosaic::engine::script
