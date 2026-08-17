#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "foundation/logging/log.hpp"

using mosaic::foundation::log::Level;
using mosaic::foundation::log::Logger;
using mosaic::foundation::log::Record;
using mosaic::foundation::log::Sink;

namespace
{
// Appends each record's message as one whole entry under its own lock. If the
// Logger did not serialize dispatch, concurrent writers could corrupt the
// vector (a data race) or split a record; the count and the intact strings
// prove neither happened.
class CountingSink : public Sink
{
public:
    std::vector<std::string> lines;

    void write(const Record& record) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines.emplace_back(record.message);
    }

private:
    std::mutex mutex_;
};
} // namespace

TEST_CASE("concurrent logging loses no records and tears no lines", "[logging]")
{
    Logger logger;
    auto sink = std::make_shared<CountingSink>();
    logger.add_sink(sink);
    logger.set_min_level(Level::Trace);

    constexpr int thread_count = 8;
    constexpr int per_thread = 500;
    const std::string payload = "intact-line-payload";

    std::vector<std::thread> threads;
    for (int t = 0; t < thread_count; ++t)
    {
        threads.emplace_back([&logger, &payload] {
            for (int i = 0; i < per_thread; ++i)
            {
                logger.log(Level::Info, "concurrency", payload);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }

    REQUIRE(sink->lines.size() == static_cast<std::size_t>(thread_count * per_thread));
    for (const auto& line : sink->lines)
    {
        REQUIRE(line == payload); // no torn/interleaved content
    }
}
