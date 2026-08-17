#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace mosaic::app
{

// Outcome of a run, reported once at the end.
struct RunSummary
{
    std::uint64_t ticks;      // total fixed ticks executed
    double        elapsed_s;  // wall-clock (or scripted) seconds elapsed
    double        fixed_dt;   // the RuntimeLoop fixed timestep
    std::string   scene;      // scene file loaded, or "" if none
    std::string   stopped_by; // "signal" | "duration" | "ticks"
};

// Sink for run status. The frame pump calls progress() periodically and
// summary() once; main calls error() on a startup failure.
class StatusReporter
{
public:
    virtual ~StatusReporter() = default;
    virtual void progress(std::uint64_t tick, double elapsed_s) = 0;
    virtual void summary(const RunSummary&) = 0;
    virtual void error(const std::string& message) = 0;
};

// Writes one JSON object per line to the given stream (default std::cout) and
// mirrors a human-readable line through the logging facility.
class JsonStatusReporter : public StatusReporter
{
public:
    explicit JsonStatusReporter(std::ostream& out);
    JsonStatusReporter();
    void progress(std::uint64_t tick, double elapsed_s) override;
    void summary(const RunSummary&) override;
    void error(const std::string& message) override;

private:
    std::ostream& out_;
};

// Human-readable only: routes everything through the logging facility, no JSON.
class TextStatusReporter : public StatusReporter
{
public:
    void progress(std::uint64_t tick, double elapsed_s) override;
    void summary(const RunSummary&) override;
    void error(const std::string& message) override;
};

} // namespace mosaic::app
