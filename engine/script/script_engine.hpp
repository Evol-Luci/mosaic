#pragma once

#include <string>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
// Result of running a script: ok=false carries a human-readable error; output is captured stdout.
struct ScriptResult
{
    bool        ok = false;
    std::string error;  // empty when ok
    std::string output; // captured sys.stdout/stderr
};

// A neutral embedded-scripting engine. The concrete backend (CPython/pybind11) lives in
// modules/script_python and never leaks its types above this interface, so engine/foundation
// stay language-agnostic.
class ScriptEngine
{
public:
    virtual ~ScriptEngine() = default;
    // Run a script in this engine's own global namespace, capturing stdout + errors. Never throws.
    virtual ScriptResult run_string(const std::string& code) = 0;

    // Make get_component/set_component available to scripts, bound to this world + component set.
    // The engine holds non-owning pointers; the caller keeps them alive across run_string calls.
    virtual void bind_world(mosaic::engine::World& world,
                            mosaic::engine::reflection::ComponentReflection& types) = 0;

    // Inject a value into the script's global namespace (e.g. an entity handle), marshaled from
    // the #10 Value DOM.
    virtual void set_global(const std::string& name,
                            const mosaic::foundation::serialization::Value& value) = 0;
};
} // namespace mosaic::engine::script
