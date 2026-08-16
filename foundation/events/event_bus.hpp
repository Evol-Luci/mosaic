#pragma once

#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mosaic::foundation::events
{

// In-process publish/subscribe for immutable event payloads.
//
// Events are plain value types; any copyable type may be emitted. A subscriber
// chooses its delivery discipline:
//
//   - Synchronous subscribers fire in-tick, on the emitting thread, before
//     `emit` returns. Use for handlers that must observe the change immediately.
//   - Deferred subscribers are served from a double-buffered queue: emits during
//     a tick append to a write buffer; `drain_deferred` (called once at the
//     start of the next tick) swaps buffers and delivers the read buffer in full
//     — FIFO in global emit order across all event types — then clears it. A
//     deferred subscriber therefore always observes events exactly one tick
//     after emission, never mid-tick, which removes reentrancy hazards for
//     handlers that mutate shared state.
//
// The two disciplines are independent: a synchronous and a deferred subscriber
// to the same event type each fire on their own schedule.
class EventBus
{
public:
    template <class E>
    void subscribe(std::function<void(const E&)> handler)
    {
        sync_handlers_[std::type_index(typeid(E))].push_back(
            [h = std::move(handler)](const void* payload) {
                h(*static_cast<const E*>(payload));
            });
    }

    template <class E>
    void subscribe_deferred(std::function<void(const E&)> handler)
    {
        deferred_handlers_[std::type_index(typeid(E))].push_back(
            [h = std::move(handler)](const void* payload) {
                h(*static_cast<const E*>(payload));
            });
    }

    template <class E>
    void emit(const E& event)
    {
        // Synchronous subscribers fire immediately, before emit returns.
        auto it = sync_handlers_.find(std::type_index(typeid(E)));
        if (it != sync_handlers_.end())
        {
            for (auto& handler : it->second)
            {
                handler(&event);
            }
        }

        // Deferred subscribers are served on the next drain. Enqueue a closure
        // holding a copy of the event so the payload outlives this call; the
        // deferred subscriber set is resolved at drain time. A single queue
        // across all types preserves global FIFO emit order.
        deferred_write_.push_back([this, copy = event]() { dispatch_deferred(copy); });
    }

    // Swap the double buffer and deliver every event emitted since the previous
    // drain to deferred subscribers, FIFO, then clear the buffer. Call once per
    // tick at a fixed sync point (the start of the tick).
    void drain_deferred()
    {
        deferred_read_.swap(deferred_write_);
        for (auto& deliver : deferred_read_)
        {
            deliver();
        }
        deferred_read_.clear();
    }

private:
    template <class E>
    void dispatch_deferred(const E& event)
    {
        auto it = deferred_handlers_.find(std::type_index(typeid(E)));
        if (it == deferred_handlers_.end())
        {
            return;
        }
        for (auto& handler : it->second)
        {
            handler(&event);
        }
    }

    using ErasedHandler = std::function<void(const void*)>;
    using HandlerMap = std::unordered_map<std::type_index, std::vector<ErasedHandler>>;

    HandlerMap sync_handlers_;
    HandlerMap deferred_handlers_;
    std::vector<std::function<void()>> deferred_write_;
    std::vector<std::function<void()>> deferred_read_;
};

} // namespace mosaic::foundation::events
