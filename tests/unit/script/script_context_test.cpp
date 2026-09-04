#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_context.hpp"

using mosaic::engine::script::SnapshotContext;
using mosaic::foundation::EntityID;
using mosaic::foundation::serialization::Value;

TEST_CASE("SnapshotContext reads the snapshot and buffers writes", "[script]")
{
    SnapshotContext::Snapshot snap;
    snap[1]["Health"] = Value(static_cast<std::int64_t>(42));

    SnapshotContext ctx(snap);
    auto got = ctx.get(EntityID(1), "Health");
    REQUIRE(got.has_value());
    REQUIRE(ctx.get(EntityID(1), "Missing").has_value() == false);
    REQUIRE(ctx.get(EntityID(2), "Health").has_value() == false);

    REQUIRE(ctx.set(EntityID(1), "Health", Value(static_cast<std::int64_t>(7))));
    REQUIRE(ctx.set(EntityID(1), "Mana", Value(static_cast<std::int64_t>(3))));
    REQUIRE(ctx.writes().size() == 2);
    REQUIRE(ctx.writes()[0].entity == 1);
    REQUIRE(ctx.writes()[0].comp == "Health");
    REQUIRE(ctx.writes()[1].comp == "Mana");
}
