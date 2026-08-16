#pragma once

#include <string>
#include <utility>
#include <vector>

#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::commands
{

// Every mutating action in Mosaic is a Command. Because a command carries its own
// inverse (`undo`) and a serialized form, the same primitive gives us undo/redo,
// a replayable transaction log, macro recording, scripting, and agent actions —
// all variations of one thing. A command is the *only* way state is mutated, so
// the log is the source of truth for undoable mutations.
//
// Events are separate: a command may announce its effect by emitting an event
// from its `execute` body, but events are never stored in or replayed by the
// command log (see foundation/events/event_bus.hpp).
class Command
{
public:
    virtual ~Command() = default;

    // Stable identifier for the command kind (e.g. "scene.set_property"). This is
    // the automation surface: the same id a human button and an agent call route
    // through.
    virtual std::string id() const = 0;

    // Apply the mutation.
    virtual void execute() = 0;

    // Reverse the effect of `execute`. Must restore the state that existed before
    // execute ran.
    virtual void undo() = 0;

    // The command's inputs, as a reflected/serializable value. Defaults to null
    // for parameterless commands.
    virtual serialization::Value parameters() const { return serialization::Value{}; }

    // Capabilities a caller must hold to run this command; ties into the
    // automation/agents permission model. Defaults to none.
    virtual const std::vector<std::string>& required_permissions() const
    {
        static const std::vector<std::string> none;
        return none;
    }

    // Project the command to a serializable value: { id, parameters,
    // required_permissions }. Sufficient to persist the transaction log or ship a
    // command over the automation API. Commands with non-trivial state may
    // override to add fields.
    virtual serialization::Value serialize() const
    {
        serialization::Table table;
        table["id"] = serialization::Value{id()};
        table["parameters"] = parameters();

        serialization::Array perms;
        for (const auto& permission : required_permissions())
        {
            perms.push_back(serialization::Value{permission});
        }
        table["required_permissions"] = serialization::Value{std::move(perms)};

        return serialization::Value{std::move(table)};
    }
};

} // namespace mosaic::foundation::commands
