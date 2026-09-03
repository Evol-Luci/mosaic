#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_component.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
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
    // into entity e's ScriptComponent (-> ScriptSystem recompiles it live). On a compile/top-level
    // error, return Error{error} and leave the component untouched. Never throws.
    ScriptApplyResult apply(const std::string& source, mosaic::engine::World& world,
                            const mosaic::engine::reflection::ComponentReflection& reflection,
                            mosaic::foundation::EntityID e)
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
        mosaic::engine::script::ScriptComponent* sc =
            world.get_component<mosaic::engine::script::ScriptComponent>(e);
        if (sc == nullptr)
        {
            return {ScriptApplyResult::Kind::Error, "selected entity has no ScriptComponent"};
        }
        sc->source = source;
        return {ScriptApplyResult::Kind::Ok, r.output};
    }

private:
    EngineFactory factory_;
};
} // namespace mosaic::studio
