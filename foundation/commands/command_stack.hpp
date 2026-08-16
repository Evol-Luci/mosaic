#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "foundation/commands/command.hpp"

namespace mosaic::foundation::commands
{

// The undoable transaction log. Executing a command runs it and pushes it onto
// the undo stack; the redo stack is cleared, since a new action invalidates any
// previously-undone future. `undo` pops the most recent command, inverts it, and
// moves it to the redo stack; `redo` re-executes it and moves it back. Ordering
// is preserved: the undo stack, bottom-to-top, is the sequence in which commands
// were applied.
class CommandStack
{
public:
    // Run `command` and record it. Clears any redo history.
    void execute(std::unique_ptr<Command> command)
    {
        command->execute();
        undone_.clear();
        applied_.push_back(std::move(command));
    }

    bool can_undo() const noexcept { return !applied_.empty(); }
    bool can_redo() const noexcept { return !undone_.empty(); }

    std::size_t undo_depth() const noexcept { return applied_.size(); }
    std::size_t redo_depth() const noexcept { return undone_.size(); }

    // Invert the most recently applied command. Returns false if there is
    // nothing to undo.
    bool undo()
    {
        if (applied_.empty())
        {
            return false;
        }
        std::unique_ptr<Command> command = std::move(applied_.back());
        applied_.pop_back();
        command->undo();
        undone_.push_back(std::move(command));
        return true;
    }

    // Re-execute the most recently undone command. Returns false if there is
    // nothing to redo. Re-execution runs the command's `execute` again, so any
    // events its body emits are re-announced — events live in the command body,
    // not in the log.
    bool redo()
    {
        if (undone_.empty())
        {
            return false;
        }
        std::unique_ptr<Command> command = std::move(undone_.back());
        undone_.pop_back();
        command->execute();
        applied_.push_back(std::move(command));
        return true;
    }

private:
    std::vector<std::unique_ptr<Command>> applied_;
    std::vector<std::unique_ptr<Command>> undone_;
};

} // namespace mosaic::foundation::commands
