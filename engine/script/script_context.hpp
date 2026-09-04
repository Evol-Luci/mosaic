#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
using Value    = mosaic::foundation::serialization::Value;
using EntityID = mosaic::foundation::EntityID;

// Sandbox seam: a script's get_component/set_component route through this
// instead of touching World, so worker-thread scripts never race the main loop.
class ScriptContext
{
public:
    virtual ~ScriptContext() = default;
    virtual std::optional<Value> get(EntityID e, std::string_view comp) = 0;
    virtual bool set(EntityID e, std::string_view comp, const Value& v) = 0;
};

// Reads an immutable snapshot; buffers writes for the main thread to apply.
class SnapshotContext : public ScriptContext
{
public:
    using Snapshot =
        std::unordered_map<std::uint64_t, std::unordered_map<std::string, Value>>;
    struct Write { std::uint64_t entity; std::string comp; Value value; };

    explicit SnapshotContext(const Snapshot& snap) : snap_(&snap) {}

    std::optional<Value> get(EntityID e, std::string_view comp) override
    {
        auto ent = snap_->find(e.value());
        if (ent == snap_->end()) { return std::nullopt; }
        auto it = ent->second.find(std::string(comp));
        if (it == ent->second.end()) { return std::nullopt; }
        return it->second;
    }

    bool set(EntityID e, std::string_view comp, const Value& v) override
    {
        writes_.push_back(Write{e.value(), std::string(comp), v});
        return true;
    }

    const std::vector<Write>& writes() const { return writes_; }

private:
    const Snapshot*    snap_;
    std::vector<Write> writes_;
};
} // namespace mosaic::engine::script
