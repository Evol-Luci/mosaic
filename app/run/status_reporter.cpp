#include "app/run/status_reporter.hpp"

#include <iostream>

#include <nlohmann/json.hpp>

#include "foundation/logging/log.hpp"

namespace mosaic::app
{

JsonStatusReporter::JsonStatusReporter(std::ostream& out) : out_(out) {}
JsonStatusReporter::JsonStatusReporter() : out_(std::cout) {}

void JsonStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    out_ << nlohmann::json{{"event", "progress"}, {"tick", tick},
                           {"elapsed_s", elapsed_s}}
                .dump()
         << '\n';
    mosaic::foundation::log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void JsonStatusReporter::summary(const RunSummary& s)
{
    out_ << nlohmann::json{{"event", "summary"}, {"ticks", s.ticks},
                           {"elapsed_s", s.elapsed_s}, {"fixed_dt", s.fixed_dt},
                           {"scene", s.scene}, {"stopped_by", s.stopped_by}}
                .dump()
         << '\n';
    mosaic::foundation::log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void JsonStatusReporter::error(const std::string& message)
{
    out_ << nlohmann::json{{"event", "error"}, {"message", message}}.dump() << '\n';
    mosaic::foundation::log::error("run", "{}", message);
}

void TextStatusReporter::progress(std::uint64_t tick, double elapsed_s)
{
    mosaic::foundation::log::info("run", "tick {} ({:.2f}s)", tick, elapsed_s);
}

void TextStatusReporter::summary(const RunSummary& s)
{
    mosaic::foundation::log::info("run", "done: {} ticks in {:.2f}s (stopped by {})", s.ticks,
              s.elapsed_s, s.stopped_by);
}

void TextStatusReporter::error(const std::string& message)
{
    mosaic::foundation::log::error("run", "{}", message);
}

} // namespace mosaic::app
