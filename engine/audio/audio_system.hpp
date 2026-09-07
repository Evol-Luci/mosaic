#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "engine/audio/audio_backend.hpp"
#include "engine/audio/audio_components.hpp"
#include "engine/ecs/system.hpp"
#include "engine/transform/transform.hpp"
#include "engine/world/world.hpp"
#include "foundation/core/entity_id.hpp"

namespace mosaic::engine::audio
{
// Drives AudioSources: starts/stops a backend voice on the source's `playing` edge,
// pushes gain/pitch changes and (for spatial sources) the entity's position, and sets
// the listener pose from the AudioListener entity's Transform. The backend is injected;
// this system depends only on the neutral interface, never on a device module. Writes
// nothing back to components; voices are transient state keyed by entity id.
class AudioSystem : public ecs::System
{
public:
    explicit AudioSystem(std::unique_ptr<AudioBackend> backend) : backend_(std::move(backend)) {}

    void update(World& world, float /*dt*/) override
    {
        if (backend_ == nullptr) { return; }

        // Listener: the first entity with AudioListener + Transform.
        bool listener_done = false;
        world.for_each<AudioListener>(
            [&](mosaic::foundation::EntityID e, AudioListener&)
            {
                if (listener_done) { return; }
                if (auto* tf = world.get_component<transform::Transform>(e))
                {
                    backend_->set_listener(ListenerState{tf->position, Vec3{0.0f, 0.0f, -1.0f},
                                                         Vec3{0.0f, 1.0f, 0.0f}});
                    listener_done = true;
                }
            });

        std::unordered_set<std::uint64_t> seen;
        world.for_each<AudioSource>(
            [&](mosaic::foundation::EntityID e, AudioSource& src)
            {
                const std::uint64_t id = e.value();
                seen.insert(id);
                Voice& st = voices_[id]; // default-constructs on first sight

                if (src.playing && !st.was_playing)
                {
                    st.id    = backend_->play(src.clip, src.gain, src.pitch, src.looping);
                    st.gain  = src.gain;
                    st.pitch = src.pitch;
                }
                else if (!src.playing && st.was_playing)
                {
                    if (st.id != 0) { backend_->stop(st.id); }
                    st.id = 0;
                }

                if (st.id != 0)
                {
                    if (src.gain != st.gain) { backend_->set_gain(st.id, src.gain); st.gain = src.gain; }
                    if (src.pitch != st.pitch) { backend_->set_pitch(st.id, src.pitch); st.pitch = src.pitch; }
                    if (src.spatial)
                    {
                        if (auto* tf = world.get_component<transform::Transform>(e))
                        {
                            backend_->set_voice_position(st.id, tf->position);
                        }
                    }
                }

                st.was_playing = src.playing;
            });

        // Prune voices for emitters that no longer exist; stop any that were playing.
        for (auto it = voices_.begin(); it != voices_.end();)
        {
            if (seen.count(it->first) != 0)
            {
                ++it;
            }
            else
            {
                if (it->second.id != 0) { backend_->stop(it->second.id); }
                it = voices_.erase(it);
            }
        }
    }

private:
    struct Voice
    {
        VoiceId id          = 0;
        bool    was_playing = false;
        float   gain        = 1.0f;
        float   pitch       = 1.0f;
    };

    std::unique_ptr<AudioBackend>            backend_;
    std::unordered_map<std::uint64_t, Voice> voices_;
};
} // namespace mosaic::engine::audio
