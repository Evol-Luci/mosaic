#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_order_check.hpp"
#include "studio/order_warnings.hpp"

using mosaic::engine::ui::OrderDisagreement;
using mosaic::foundation::EntityID;
using mosaic::studio::OrderWarningModel;

namespace
{
// Generation 1, index 7 -- a real live handle shape rather than a raw 0, which is the
// reserved nil id and would make a key collision with "no entity" invisible.
const EntityID kEntity{7, 1};

OrderDisagreement finding(int button_order, int panel_order)
{
    return OrderDisagreement{kEntity, button_order, panel_order};
}
} // namespace

TEST_CASE("a newly appeared disagreement is emitted once", "[studio][order]")
{
    // The first frame it exists, a user must hear about it; the second frame they must not,
    // or an every-frame check warns sixty times a second.
    OrderWarningModel m;

    const auto first = m.emit({finding(5, 2)});
    REQUIRE(first.size() == 1);
    REQUIRE(first[0].button_order == 5);
    REQUIRE(first[0].panel_order == 2);

    REQUIRE(m.emit({finding(5, 2)}).empty());
}

TEST_CASE("a disagreement whose orders changed is emitted again", "[studio][order]")
{
    // The numbers are part of the identity. Keyed on the entity alone, the model would warn
    // once about 5-vs-2 and then stay silent forever -- including about 5-vs-9, which is a
    // different mistake with a different fix.
    OrderWarningModel m;
    REQUIRE(m.emit({finding(5, 2)}).size() == 1);

    const auto again = m.emit({finding(5, 9)});
    REQUIRE(again.size() == 1);
    REQUIRE(again[0].panel_order == 9);
}

TEST_CASE("a disagreement that disappears and returns is emitted again", "[studio][order]")
{
    // The user fixed it, then reintroduced it. A set that only ever grew would be silent the
    // second time, which is the moment the warning is most useful.
    OrderWarningModel m;
    REQUIRE(m.emit({finding(5, 2)}).size() == 1);
    REQUIRE(m.emit({}).empty()); // fixed -- nothing to say
    REQUIRE(m.emit({finding(5, 2)}).size() == 1);
}

TEST_CASE("two entities with identical orders are tracked independently", "[studio][order]")
{
    // A key that dropped the entity would report the first button and then silently swallow
    // every other button in the scene that made the same mistake.
    OrderWarningModel       m;
    const OrderDisagreement a{EntityID{7, 1}, 5, 2};
    const OrderDisagreement b{EntityID{8, 1}, 5, 2};

    REQUIRE(m.emit({a}).size() == 1);

    const auto second = m.emit({a, b});
    REQUIRE(second.size() == 1);
    REQUIRE(second[0].e == EntityID{8, 1});
}

TEST_CASE("a recycled entity index does not inherit a warning", "[studio][order]")
{
    // Same index, later generation: a destroyed button's successor is a different entity and
    // must be reported on its own. The key uses EntityID::value(), which carries the
    // generation in its high 32 bits, so this works only because the generation is in there.
    OrderWarningModel m;
    REQUIRE(m.emit({OrderDisagreement{EntityID{7, 1}, 5, 2}}).size() == 1);

    const auto recycled = m.emit({OrderDisagreement{EntityID{7, 2}, 5, 2}});
    REQUIRE(recycled.size() == 1);
    REQUIRE(recycled[0].e.generation() == 2);
}

TEST_CASE("an empty world emits nothing", "[studio][order]")
{
    // The state the editor is in on almost every frame of its life.
    OrderWarningModel m;
    REQUIRE(m.emit({}).empty());
}
