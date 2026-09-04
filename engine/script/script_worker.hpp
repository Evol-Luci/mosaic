#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "engine/script/script_context.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/script/script_host.hpp"

namespace mosaic::engine::script
{
// Runs script on_update()s on ONE dedicated thread that owns the interpreter and
// all engines. Main thread communicates only via dispatch()/collect(); it never
// touches Python. dispatch() enqueues; collect() waits for the in-flight job and
// returns its write batch. One job in flight at a time (a 1-tick pipeline).
class ScriptWorker
{
public:
    using EngineFactory = std::function<std::unique_ptr<ScriptEngine>()>;
    struct ScriptRef { std::uint64_t entity; std::string source; };
    struct Job
    {
        SnapshotContext::Snapshot snapshot;
        float                     dt = 0.0f;
        std::vector<ScriptRef>    scripts;
    };

    explicit ScriptWorker(EngineFactory factory, ScriptHost host = {});
    ~ScriptWorker();

    ScriptWorker(const ScriptWorker&) = delete;
    ScriptWorker& operator=(const ScriptWorker&) = delete;

    void dispatch(Job job);                          // enqueue (non-blocking)
    std::vector<SnapshotContext::Write> collect();   // wait for the job, return its writes
    bool has_pending() const;

private:
    void run();                                      // worker loop
    std::vector<SnapshotContext::Write> execute(const Job& job); // on worker thread

    EngineFactory factory_;
    ScriptHost    host_;

    // Engines live on the worker thread only.
    struct Entry { std::unique_ptr<ScriptEngine> engine; std::string source; };
    std::unordered_map<std::uint64_t, Entry> engines_;

    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::optional<Job>      job_;         // pending input
    std::vector<SnapshotContext::Write> result_;
    bool                    result_ready_ = false;
    bool                    pending_      = false;
    bool                    stop_         = false;
    std::thread             thread_;
};
} // namespace mosaic::engine::script
