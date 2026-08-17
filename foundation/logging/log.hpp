#pragma once

#include <atomic>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
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

    // A sink's write() must not call back into this Logger (log/add_sink/clear_sinks):
    // the dispatch mutex is non-recursive and would deadlock.
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

// The default console sink: writes "[LEVEL] category: message\n" to std::cerr
// (unbuffered, so lines survive a crash). Stream exceptions are caught and
// dropped to honour the noexcept contract.
class ConsoleSink : public Sink
{
public:
    void write(const Record& record) noexcept override
    {
        try
        {
            std::cerr << '[' << to_string(record.level) << "] " << record.category
                      << ": " << record.message << '\n';
        }
        catch (...)
        {
            // A logging sink must never throw into its caller (noexcept). If the
            // stream has exceptions enabled and fails, drop the line rather than
            // terminate.
        }
    }
};

// The process-global default logger. A function-local static: constructed on
// first use, destroyed at program exit, no static-init-order hazard.
inline Logger& default_logger()
{
    static Logger logger;
    return logger;
}

inline void set_min_level(Level level)
{
    default_logger().set_min_level(level);
}

inline void add_sink(std::shared_ptr<Sink> sink)
{
    default_logger().add_sink(std::move(sink));
}

inline void clear_sinks()
{
    default_logger().clear_sinks();
}

template <class... A>
void trace(std::string_view category, std::format_string<A...> fmt, A&&... args)
{
    default_logger().log(Level::Trace, category,
                         std::format(fmt, std::forward<A>(args)...));
}

template <class... A>
void debug(std::string_view category, std::format_string<A...> fmt, A&&... args)
{
    default_logger().log(Level::Debug, category,
                         std::format(fmt, std::forward<A>(args)...));
}

template <class... A>
void info(std::string_view category, std::format_string<A...> fmt, A&&... args)
{
    default_logger().log(Level::Info, category,
                         std::format(fmt, std::forward<A>(args)...));
}

template <class... A>
void warn(std::string_view category, std::format_string<A...> fmt, A&&... args)
{
    default_logger().log(Level::Warn, category,
                         std::format(fmt, std::forward<A>(args)...));
}

template <class... A>
void error(std::string_view category, std::format_string<A...> fmt, A&&... args)
{
    default_logger().log(Level::Error, category,
                         std::format(fmt, std::forward<A>(args)...));
}

} // namespace mosaic::foundation::log
