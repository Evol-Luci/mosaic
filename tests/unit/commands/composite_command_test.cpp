#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/commands/command.hpp"
#include "foundation/commands/composite_command.hpp"
#include "foundation/serialization/value.hpp"

using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CompositeCommand;
namespace ser = mosaic::foundation::serialization;

namespace
{
// A command that appends its tag to a shared trace on execute and removes it on
// undo, so a test can observe execution/undo order -- the same idiom
// tests/unit/commands/command_stack_test.cpp already uses for CommandStack itself.
class AppendCommand final : public Command
{
public:
    AppendCommand(std::vector<std::string>& trace, std::string tag)
        : trace_(trace), tag_(std::move(tag))
    {
    }

    std::string id() const override { return "test.append." + tag_; }
    void execute() override { trace_.push_back(tag_); }
    void undo() override { trace_.pop_back(); }

private:
    std::vector<std::string>& trace_;
    std::string tag_;
};
} // namespace

TEST_CASE("CompositeCommand::execute runs every sub-command in order", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "c"));

    CompositeCommand composite("test.batch", std::move(subs));
    composite.execute();

    REQUIRE(trace == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("CompositeCommand::undo reverses in reverse order", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "c"));

    CompositeCommand composite("test.batch", std::move(subs));
    composite.execute();
    REQUIRE(trace == std::vector<std::string>{"a", "b", "c"});

    composite.undo();
    REQUIRE(trace.empty());
}

TEST_CASE("CompositeCommand::id returns the id given at construction", "[commands]")
{
    std::vector<std::unique_ptr<Command>> subs;
    CompositeCommand composite("mcp.bulk_set_field", std::move(subs));
    REQUIRE(composite.id() == "mcp.bulk_set_field");
}

TEST_CASE("CompositeCommand::parameters projects every sub-command's own serialize()", "[commands]")
{
    std::vector<std::string> trace;
    std::vector<std::unique_ptr<Command>> subs;
    subs.push_back(std::make_unique<AppendCommand>(trace, "a"));
    subs.push_back(std::make_unique<AppendCommand>(trace, "b"));

    CompositeCommand composite("test.batch", std::move(subs));
    ser::Value params = composite.parameters();

    REQUIRE(params.is<ser::Array>());
    const ser::Array& arr = params.as<ser::Array>();
    REQUIRE(arr.size() == 2);
    REQUIRE(arr[0].is<ser::Table>());
    const ser::Value* id0 = arr[0].as<ser::Table>().find("id");
    REQUIRE(id0 != nullptr);
    REQUIRE(id0->as<std::string>() == "test.append.a");
    const ser::Value* id1 = arr[1].as<ser::Table>().find("id");
    REQUIRE(id1 != nullptr);
    REQUIRE(id1->as<std::string>() == "test.append.b");
}

TEST_CASE("CompositeCommand with zero sub-commands is a safe no-op", "[commands]")
{
    std::vector<std::unique_ptr<Command>> subs;
    CompositeCommand composite("test.empty", std::move(subs));
    composite.execute(); // no crash
    composite.undo();    // no crash
    REQUIRE(composite.id() == "test.empty");
}
