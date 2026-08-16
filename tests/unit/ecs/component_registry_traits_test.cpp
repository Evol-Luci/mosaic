#include <type_traits>

#include "engine/ecs/component_registry.hpp"

// ComponentRegistry holds a non-owning back-pointer to the EntityRegistry it was
// constructed from, so its address must stay stable: copying or moving one would
// leave the copy pointing at the original constructor argument (dangling once that
// EntityRegistry, or the enclosing object, goes away). Guard the invariant at
// compile time so no future caller can reintroduce the bug.
static_assert(!std::is_copy_constructible_v<mosaic::engine::ecs::ComponentRegistry>,
              "ComponentRegistry must be non-copyable (non-owning back-pointer)");
static_assert(!std::is_copy_assignable_v<mosaic::engine::ecs::ComponentRegistry>,
              "ComponentRegistry must be non-copy-assignable (non-owning back-pointer)");
static_assert(!std::is_move_constructible_v<mosaic::engine::ecs::ComponentRegistry>,
              "ComponentRegistry must be non-movable (non-owning back-pointer)");
static_assert(!std::is_move_assignable_v<mosaic::engine::ecs::ComponentRegistry>,
              "ComponentRegistry must be non-move-assignable (non-owning back-pointer)");
