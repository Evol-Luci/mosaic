#pragma once

#include <cstdint>

namespace mosaic::engine::ui
{

// WHY THIS EXISTS: counting beats timing. U8d's job was to decide whether the
// `parent -> children` scan in ui_layout.hpp needs a cache, and the backlog's case for one
// is stated entirely in COUNTS -- "~12,000 visits, 120 sorts and 240 heap allocations per
// frame" for a 30-child stack in a world of 100 UiParent entities. Those are deterministic
// and exactly reproducible. Wall time on this machine is not: there is no /usr/bin/time and
// no bc here, so the usual shell timing idiom prints a plausible `0.00s` instead of failing,
// and even a correct harness gives a debug-build number with a noise floor wider than the
// effect being argued about.
//
// So the committed artifact is a COUNT, not a millisecond figure:
// `tests/unit/ui/ui_layout_cost_test.cpp` asserts the exact visit/sort/allocation counts the
// layout path performs at N = 16, 64 and 256 children, and those assertions move the instant
// anyone changes the algorithm's shape. Timing is corroboration only, and is reported with
// its build type and its spread.
//
// WHAT IT COSTS THE ENGINE, stated rather than hidden. `visits` is incremented once inside
// stack_children's for_each body -- the only counter in a hot loop -- and the rest are
// incremented once per call, outside any loop. One non-atomic 64-bit increment against a
// function-local static, beside a sparse-set element load and an EntityID comparison that
// the loop body already performs. Measured at N=256 (debug, gcc 16.2.1): the difference
// between the instrumented and uninstrumented build was inside the run-to-run spread, so the
// instrument did not measurably change what it measures. If a release-build profile ever
// shows otherwise, the counters are three lines to gate; that is recorded in the backlog.
//
// NOT THREAD-SAFE, deliberately. The UI layout path runs on the frame thread (the draw
// passes and the hit path in render_system.hpp / ui_system.hpp are all single-threaded) and
// an atomic counter in a loop that runs 65,536 times a frame would cost more than the thing
// it measures. A caller that parallelises layout must stop using these numbers, not make
// them atomic.
struct LayoutStats
{
    // Calls to stack_children. One per laid-out element per resolve, today.
    std::uint64_t scans = 0;
    // UiParent entities visited by stack_children's for_each. This is the O(E x P) term the
    // backlog's estimate is about.
    std::uint64_t visits = 0;
    // std::sort calls. Equal to `scans` today -- a scan sorts even when it matched nothing --
    // and counted separately so that stays a measurement rather than an assumption.
    std::uint64_t sorts = 0;
    // HEAP ALLOCATIONS inside stack_children, counted EXACTLY rather than modelled. The
    // backlog's estimate assumes 2 per scan (the `keyed` vector and the `out` vector); the
    // real number is higher, because `keyed` is filled with push_back and reallocates as it
    // grows. Counting the growth steps is what makes that visible.
    std::uint64_t allocations = 0;
    // get_component probes slot_for_child performs looking for a container component, BEFORE
    // it can conclude there is no layout at all. U8c took this from one (UiStack) to three
    // (UiStack, UiGrid, UiFlow), which is the cost every parented element in every scene now
    // pays whether or not any container exists. Counted so the "non-stack scenes pay
    // nothing" claim in the backlog can be checked instead of repeated.
    std::uint64_t container_probes = 0;
    // resolve_parent_frame entries, INCLUDING recursive ones. The multiplicity the backlog
    // puts at "4-5 times a frame" is a property of this number over the element count.
    std::uint64_t resolves = 0;
};

// One process-global counter set. A function-local static rather than a namespace-scope
// variable so the header stays include-anywhere with no .cpp and no ODR question.
inline LayoutStats& layout_stats()
{
    static LayoutStats stats;
    return stats;
}

// Take a snapshot and zero the counters, so a measurement measures one workload rather than
// everything the process has done. Returning the old value rather than taking an out
// parameter is what lets a test read "what did THAT cost" in one expression.
inline LayoutStats take_layout_stats()
{
    LayoutStats& live = layout_stats();
    const LayoutStats snapshot = live;
    live = LayoutStats{};
    return snapshot;
}

} // namespace mosaic::engine::ui
