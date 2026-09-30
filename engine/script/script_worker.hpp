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

#include "foundation/math/vec3.hpp"
#include "engine/script/script_context.hpp"
#include "engine/script/script_engine.hpp"
#include "engine/script/script_host.hpp"

namespace mosaic::engine::script
{
// A physics impulse a script asked for on the worker thread, recorded rather than
// applied. The main thread replays it through the neutral ScriptHost callback -- the
// worker must never touch the live physics world, because it runs while the main
// thread may be inside PhysicsSystem's step.
struct DeferredImpulse
{
    std::uint64_t                  entity = 0;
    mosaic::foundation::math::Vec3 impulse{};
    mosaic::foundation::math::Vec3 point{};
};

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

    // What one job produced. Two independent channels: a tick may buffer writes with
    // no impulses, impulses with no writes, or both.
    struct WorkerResult
    {
        std::vector<SnapshotContext::Write> writes;
        std::vector<DeferredImpulse>        impulses;
    };

    void dispatch(Job job);                          // enqueue (non-blocking)
    WorkerResult collect();   // wait for the job, return its writes + impulses
    bool has_pending() const;

private:
    void run();                                      // worker loop
    WorkerResult execute(const Job& job); // on worker thread

    EngineFactory factory_;
    ScriptHost    host_;

    // host_ with apply_impulse replaced by the recording version. Bound into every
    // engine instead of host_, so a script's push is buffered rather than applied.
    ScriptHost worker_host_;
    // Worker-thread-only state, valid for the duration of one execute() call.
    const Job*                   current_job_ = nullptr;
    std::vector<DeferredImpulse> pending_impulses_;

    // Engines live on the worker thread only.
    struct Entry { std::unique_ptr<ScriptEngine> engine; std::string source; };
    std::unordered_map<std::uint64_t, Entry> engines_;

    mutable std::mutex      mutex_;
    std::condition_variable cv_;
    std::optional<Job>      job_;         // pending input
    WorkerResult result_;
    bool                    result_ready_ = false;
    bool                    pending_      = false;
    // True from the moment a job is dequeued until its result is published. Without
    // it, collect() cannot distinguish "executing right now" from "nothing was ever
    // dispatched", because pending_ is cleared at dequeue and result_ready_ is not
    // set until execute() returns -- and execute() runs unlocked for as long as
    // Python takes.
    bool                    executing_    = false;
    bool                    stop_         = false;
    std::thread             thread_;
};
} // namespace mosaic::engine::script
