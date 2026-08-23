#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "foundation/logging/log.hpp"

namespace mosaic::studio
{

// One captured log line, with owned copies of the (transient) record strings.
struct ConsoleLine
{
    mosaic::foundation::log::Level level;
    std::string                    category;
    std::string                    message;
};

// A bounded, thread-safe ring buffer of captured log lines. push() may run on
// non-UI threads (Vulkan validation, jobs); snapshot() reads from the UI thread.
// Capacity-capped: the oldest line is dropped when full.
class ConsoleBuffer
{
public:
    explicit ConsoleBuffer(std::size_t capacity = 1000)
        : capacity_(capacity == 0 ? 1 : capacity)
    {
    }

    // Copies record.category / record.message (they are string_views valid only now).
    void push(const mosaic::foundation::log::Record& record)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(ConsoleLine{record.level, std::string(record.category),
                                     std::string(record.message)});
        while (lines_.size() > capacity_)
        {
            lines_.pop_front();
        }
    }

    std::vector<ConsoleLine> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<ConsoleLine>(lines_.begin(), lines_.end());
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return lines_.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.clear();
    }

private:
    mutable std::mutex      mutex_;
    std::deque<ConsoleLine> lines_;
    std::size_t             capacity_;
};

// A log::Sink that forwards records into a shared ConsoleBuffer. Held by the
// process-global default logger for the editor's lifetime. write() is noexcept and
// never calls back into the logger (honoring the Sink contract).
class ConsoleSink : public mosaic::foundation::log::Sink
{
public:
    explicit ConsoleSink(std::shared_ptr<ConsoleBuffer> buffer) : buffer_(std::move(buffer)) {}

    void write(const mosaic::foundation::log::Record& record) noexcept override
    {
        try
        {
            if (buffer_)
            {
                buffer_->push(record);
            }
        }
        catch (...)
        {
            // A sink must never throw into its caller. Drop the line on allocation
            // failure rather than terminate.
        }
    }

private:
    std::shared_ptr<ConsoleBuffer> buffer_;
};

} // namespace mosaic::studio
