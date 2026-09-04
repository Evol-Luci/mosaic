#include "engine/script/script_worker.hpp"

#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{

ScriptWorker::ScriptWorker(EngineFactory factory, ScriptHost host)
    : factory_(std::move(factory)), host_(std::move(host))
{
    thread_ = std::thread([this] { run(); });
}

ScriptWorker::~ScriptWorker()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) { thread_.join(); }
}

void ScriptWorker::dispatch(Job job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job_          = std::move(job);
        pending_      = true;
        result_ready_ = false;
    }
    cv_.notify_all();
}

std::vector<SnapshotContext::Write> ScriptWorker::collect()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!pending_ && !result_ready_) { return {}; } // nothing in flight
    cv_.wait(lock, [this] { return result_ready_ || stop_; });
    result_ready_ = false;
    return std::move(result_);
}

bool ScriptWorker::has_pending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_ || result_ready_;
}

void ScriptWorker::run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return pending_ || stop_; });
            if (stop_) { engines_.clear(); return; }
            job      = std::move(*job_);
            job_.reset();
            pending_ = false;
        }
        std::vector<SnapshotContext::Write> writes = execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_       = std::move(writes);
            result_ready_ = true;
        }
        cv_.notify_all();
    }
}

std::vector<SnapshotContext::Write> ScriptWorker::execute(const Job& job)
{
    SnapshotContext ctx(job.snapshot); // reads snapshot, buffers writes
    for (const ScriptRef& ref : job.scripts)
    {
        Entry& entry = engines_[ref.entity];
        if (!entry.engine || entry.source != ref.source)
        {
            entry.engine = factory_();
            entry.source = ref.source;
            entry.engine->bind_context(ctx);
            entry.engine->bind_host(host_);
            entry.engine->set_global(
                "entity", mosaic::foundation::serialization::Value(ref.entity));
            entry.engine->run_string(ref.source); // (re)compile: define on_update
        }
        else
        {
            entry.engine->bind_context(ctx); // rebind: fresh context each job
        }
        entry.engine->set_global(
            "dt", mosaic::foundation::serialization::Value(static_cast<double>(job.dt)));
        entry.engine->run_string("on_update(dt)");
    }
    return std::vector<SnapshotContext::Write>(ctx.writes());
}

} // namespace mosaic::engine::script
