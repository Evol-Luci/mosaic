#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command_stack.hpp"

namespace mosaic::studio
{
// One line in the console transcript.
struct ConsoleEntry
{
    enum class Kind { Input, Output, Error };
    Kind        kind;
    std::string text;
};

// Runs script strings against the live editor World, keeping a transcript. Pure logic (no ImGui):
// the ImGui panel owns the input buffer and calls run(). Drives the NEUTRAL ScriptEngine via an
// injected factory, so Studio stays Python-free. The engine is a persistent REPL — created and bound
// once, reused across runs, so state (globals) survives between commands.
class ScriptConsoleModel
{
public:
    using EngineFactory = std::function<std::unique_ptr<mosaic::engine::script::ScriptEngine>()>;

    explicit ScriptConsoleModel(EngineFactory factory = {}) : factory_(std::move(factory)) {}

    const std::vector<ConsoleEntry>& history() const { return history_; }
    void clear_history() { history_.clear(); }

    // Execute `code` against `world` (reflection + command stack bound). Lazily creates the engine
    // on first run (persistent REPL). Appends an Input echo + Output/Error entries. Empty code is a
    // no-op. A null factory (scripting unavailable) makes every run() append one 'not available'
    // Error entry and do nothing else (no engine is created).
    void run(const std::string& code, mosaic::engine::World& world,
             const mosaic::engine::reflection::ComponentReflection& reflection,
             mosaic::foundation::commands::CommandStack& commands)
    {
        if (code.empty()) { return; }
        if (!engine_)
        {
            if (!factory_)
            {
                history_.push_back({ConsoleEntry::Kind::Error,
                                    "scripting is not available in this build"});
                return;
            }
            engine_ = factory_();
            engine_->bind_world(world, reflection);
            engine_->bind_commands(commands);
        }
        history_.push_back({ConsoleEntry::Kind::Input, code});
        const mosaic::engine::script::ScriptResult r = engine_->run_string(code);
        if (!r.output.empty()) { history_.push_back({ConsoleEntry::Kind::Output, r.output}); }
        if (!r.ok) { history_.push_back({ConsoleEntry::Kind::Error, r.error}); }
    }

private:
    EngineFactory                                                  factory_;
    std::unique_ptr<mosaic::engine::script::ScriptEngine>          engine_; // persistent REPL
    std::vector<ConsoleEntry>                                      history_;
};
} // namespace mosaic::studio
