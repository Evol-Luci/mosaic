#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "engine/reflection/component_reflection.hpp"
#include "engine/world/world.hpp"
#include "foundation/commands/command.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::commands
{
// Undoable "set component `name` on entity `e` to `next`". Captures the prior value (or its
// absence) at construction, so undo restores exactly what was there: the prior value if the
// component existed, or removal if the set added it. Reflection-derived via S2's
// component_value/set_value + Task 1's remove_value — works for any registered component.
class SetComponentCommand final : public mosaic::foundation::commands::Command
{
public:
    using Value = mosaic::foundation::serialization::Value;
    using EntityID = mosaic::foundation::EntityID;
    using ComponentReflection = mosaic::engine::reflection::ComponentReflection;

    SetComponentCommand(World& world, ComponentReflection& types, EntityID e, std::string name,
                        Value next)
        : world_(world), types_(types), e_(e), name_(std::move(name)), next_(std::move(next)),
          prior_(types_.component_value(world_, e_, name_)), had_component_(prior_.has_value())
    {
    }

    std::string id() const override { return "world.set_component"; }

    // Concrete-type accessor (not a Command override): true once execute() applied successfully.
    // The caller checks this before recording the command, so a rejected set is never stored.
    bool valid() const { return applies_; }

    void execute() override { applies_ = types_.set_value(world_, e_, name_, next_); }

    void undo() override
    {
        if (!applies_) { return; } // nothing was applied (never executed, or the set was rejected)
        if (had_component_) { types_.set_value(world_, e_, name_, *prior_); }
        else { types_.remove_value(world_, e_, name_); }
    }

    Value parameters() const override
    {
        mosaic::foundation::serialization::Table t;
        t["entity"] = Value{static_cast<std::int64_t>(e_.value())};
        t["component"] = Value{name_};
        t["value"] = next_;
        return Value{std::move(t)};
    }

private:
    World& world_;
    ComponentReflection& types_;
    EntityID e_;
    std::string name_;
    Value next_;
    std::optional<Value> prior_;
    bool had_component_ = false;
    bool applies_ = false;
};
} // namespace mosaic::engine::commands
