#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "engine/ecs/component_registry.hpp"
#include "engine/ecs/entity_registry.hpp"
#include "engine/ecs/system.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/events/event_bus.hpp"

namespace mosaic::engine
{

// The runtime container: owns the entities, their components, the active system
// set, and a per-world event channel. `tick` advances the world one step.
// Scene (de)serialization and the outer runtime loop live outside this class.
class World
{
public:
    using EntityID = mosaic::foundation::EntityID;
    using EventBus = mosaic::foundation::events::EventBus;

    World() = default;

    // Non-copyable and non-movable: components_ holds a back-pointer into
    // entities_, and the event bus captures `this` in deferred closures, so the
    // World's address must be stable for its lifetime.
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    EntityID create_entity();
    void destroy_entity(EntityID id);
    bool is_valid(EntityID id) const { return entities_.is_valid(id); }
    std::size_t entity_count() const { return entities_.alive_count(); }

    template <typename Fn>
    void for_each_entity(Fn&& fn) const { entities_.for_each(std::forward<Fn>(fn)); }

    template <typename T>
    bool add_component(EntityID id, T value) { return components_.add<T>(id, std::move(value)); }

    template <typename T>
    T* get_component(EntityID id) { return components_.get<T>(id); }

    template <typename T>
    bool has_component(EntityID id) const { return components_.has<T>(id); }

    template <typename T>
    bool remove_component(EntityID id) { return components_.remove<T>(id); }

    template <typename T, typename Fn>
    void for_each(Fn&& fn) { components_.for_each<T>(std::forward<Fn>(fn)); }

    EventBus& events() { return events_; }

    template <typename S, typename... Args>
    S& add_system(Args&&... args)
    {
        auto system = std::make_unique<S>(std::forward<Args>(args)...);
        S& ref = *system;
        systems_.push_back(std::move(system));
        return ref;
    }

    // The first registered system that is a T (dynamic_cast), or nullptr. Systems are polymorphic
    // (virtual update), so this is a safe cross-cast. O(#systems); the set is tiny.
    template <typename S>
    S* get_system()
    {
        for (std::unique_ptr<ecs::System>& sys : systems_)
        {
            if (S* s = dynamic_cast<S*>(sys.get())) { return s; }
        }
        return nullptr;
    }

    void tick(float dt);

private:
    ecs::EntityRegistry entities_;
    ecs::ComponentRegistry components_{entities_};
    EventBus events_;
    std::vector<std::unique_ptr<ecs::System>> systems_;
    std::vector<EntityID> pending_destroy_;
};

} // namespace mosaic::engine
