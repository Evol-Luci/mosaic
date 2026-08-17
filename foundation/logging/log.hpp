#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace mosaic::foundation::log
{

// Severity levels, ordered ascending so the min-level filter is a `>=` compare.
enum class Level
{
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

constexpr std::string_view to_string(Level level)
{
    switch (level)
    {
    case Level::Trace: return "TRACE";
    case Level::Debug: return "DEBUG";
    case Level::Info:  return "INFO";
    case Level::Warn:  return "WARN";
    case Level::Error: return "ERROR";
    }
    return "?";
}

// The immutable value handed to a sink. `message` is already formatted; both
// string views are valid only for the duration of the sink's `write` call.
struct Record
{
    Level            level;
    std::string_view category;
    std::string_view message;
};

// A destination for log records. `write` is noexcept by contract: logging can
// happen during error handling, so a sink must never throw into the caller.
class Sink
{
public:
    virtual ~Sink() = default;
    virtual void write(const Record&) noexcept = 0;
};

// The complete logging core: a set of sinks, a minimum level, and a mutex that
// serializes dispatch so concurrent logs never produce torn/interleaved output.
//
// v0.1 exposes only the process-global free-function surface below; this class
// is instantiable (and directly unit-tested) so a future version can promote it
// to a public, injectable API without changing any call site.
class Logger
{
public:
    void add_sink(std::shared_ptr<Sink> sink)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.push_back(std::move(sink));
    }

    void clear_sinks()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks_.clear();
    }

    void set_min_level(Level level)
    {
        min_level_.store(level, std::memory_order_relaxed);
    }

    void log(Level level, std::string_view category, std::string_view message)
    {
        // Drop below-threshold messages without taking the lock.
        if (level < min_level_.load(std::memory_order_relaxed))
        {
            return;
        }
        const Record record{level, category, message};
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& sink : sinks_)
        {
            sink->write(record);
        }
    }

private:
    std::vector<std::shared_ptr<Sink>> sinks_;
    std::atomic<Level>                 min_level_{Level::Info};
    std::mutex                         mutex_;
};

} // namespace mosaic::foundation::log
