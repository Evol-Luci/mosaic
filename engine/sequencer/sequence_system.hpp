#pragma once

#include <cmath>

#include "engine/animation/animation_system.hpp" // apply_clip_at
#include "engine/ecs/system.hpp"
#include "engine/reflection/component_reflection.hpp"
#include "engine/script/script_event.hpp"
#include "engine/sequencer/sequence.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"
#include "foundation/serialization/value.hpp"

namespace mosaic::engine::sequencer
{
// Drives Sequence timelines: advances the master clock, samples each active clip track onto its
// target entity (reusing the animation write path), and fires event markers as the playhead
// crosses them. Markers emit a ScriptEvent, delivered to scripts' on_event one tick later.
class SequenceSystem : public ecs::System
{
public:
    explicit SequenceSystem(const reflection::ComponentReflection& types) : types_(types) {}

    void update(World& world, float dt) override
    {
        world.for_each<Sequence>(
            [&](mosaic::foundation::EntityID, Sequence& seq)
            {
                if (!seq.playing) { return; }
                const float prev = seq.time;
                seq.time += dt;
                bool wrapped = false;
                if (seq.loop && seq.duration > 0.0f && seq.time >= seq.duration)
                {
                    seq.time = std::fmod(seq.time, seq.duration);
                    wrapped = true;
                }

                // Clip tracks: sample active clips onto their target entities.
                for (const SequenceClipTrack& track : seq.clips)
                {
                    const float local = seq.time - track.start;
                    if (local < 0.0f || local > track.clip.duration) { continue; }
                    mosaic::engine::animation::apply_clip_at(
                        types_, world, mosaic::foundation::EntityID(track.target), track.clip,
                        local);
                }

                // Event markers: fire those the playhead crossed this tick.
                for (const SequenceEventTrack& marker : seq.events)
                {
                    const bool crossed = wrapped
                                             ? (marker.time > prev || marker.time <= seq.time)
                                             : (prev < marker.time && marker.time <= seq.time);
                    if (crossed)
                    {
                        world.events().emit(mosaic::engine::script::ScriptEvent{
                            marker.event, mosaic::foundation::serialization::Value{}});
                    }
                }
            });
    }

private:
    const reflection::ComponentReflection& types_;
};
} // namespace mosaic::engine::sequencer
