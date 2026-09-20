#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "engine/commands/set_component_command.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_debugger.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::studio
{
// The outcome of a validate-on-apply.
struct ScriptApplyResult
{
    enum class Kind { None, Ok, Error };
    Kind        kind = Kind::None;
    std::string message; // captured stdout on Ok (usually empty); the error text on Error
};

// Validates + applies an edited ScriptComponent source. Pure logic (no ImGui): the panel owns the
// edit buffer and calls apply(). Drives the NEUTRAL ScriptEngine via an injected factory, so Studio
// stays Python-free. apply() mirrors ScriptSystem's compile step, so a source that validates here
// compiles identically when ScriptSystem reloads it live.
class ScriptEditorModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptEditorModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    // Test-compile `source` against a throwaway engine bound to the live world; on success, write it
    // into entity e's ScriptComponent (-> ScriptSystem recompiles it live) via a SetComponentCommand
    // pushed onto `commands` -- the same generic undo/redo mechanism set_component already uses,
    // rather than a raw field write. On a compile/top-level error, return Error{error} and leave the
    // component untouched. Never throws.
    ScriptApplyResult apply(const std::string& source, mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection,
                            mosaic::foundation::EntityID e,
                            mosaic::foundation::commands::CommandStack& commands)
    {
        if (!factory_)
        {
            return {ScriptApplyResult::Kind::Error, "scripting is not available in this build"};
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        engine->bind_world(world, reflection);
        engine->set_global(
            "entity",
            mosaic::foundation::serialization::Value(static_cast<std::uint64_t>(e.value())));
        const mosaic::engine::script::ScriptResult r = engine->run_string(source);
        if (!r.ok)
        {
            return {ScriptApplyResult::Kind::Error, r.error};
        }
        const auto current = reflection.component_value(world, e, "ScriptComponent");
        if (!current)
        {
            return {ScriptApplyResult::Kind::Error, "selected entity has no ScriptComponent"};
        }
        mosaic::foundation::serialization::Value next = *current;
        next.as<mosaic::foundation::serialization::Table>()["source"] =
            mosaic::foundation::serialization::Value(source);
        auto cmd = std::make_unique<mosaic::engine::commands::SetComponentCommand>(
            world, reflection, e, "ScriptComponent", next);
        cmd->execute();
        if (!cmd->valid())
        {
            return {ScriptApplyResult::Kind::Error, "failed to apply the compiled script"};
        }
        commands.push_executed(std::move(cmd));
        return {ScriptApplyResult::Kind::Ok, r.output};
    }

    // Run `source` once with the debugger attached: compile it (defines on_update), then call
    // on_update(0) so breakpoints inside on_update fire. Fills `debugger.hits()`. Does NOT write
    // the component (this is a debug run, not an apply). Returns the compile result. Never throws.
    ScriptApplyResult debug(const std::string& source, mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection,
                            mosaic::foundation::EntityID e,
                            mosaic::engine::script::ScriptDebugger& debugger)
    {
        if (!factory_)
        {
            return {ScriptApplyResult::Kind::Error, "scripting is not available in this build"};
        }
        std::unique_ptr<mosaic::engine::script::ScriptEngine> engine = factory_();
        engine->bind_world(world, reflection);
        engine->set_global(
            "entity",
            mosaic::foundation::serialization::Value(static_cast<std::uint64_t>(e.value())));
        engine->bind_debugger(&debugger);
        debugger.clear_hits();

        const mosaic::engine::script::ScriptResult r = engine->run_string(source);
        if (!r.ok)
        {
            engine->bind_debugger(nullptr);
            return {ScriptApplyResult::Kind::Error, r.error};
        }
        // Exercise on_update once so breakpoints inside it fire (guarded: no-op if undefined).
        engine->set_global("dt", mosaic::foundation::serialization::Value(0.0));
        engine->run_string("if 'on_update' in globals(): on_update(dt)\n");
        engine->bind_debugger(nullptr);
        return {ScriptApplyResult::Kind::Ok, r.output};
    }

private:
    EngineFactory factory_;
};
} // namespace mosaic::studio
