#pragma once

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
// One recorded breakpoint hit: the line and a snapshot of the frame's local variables.
struct BreakpointHit
{
    int line = 0;
    std::vector<std::pair<std::string, mosaic::foundation::serialization::Value>> locals;
};

// Tracepoint-inspection debugger state: a set of breakpoint lines the backend's sys.settrace hook
// checks, and a log of hits (non-blocking — execution never pauses). Neutral: no Python types.
class ScriptDebugger
{
public:
    void set_breakpoint(int line) { breakpoints_.insert(line); }
    void clear_breakpoint(int line) { breakpoints_.erase(line); }
    void clear_breakpoints() { breakpoints_.clear(); }
    bool has_breakpoint(int line) const { return breakpoints_.count(line) != 0; }
    const std::set<int>& breakpoints() const { return breakpoints_; }

    void record_hit(BreakpointHit hit) { hits_.push_back(std::move(hit)); }
    const std::vector<BreakpointHit>& hits() const { return hits_; }
    void clear_hits() { hits_.clear(); }

private:
    std::set<int>              breakpoints_;
    std::vector<BreakpointHit> hits_;
};
} // namespace mosaic::engine::script
