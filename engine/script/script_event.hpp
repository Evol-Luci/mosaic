#pragma once

#include <string>

#include "foundation/serialization/value.hpp"

namespace mosaic::engine::script
{
// A dynamic, named event scripts emit() and receive via on_event(name, payload).
// Rides the World event bus (deferred: delivered exactly one tick later, FIFO).
struct ScriptEvent
{
    std::string                              name;
    mosaic::foundation::serialization::Value payload; // null when omitted
};
} // namespace mosaic::engine::script
