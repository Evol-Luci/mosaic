#pragma once

#include <optional>
#include <string>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_context.hpp"
#include "engine/script/script_debugger.hpp"
#include "engine/script/script_host.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
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
                            const mosaic::engine::reflection::ComponentReflection& types) = 0;

    // Inject a value into the script's global namespace (e.g. an entity handle), marshaled from
    // the #10 Value DOM.
    virtual void set_global(const std::string& name,
                            const mosaic::foundation::serialization::Value& value) = 0;

    // Reads a global variable's current value out of the script's namespace, marshaled
    // through the same #10 Value DOM set_global writes with. Returns nullopt if the
    // name is absent, or if its value isn't representable in the Value DOM (e.g. a
    // function or class object).
    virtual std::optional<mosaic::foundation::serialization::Value>
        get_global(const std::string& name) = 0;

    // Calls an already-defined zero-argument function in this engine's namespace by
    // name (e.g. one declared by a prior run_string). Returns the same ScriptResult
    // shape as run_string: ok/error/captured stdout. ok=false with a descriptive error
    // if name isn't defined or isn't callable.
    virtual ScriptResult call_function(const std::string& name) = 0;

    // Bind an undoable command stack. Once bound, script set_component runs through a
    // SetComponentCommand (undoable via undo()/redo()); entity lifecycle stays direct. Non-owning;
    // the caller keeps the stack alive across run_string calls. Optional (S2 behavior without it).
    virtual void bind_commands(mosaic::foundation::commands::CommandStack& stack) = 0;

    // Bind host run-state callbacks (play/pause/stop). Optional; default no-op so
    // engines that do not support it (or are built before a host exists) still work.
    virtual void bind_host(const ScriptHost& host) { (void)host; }

    // Route get_component/set_component through a sandbox context (worker mode).
    // Optional; default no-op. Entity-lifecycle globals are NOT installed here.
    virtual void bind_context(ScriptContext& context) { (void)context; }

    // Attach a tracepoint debugger: while bound, run_string installs a non-blocking
    // sys.settrace hook that records breakpoint hits. nullptr detaches. Default no-op.
    virtual void bind_debugger(ScriptDebugger* debugger) { (void)debugger; }
};
} // namespace mosaic::engine::script
