#pragma once

#include <cstdint>

namespace mosaic::engine::net
{
// Stable network identity of a replicated entity (0 = unset).
using NetId = std::uint32_t;

// Server-side marker: tag an entity to replicate, with its stable net id.
// Reflected + registered as "NetReplicated" (authorable in scenes/inspector).
// The marker itself is NOT replicated — the net id is the snapshot key. N4
// formalizes net-id assignment; for now the id is set by whoever creates the
// entity.
struct NetReplicated
{
    std::uint32_t id = 0;
};

template <class V>
void reflect(V& visitor, NetReplicated& n)
{
    visitor.member("id", n.id);
}
} // namespace mosaic::engine::net
