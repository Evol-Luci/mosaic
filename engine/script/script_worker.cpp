#include "engine/script/script_worker.hpp"

#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{

ScriptWorker::ScriptWorker(EngineFactory factory, ScriptHost host)
    : factory_(std::move(factory)), host_(std::move(host))
{
    worker_host_ = host_;
    // Record instead of applying. Answer from the CURRENT job's snapshot: body
    // creation needs a RigidBody plus one of the three collider components, so
    // checking RigidBody alone would report True for an entity that never gets a body.
    worker_host_.apply_impulse = [this](std::uint64_t entity,
                                        mosaic::foundation::math::Vec3 impulse,
                                        mosaic::foundation::math::Vec3 point) -> bool
    {
        if (!host_.apply_impulse) { return false; }
        if (current_job_ == nullptr) { return false; }
        const auto ent = current_job_->snapshot.find(entity);
        if (ent == current_job_->snapshot.end())
        {
            // Not a scripted entity, so it is absent from the snapshot and we cannot
            // tell whether it has a body. Buffer it and let the main-thread replay be
            // the authority -- make_physics_apply_impulse re-checks has_body there and
            // drops it if there is none. Answering False here instead would make a
            // controller script that pushes some OTHER object work single-threaded and
            // silently do nothing under --script-thread.
            pending_impulses_.push_back(DeferredImpulse{entity, impulse, point});
            return true;
        }
        // In the snapshot, so we can answer exactly. Body creation needs a Transform, a
        // RigidBody and one of the three collider components -- PhysicsSystem::update
        // bails without a Transform, so omitting it would answer True for an entity the
        // main thread will refuse.
        if (ent->second.find("Transform") == ent->second.end()) { return false; }
        if (ent->second.find("RigidBody") == ent->second.end()) { return false; }
        const bool has_collider = ent->second.find("Collider") != ent->second.end()
                               || ent->second.find("SphereCollider") != ent->second.end()
                               || ent->second.find("CapsuleCollider") != ent->second.end();
        if (!has_collider) { return false; }
        pending_impulses_.push_back(DeferredImpulse{entity, impulse, point});
        return true;
    };
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

ScriptWorker::WorkerResult ScriptWorker::collect()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (!pending_ && !executing_ && !result_ready_) { return {}; } // nothing in flight
    cv_.wait(lock, [this] { return result_ready_ || stop_; });
    result_ready_ = false;
    return std::move(result_);
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
            executing_ = true;
        }
        WorkerResult result = execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_       = std::move(result);
            result_ready_ = true;
            executing_ = false;
        }
        cv_.notify_all();
    }
}

ScriptWorker::WorkerResult ScriptWorker::execute(const Job& job)
{
    current_job_ = &job;
    pending_impulses_.clear();
    SnapshotContext ctx(job.snapshot); // reads snapshot, buffers writes
    for (const ScriptRef& ref : job.scripts)
    {
        Entry& entry = engines_[ref.entity];
        if (!entry.engine || entry.source != ref.source)
        {
            entry.engine = factory_();
            entry.source = ref.source;
            entry.engine->bind_context(ctx);
            entry.engine->bind_host(worker_host_);
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
    WorkerResult out;
    out.writes   = std::vector<SnapshotContext::Write>(ctx.writes());
    out.impulses = std::move(pending_impulses_);
    pending_impulses_.clear();
    current_job_ = nullptr;
    return out;
}

} // namespace mosaic::engine::script
