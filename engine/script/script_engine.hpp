#pragma once

#include <string>

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
};
} // namespace mosaic::engine::script
