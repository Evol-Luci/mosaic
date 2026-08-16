#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/commands/command.hpp"
#include "foundation/commands/command_stack.hpp"
#include "foundation/events/event_bus.hpp"

using mosaic::foundation::commands::Command;
using mosaic::foundation::commands::CommandStack;
using mosaic::foundation::events::EventBus;

namespace
{
struct EntityCreated { int entity; };

// A command that mutates a world (here, a list of live entity ids) and announces
// the change by emitting a *deferred* event. The event is emitted from execute
// only; undo reverses the state but announces nothing, matching RFC 0001: a
// deferred event enqueues when the command executes, not when it is undone.
class CreateEntityCommand final : public Command
{
public:
    CreateEntityCommand(EventBus& bus, std::vector<int>& live, int entity)
        : bus_(bus), live_(live), entity_(entity)
    {
    }

    std::string id() const override { return "scene.create_entity"; }

    void execute() override
    {
        live_.push_back(entity_);
        bus_.emit(EntityCreated{entity_});
    }

    void undo() override
    {
        live_.pop_back();
        // No event on undo.
    }

private:
    EventBus& bus_;
    std::vector<int>& live_;
    int entity_;
};
} // namespace

TEST_CASE("a deferred event emitted by a command is announced on the next drain", "[commands][events]")
{
    EventBus bus;
    std::vector<int> live;
    std::vector<int> announced;
    bus.subscribe_deferred<EntityCreated>([&](const EntityCreated& e) { announced.push_back(e.entity); });

    CommandStack stack;
    stack.execute(std::make_unique<CreateEntityCommand>(bus, live, 7));

    // State changed immediately; the deferred announcement waits for the drain.
    REQUIRE(live == std::vector<int>{7});
    REQUIRE(announced.empty());

    bus.drain_deferred();
    REQUIRE(announced == std::vector<int>{7});
}

TEST_CASE("undo reverses state but enqueues no deferred event", "[commands][events]")
{
    EventBus bus;
    std::vector<int> live;
    std::vector<int> announced;
    bus.subscribe_deferred<EntityCreated>([&](const EntityCreated& e) { announced.push_back(e.entity); });

    CommandStack stack;
    stack.execute(std::make_unique<CreateEntityCommand>(bus, live, 7));
    bus.drain_deferred();
    REQUIRE(announced == std::vector<int>{7});

    stack.undo();
    REQUIRE(live.empty());

    // Undo announced nothing, so the next drain delivers nothing new.
    bus.drain_deferred();
    REQUIRE(announced == std::vector<int>{7});
}

TEST_CASE("a synchronous subscriber sees a command's event during execute", "[commands][events]")
{
    EventBus bus;
    std::vector<int> live;
    int sync_count = 0;
    bus.subscribe<EntityCreated>([&](const EntityCreated&) { ++sync_count; });

    CommandStack stack;
    stack.execute(std::make_unique<CreateEntityCommand>(bus, live, 3));

    // Synchronous delivery happened in-tick, inside execute — no drain required.
    REQUIRE(sync_count == 1);
}
