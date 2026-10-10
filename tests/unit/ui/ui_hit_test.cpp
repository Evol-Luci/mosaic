#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "engine/ui/ui_hit.hpp"

using mosaic::engine::ui::HitCandidate;
using mosaic::engine::ui::PointerState;
using mosaic::engine::ui::Rect;
using mosaic::engine::ui::topmost_hit;
using mosaic::foundation::EntityID;

namespace
{
// Distinct ids so a test can tell WHICH candidate was chosen; EntityID{} is the nil
// "nothing was hit" answer, so no candidate may use it.
//
// Generation 1, not the raw single-argument ctor: generations start at 1 and a zero value
// is the reserved null handle, so `EntityID{n, 1}` is a real handle while `EntityID{n}`
// would build one with generation 0. These never pass through a World, but a test id that
// could not exist is a trap for whoever reuses this helper.
EntityID id(std::uint32_t n) { return EntityID{n, 1}; }

// A rect covering the top-left fifth of the screen, which the pointer {0.05, 0.05} is
// inside and {0.9, 0.9} is outside.
constexpr Rect kTopLeft{0.0f, 0.0f, 0.2f, 0.2f};
constexpr PointerState kInside{0.05f, 0.05f, false};
constexpr PointerState kOutside{0.9f, 0.9f, false};
} // namespace

TEST_CASE("an empty candidate list hits nothing", "[ui][hit]")
{
    // The empty HUD, and the first thing UiSystem will pass once per update on any scene
    // with no buttons -- which is most scenes.
    REQUIRE(topmost_hit({}, kInside) == EntityID{});
}

TEST_CASE("a single candidate is hit only when the pointer is inside it", "[ui][hit]")
{
    const std::vector<HitCandidate> one{HitCandidate{id(7), 0, kTopLeft}};
    REQUIRE(topmost_hit(one, kInside) == id(7));
    REQUIRE(topmost_hit(one, kOutside) == EntityID{});
}

TEST_CASE("the higher order wins among overlapping candidates", "[ui][hit]")
{
    // Asserted BOTH ways round. With only one ordering, a comparison written backwards
    // passes: whichever candidate happens to sit where the bug returns it.
    SECTION("higher order listed second")
    {
        const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft},
                                          HitCandidate{id(2), 5, kTopLeft}};
        REQUIRE(topmost_hit(v, kInside) == id(2));
    }
    SECTION("higher order listed first")
    {
        const std::vector<HitCandidate> v{HitCandidate{id(2), 5, kTopLeft},
                                          HitCandidate{id(1), 0, kTopLeft}};
        REQUIRE(topmost_hit(v, kInside) == id(2));
    }
}

TEST_CASE("equal orders resolve to the one later in iteration order", "[ui][hit]")
{
    // THE regression test for the bug this slice exists to fix. Today UiSystem takes the
    // FIRST button under the pointer, while the panel pass stable-sorts ascending and draws
    // in that order -- so among equal orders the entity drawn LAST is on top. First-hit and
    // last-drawn are opposite ends of the same list, which means a click currently goes to
    // the button the user CANNOT see.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 3, kTopLeft},
                                      HitCandidate{id(2), 3, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(2));
}

TEST_CASE("equal orders stay stable across three or more candidates", "[ui][hit]")
{
    // With only two candidates a std::sort / stable_sort difference can pass by luck --
    // two elements are trivially "stable" under many orderings. Three with one distinct
    // key in the middle is the shortest case that actually constrains it.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 2, kTopLeft},
                                      HitCandidate{id(2), 2, kTopLeft},
                                      HitCandidate{id(3), 2, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(3));
}

TEST_CASE("a negative order is ordered normally rather than treated as magnitude", "[ui][hit]")
{
    // `order` is an int on a reflected component, so a scene file can hold -1 -- a
    // legitimate way to push a button behind its neighbours. A comparison using unsigned
    // types or std::abs would rank -5 ABOVE 1.
    const std::vector<HitCandidate> v{HitCandidate{id(1), -5, kTopLeft},
                                      HitCandidate{id(2), 1, kTopLeft}};
    REQUIRE(topmost_hit(v, kInside) == id(2));
    const std::vector<HitCandidate> both_negative{HitCandidate{id(1), -5, kTopLeft},
                                                  HitCandidate{id(2), -1, kTopLeft}};
    REQUIRE(topmost_hit(both_negative, kInside) == id(2)); // -1 is above -5
}

TEST_CASE("only candidates under the pointer compete on order", "[ui][hit]")
{
    // A high-order button elsewhere on screen must not win. The natural wrong
    // implementation sorts first and then tests the single winner for containment, which
    // returns nothing here even though a lower-order button IS under the pointer.
    const std::vector<HitCandidate> v{
        HitCandidate{id(1), 0, kTopLeft},                          // under the pointer
        HitCandidate{id(2), 99, Rect{0.8f, 0.8f, 0.2f, 0.2f}}};    // high, but far away
    REQUIRE(topmost_hit(v, kInside) == id(1));
}

TEST_CASE("the pointer exactly on a rect edge counts as inside", "[ui][hit]")
{
    // UiSystem::hit is inclusive on both edges today (`>=` and `<=`). Keeping that is
    // deliberate, so it is asserted -- otherwise the next person reads the inclusive
    // comparison as an oversight and "fixes" it, silently shrinking every hit region by a
    // pixel on two sides.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft}};
    REQUIRE(topmost_hit(v, PointerState{0.0f, 0.0f, false}) == id(1));   // top-left corner
    REQUIRE(topmost_hit(v, PointerState{0.2f, 0.2f, false}) == id(1));   // bottom-right
    REQUIRE(topmost_hit(v, PointerState{0.2f, 0.0f, false}) == id(1));   // top-right
    REQUIRE(topmost_hit(v, PointerState{0.20001f, 0.1f, false}) == EntityID{}); // just past
}

TEST_CASE("a zero-size button is hit only at its exact corner", "[ui][hit]")
{
    // A collapsed button (size {0,0}) has a one-point hit region rather than none, because
    // the comparison is inclusive. Asserted rather than left undefined: a scene that
    // animates a button's size to zero should not have it swallow a click from the middle
    // of the screen, and should not be a special case either.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, Rect{0.5f, 0.5f, 0.0f, 0.0f}}};
    REQUIRE(topmost_hit(v, PointerState{0.5f, 0.5f, false}) == id(1));
    REQUIRE(topmost_hit(v, PointerState{0.51f, 0.5f, false}) == EntityID{});
}

TEST_CASE("a pointer that is not present hits nothing", "[ui][hit]")
{
    // The hole the `if (!pointer_)` guard in UiSystem did NOT close. That guard handles an
    // EMPTY provider, but every real provider expresses "there is no pointer" as a ZEROED
    // PointerState -- and {0, 0} is a real position inside any button anchored TopLeft at
    // {0,0}, which is exactly what a default-constructed UiButton is (pos {0,0}, size
    // {0.1,0.1}).
    //
    // So headless `mosaic run` -- where glfw_window is never created and the provider
    // returns {} every tick -- would hover a default-positioned button forever, in a
    // process with no pointer at all. And in the editor, moving the cursor off the viewport
    // onto the Inspector clamps the position to the panel edge with `hovered` false, so an
    // edge-anchored button would stay lit while the user works elsewhere.
    //
    // `inside` is how a provider says so, and this is where it is honoured: no pointer
    // means nothing is hit, whatever the coordinates happen to be.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft}};
    PointerState absent = kInside;        // coordinates squarely inside the candidate
    absent.inside = false;
    REQUIRE(topmost_hit(v, absent) == EntityID{});
    // And a zeroed state, which is what the real providers actually return.
    PointerState zeroed{};
    zeroed.inside = false;
    REQUIRE(topmost_hit(v, zeroed) == EntityID{});
}

TEST_CASE("the pointer's down flag does not affect what is hit", "[ui][hit]")
{
    // topmost_hit answers "what is under the pointer", never "was it pressed". Keeping the
    // button state out of this function is what lets hover and press share it.
    const std::vector<HitCandidate> v{HitCandidate{id(1), 0, kTopLeft}};
    REQUIRE(topmost_hit(v, PointerState{0.05f, 0.05f, true}) == id(1));
    REQUIRE(topmost_hit(v, PointerState{0.05f, 0.05f, false}) == id(1));
}
