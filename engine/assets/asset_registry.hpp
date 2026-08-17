#pragma once

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include "engine/assets/asset_metadata.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::engine::assets
{

// Runtime asset table: AssetID -> (metadata, lazily-loaded + cached resource).
// Resources are type-erased (shared_ptr<void>); load<T> static_pointer_casts. No
// concrete resource types exist yet (rendering), so loaders are caller-supplied.
class AssetRegistry
{
public:
    using Loader = std::function<std::shared_ptr<void>()>;

    // Register metadata + a lazy loader (invoked at most once, on first load).
    // Re-registering the same uuid overwrites its entry.
    void register_asset(AssetMetadata metadata, Loader loader)
    {
        const AssetID id = metadata.uuid;
        Entry entry;
        entry.metadata = std::move(metadata);
        entry.loader = std::move(loader);
        entries_[id] = std::move(entry);
    }

    // Eager convenience: register metadata + an already-built resource.
    template <class T>
    void register_resource(AssetMetadata metadata, std::shared_ptr<T> resource)
    {
        std::shared_ptr<void> erased = std::move(resource);
        register_asset(std::move(metadata), [erased]() { return erased; });
    }

    bool contains(AssetID id) const { return entries_.find(id) != entries_.end(); }

    // Metadata by id; nullptr if unregistered.
    const AssetMetadata* metadata(AssetID id) const
    {
        auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : &it->second.metadata;
    }

    // Resolve + cache the resource. First call runs the loader and caches; later
    // calls return the cached instance. nullptr if the id is unregistered.
    template <class T>
    std::shared_ptr<T> load(AssetID id)
    {
        auto it = entries_.find(id);
        if (it == entries_.end())
        {
            return nullptr;
        }
        Entry& entry = it->second;
        if (!entry.loaded)
        {
            entry.cached = entry.loader ? entry.loader() : nullptr;
            entry.loaded = true;
        }
        return std::static_pointer_cast<T>(entry.cached);
    }

private:
    struct Entry
    {
        AssetMetadata metadata;
        Loader loader;
        std::shared_ptr<void> cached;
        bool loaded = false;
    };

    std::unordered_map<AssetID, Entry> entries_;
};

} // namespace mosaic::engine::assets
