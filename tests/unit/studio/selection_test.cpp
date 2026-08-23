#include <catch2/catch_test_macros.hpp>

#include "studio/selection.hpp"

using mosaic::foundation::EntityID;
using mosaic::studio::SelectionModel;

TEST_CASE("SelectionModel starts with nothing selected", "[studio]")
{
    SelectionModel sel;
    REQUIRE_FALSE(sel.has_selection());
    REQUIRE_FALSE(static_cast<bool>(sel.selected()));
}

TEST_CASE("SelectionModel select then query", "[studio]")
{
    SelectionModel sel;
    const EntityID a{3, 1};
    sel.select(a);
    REQUIRE(sel.has_selection());
    REQUIRE(sel.selected() == a);
    REQUIRE(sel.is_selected(a));
    REQUIRE_FALSE(sel.is_selected(EntityID{4, 1}));
}

TEST_CASE("SelectionModel clear resets to nothing selected", "[studio]")
{
    SelectionModel sel;
    sel.select(EntityID{2, 1});
    sel.clear();
    REQUIRE_FALSE(sel.has_selection());
    REQUIRE_FALSE(sel.is_selected(EntityID{2, 1}));
}

TEST_CASE("SelectionModel selecting a different entity updates selection", "[studio]")
{
    SelectionModel sel;
    sel.select(EntityID{1, 1});
    sel.select(EntityID{2, 1});
    REQUIRE(sel.is_selected(EntityID{2, 1}));
    REQUIRE_FALSE(sel.is_selected(EntityID{1, 1}));
}
