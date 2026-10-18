#pragma once

namespace mosaic::engine::vfx
{
// Per-camera bloom: light above `threshold` is blurred and added back, which is the
// payoff for the HDR target (#255, #256) -- the first thing in this renderer that needs
// values above display white.
//
// ITS OWN COMPONENT rather than three more fields on Camera, and not a RenderSystem
// setter. Camera is about the camera's optics; ScreenEffect already established where a
// post-process knob on the camera entity lives. A component serialises with the scene,
// differs per camera, and reaches the inspector through reflection -- a setter would do
// none of those.
//
// Defaults produce no visible effect, the same rule ScreenEffect states: adding this to
// a scene changes nothing until someone turns intensity up.
struct Bloom
{
    // 0 = off, and the default. Multiplies the chain at the resolve rather than being
    // baked into it, which makes the skip case safe by ARITHMETIC: at 0 the chain
    // contributes 0 whatever it contains, so a frame that skips building it cannot show
    // the previous frame's glow and no "is the chain stale?" flag has to be right.
    float intensity = 0.0f;
    // Display white. Light above this blooms, which is exactly the headroom the HDR
    // target exists to preserve. A threshold meaningfully below 1.0 would make this a
    // general glow over ordinary lit surfaces -- a different effect.
    float threshold = 1.0f;
    // The soft ramp below the threshold, and NOT optional polish. A hard
    // max(colour - threshold, 0) makes a specular highlight drifting across the
    // threshold pop in and out between frames on a moving object, which reads as a bug
    // rather than an effect. This is the ramp that makes it fade.
    float knee = 0.5f;
};

template <class V>
void reflect(V& visitor, Bloom& b)
{
    visitor.member("intensity", b.intensity);
    visitor.member("threshold", b.threshold);
    visitor.member("knee", b.knee);
}
} // namespace mosaic::engine::vfx
