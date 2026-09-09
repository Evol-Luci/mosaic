#pragma once

#include <functional>
#include <utility>

#include "engine/ecs/system.hpp"
#include "engine/script/script_event.hpp"
#include "engine/ui/pointer.hpp"
#include "engine/ui/ui_button.hpp"
#include "engine/ui/ui_hierarchy.hpp"
#include "engine/ui/ui_rect.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::ui
{
// Clamp an int to a ui::Anchor (out of range -> TopLeft). Mirrors render's to_anchor.
inline Anchor button_anchor(int a)
{
    return (a >= 0 && a <= 8) ? static_cast<Anchor>(a) : Anchor::TopLeft;
}

// First stateful UI system: reads an injected pointer each tick and emits a named
// ScriptEvent when the pointer presses and releases inside the same UiButton.
class UiSystem : public mosaic::engine::ecs::System
{
public:
    explicit UiSystem(std::function<PointerState()> pointer)
        : pointer_(std::move(pointer)) {}

    void update(mosaic::engine::World& world, float /*dt*/) override
    {
        const PointerState p = pointer_ ? pointer_() : PointerState{};

        if (p.down && !was_down_)
        {
            // Press: record the first button under the pointer.
            pressed_ = mosaic::foundation::EntityID{};
            world.for_each<UiButton>(
                [&](mosaic::foundation::EntityID e, UiButton& b)
                {
                    if (pressed_ != mosaic::foundation::EntityID{}) { return; }
                    if (hit(world, e, p, b)) { pressed_ = e; }
                });
        }
        else if (!p.down && was_down_)
        {
            // Release: click if the pointer is still inside the pressed button.
            if (pressed_ != mosaic::foundation::EntityID{})
            {
                world.for_each<UiButton>(
                    [&](mosaic::foundation::EntityID e, UiButton& b)
                    {
                        if (e != pressed_ || !hit(world, e, p, b)) { return; }
                        namespace ser = mosaic::foundation::serialization;
                        ser::Table payload;
                        payload["entity"] = ser::Value(static_cast<std::uint64_t>(e.value()));
                        payload["action"] = ser::Value(std::string("click"));
                        world.events().emit(mosaic::engine::script::ScriptEvent{
                            b.event, ser::Value(std::move(payload))});
                    });
            }
            pressed_ = mosaic::foundation::EntityID{};
        }
        was_down_ = p.down;
    }

private:
    static bool hit(mosaic::engine::World& world, mosaic::foundation::EntityID e,
                    const PointerState& p, UiButton& b)
    {
        const Rect r = resolve_ui_rect(world, e, button_anchor(b.anchor), b.pos, b.size);
        return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
    }

    std::function<PointerState()> pointer_;
    bool                         was_down_ = false;
    mosaic::foundation::EntityID pressed_  = {};
};
} // namespace mosaic::engine::ui
