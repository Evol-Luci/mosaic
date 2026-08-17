#include "app/run/status_reporter.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "foundation/logging/log.hpp"

namespace mosaic::app
{
namespace log = mosaic::foundation::log;

JsonStatusReporter::JsonStatusReporter(std::ostream& out) : out_(out) {}
JsonStatusReporter::JsonStatusReporter() : out_(std::cout) {}

void JsonStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    out_ << nlohmann::json{{"event", "progress"}, {"tick", tick},
                           {"elapsed_s", elapsed_s}}
                .dump()
         << '\n';
    log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void JsonStatusReporter::summary(const RunSummary& s)
{
    out_ << nlohmann::json{{"event", "summary"}, {"ticks", s.ticks},
                           {"elapsed_s", s.elapsed_s}, {"fixed_dt", s.fixed_dt},
                           {"scene", s.scene}, {"stopped_by", s.stopped_by}}
                .dump()
         << '\n';
    log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void JsonStatusReporter::error(const std::string& message)
{
    out_ << nlohmann::json{{"event", "error"}, {"message", message}}.dump() << '\n';
    log::error("run", "{}", message);
}

void TextStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void TextStatusReporter::summary(const RunSummary& s)
{
    log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void TextStatusReporter::error(const std::string& message)
{
    log::error("run", "{}", message);
}

} // namespace mosaic::app
