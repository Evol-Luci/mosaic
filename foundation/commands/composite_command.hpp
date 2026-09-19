#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "foundation/commands/command.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::foundation::commands
{

// Wraps several Commands as one atomic undo step: execute() runs each sub-command in
// order, undo() reverses in reverse order (undoing the last-applied change first). Not
// tool-specific -- any caller needing N mutations to revert as a single "undo" call
// (e.g. an MCP bulk-edit tool touching many entities) constructs one of these instead
// of pushing N separate commands onto the CommandStack.
class CompositeCommand final : public Command
{
public:
    CompositeCommand(std::string id, std::vector<std::unique_ptr<Command>> commands)
        : id_(std::move(id)), commands_(std::move(commands))
    {
    }

    std::string id() const override { return id_; }

    void execute() override
    {
        for (auto& c : commands_) { c->execute(); }
    }

    void undo() override
    {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) { (*it)->undo(); }
    }

    // Projects every sub-command's own serialize() into an array -- an agent/human
    // inspecting the transaction log sees the full batch, not an opaque blob.
    serialization::Value parameters() const override
    {
        serialization::Array arr;
        for (const auto& c : commands_) { arr.push_back(c->serialize()); }
        return serialization::Value{std::move(arr)};
    }

private:
    std::string id_;
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace mosaic::foundation::commands
