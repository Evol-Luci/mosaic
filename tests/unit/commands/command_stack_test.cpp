#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/commands/command.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CommandStack;
namespace ser = mosaic::foundation::serialization;

namespace
{
// A command that appends its tag to a shared trace on execute and removes it on
// undo, so a test can observe execution order and undo inversion.
class AppendCommand final : public Command
{
public:
    AppendCommand(std::vector<std::string>& trace, std::string tag)
        : trace_(trace), tag_(std::move(tag))
    {
    }

    std::string id() const override { return "test.append"; }
    void execute() override { trace_.push_back(tag_); }
    void undo() override { trace_.pop_back(); }

private:
    std::vector<std::string>& trace_;
    std::string tag_;
};

// A command that carries parameters and permissions, to exercise serialize().
class SetPropertyCommand final : public Command
{
public:
    std::string id() const override { return "scene.set_property"; }
    void execute() override {}
    void undo() override {}

    ser::Value parameters() const override
    {
        ser::Table params;
        params["name"] = ser::Value{std::string{"health"}};
        params["value"] = ser::Value{std::int64_t{100}};
        return ser::Value{std::move(params)};
    }

    const std::vector<std::string>& required_permissions() const override
    {
        static const std::vector<std::string> perms{"scene.write"};
        return perms;
    }
};
} // namespace

TEST_CASE("execute runs the command and records it on the transaction log", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    stack.execute(std::make_unique<AppendCommand>(trace, "a"));
    stack.execute(std::make_unique<AppendCommand>(trace, "b"));

    REQUIRE(trace == std::vector<std::string>{"a", "b"});
    REQUIRE(stack.undo_depth() == 2);
    REQUIRE(stack.redo_depth() == 0);
}

TEST_CASE("undo inverts commands in LIFO order, preserving log ordering", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    stack.execute(std::make_unique<AppendCommand>(trace, "a"));
    stack.execute(std::make_unique<AppendCommand>(trace, "b"));

    REQUIRE(stack.undo());
    REQUIRE(trace == std::vector<std::string>{"a"});
    REQUIRE(stack.undo());
    REQUIRE(trace.empty());

    // Nothing left to undo.
    REQUIRE_FALSE(stack.can_undo());
    REQUIRE_FALSE(stack.undo());
}

TEST_CASE("redo re-executes the last undone command", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    stack.execute(std::make_unique<AppendCommand>(trace, "a"));
    stack.undo();
    REQUIRE(trace.empty());

    REQUIRE(stack.redo());
    REQUIRE(trace == std::vector<std::string>{"a"});
    REQUIRE(stack.undo_depth() == 1);
    REQUIRE(stack.redo_depth() == 0);
}

TEST_CASE("executing a new command clears the redo stack", "[commands]")
{
    std::vector<std::string> trace;
    CommandStack stack;

    stack.execute(std::make_unique<AppendCommand>(trace, "a"));
    stack.undo();
    REQUIRE(stack.can_redo());

    stack.execute(std::make_unique<AppendCommand>(trace, "b"));
    REQUIRE_FALSE(stack.can_redo());
    REQUIRE_FALSE(stack.redo());
    REQUIRE(trace == std::vector<std::string>{"b"});
}

TEST_CASE("serialize projects id, parameters, and required permissions", "[commands]")
{
    SetPropertyCommand cmd;
    ser::Value serialized = cmd.serialize();

    REQUIRE(serialized.is<ser::Table>());
    const ser::Table& table = serialized.as<ser::Table>();

    const ser::Value* id = table.find("id");
    REQUIRE(id != nullptr);
    REQUIRE(id->is<std::string>());
    REQUIRE(id->as<std::string>() == "scene.set_property");

    const ser::Value* params = table.find("parameters");
    REQUIRE(params != nullptr);
    REQUIRE(params->is<ser::Table>());
    const ser::Value* name = params->as<ser::Table>().find("name");
    REQUIRE(name != nullptr);
    REQUIRE(name->as<std::string>() == "health");

    const ser::Value* perms = table.find("required_permissions");
    REQUIRE(perms != nullptr);
    REQUIRE(perms->is<ser::Array>());
    REQUIRE(perms->as<ser::Array>().size() == 1);
    REQUIRE(perms->as<ser::Array>()[0].as<std::string>() == "scene.write");
}
