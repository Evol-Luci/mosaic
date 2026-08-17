#include <memory>
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
// Deliberately unguarded: this sink has NO mutex of its own. The only thing
// that makes concurrent `emplace_back` safe is Logger::log's mutex, which
// serializes all sink dispatch. If that mutex were ever removed, concurrent
// writes here would be a data race — losing records (count != 4000) or
// crashing — so the assertions below are a direct test of Logger's mutex.
class CountingSink : public Sink
{
public:
    std::vector<std::string> lines;

    void write(const Record& record) noexcept override
    {
        lines.emplace_back(record.message);
    }
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
