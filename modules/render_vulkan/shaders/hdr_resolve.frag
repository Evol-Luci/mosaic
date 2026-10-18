#version 450

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

// The bloom chain's mip 0, in its OWN descriptor set rather than a second binding in
// set 0. Both sets use the backend's one single-combined-image-sampler layout, which is
// shared with every texture in the engine; a second binding there would need a second
// layout for one caller.
//
// Sampled UNCONDITIONALLY, with no branch on bloom_intensity. That is what makes the skip
// case safe by arithmetic rather than by bookkeeping -- but it also means this image must
// be in a readable layout on EVERY frame, including frame 1 and any frame after a skipped
// build. The backend clears and transitions the chain once at creation for exactly that.
layout(set = 1, binding = 0) uniform sampler2D bloomChain;
layout(push_constant) uniform Push {
    // 1 / the DESTINATION viewport's size in pixels, which is NOT the size of hdrScene.
    //
    // The HDR target is swapchain-sized and never resized with the editor's Viewport
    // panel, so in the editor the two differ -- a 1200x700 panel against a 1920x1080
    // scene target. gl_FragCoord spans the DESTINATION, so dividing it by
    // textureSize(hdrScene, 0) makes uv reach only (0.625, 0.648): fragment (x,y) samples
    // texel (x,y), a 1:1 crop of the top-left of the render whose zoom changes every time
    // the panel is dragged. Found in review 2026-10-16, after a run with zero validation
    // errors on both entry points -- `mosaic run --window` cannot show it, because there
    // the two extents are equal.
    //
    // Only the backend knows the destination, so it passes it, from the same
    // viewport_pass_open_ selector that picks the pipeline. Reciprocal rather than the
    // extent itself: this is per-fragment, and the division is not.
    vec2 inv_dest;
    // The active camera's exposure, passed to draw_hdr_resolve as a parameter rather than
    // read back out of the per-frame lighting UBO. It is a property of the resolve now,
    // and a parameter is testable where a UBO field read inside the backend is not.
    float exposure;
    // The active camera's Bloom::intensity, already floored at 0 by RenderSystem. At 0 the
    // chain contributes exactly 0 whatever it contains, which is why a frame that skipped
    // build_bloom cannot show the previous frame's glow and no staleness flag has to be
    // right.
    float bloom_intensity;
} pc;
layout(location = 0) out vec4 outColor;

// ACES filmic approximation (Narkowicz), and THE ONLY COPY IN THE TREE. It used to live in
// cube.frag and skybox.frag, which meant it ran per object and left decals, particles,
// ribbons and world text untouched by the curve -- they alpha-blended against a buffer
// whose contents had already been through it. Running it once over the whole scene is what
// makes a value above 1.0 meaningful, and therefore what makes bloom possible at all.
//
// This must NOT also gamma-correct: the destination is B8G8R8A8_SRGB, so the hardware
// encodes linear -> sRGB on write. A pow(c, 1.0/2.2) here would double-correct.
vec3 tonemap_aces(vec3 x)
{
    // The curve floors its own input, which is not a duplicate of the exposure floor
    // below -- it guards a DIFFERENT input. `x` can arrive negative from an unclamped
    // light colour: DirectionalLight::color is a reflected Vec3 with no clamp anywhere,
    // and the Studio inspector renders it as three unbounded DragFloats, so a colour of
    // (-1,-1,-1) is one drag away. A negative input makes this function return WHITE: its
    // numerator 2.51x^2 + 0.03x turns positive below x = -0.012 while its denominator is
    // strictly positive for every real x (discriminant -1.0127), so the ratio clamps to
    // 1.0 and the response is non-monotonic across zero.
    //
    // Flooring HERE makes that unreachable for every caller, present and future, instead
    // of requiring each call site to remember. The call site still floors exposure
    // separately, because that catches a negative knob even when the colour is ALSO
    // negative -- flooring only the product would multiply two negatives into a positive
    // and brighten.
    x = max(x, vec3(0.0));
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    // UV FROM gl_FragCoord, NOT from an interpolated clip position, and this is
    // load-bearing. Every render pass in this backend uses a negative-height viewport (the
    // Y-flip ui.vert compensates for), so a fullscreen triangle that derives its UV from
    // clip space samples the scene upside down. screen_effect.vert has exactly that bug
    // and nobody noticed, because a vignette is radially symmetric. gl_FragCoord is
    // framebuffer space, so this is correct by construction rather than by reasoning that
    // no headless test can check.
    // Scales the WHOLE HDR target into the destination viewport. The sampler is LINEAR
    // and clamps to edge, so a destination smaller than the source is a filtered
    // downscale rather than a crop, and the aspect works out: the scene was rendered with
    // the destination's aspect ratio, so mapping its full NDC range back onto the
    // destination is the identity the editor needs.
    vec2 uv = gl_FragCoord.xy * pc.inv_dest;
    vec3 hdr = texture(hdrScene, uv).rgb;
    // The chain is sampled with the SAME uv as the scene: it is a half-resolution image of
    // the same framing, so normalised coordinates line the two up with no extra scaling,
    // and the LINEAR sampler does the upscale.
    vec3 bloom = texture(bloomChain, uv).rgb;
    // Exposure scales the value ENTERING the curve, as it did in cube.frag. Applied after,
    // it would scale an already-clamped [0,1] value -- it could darken but never brighten,
    // which is most of what exposure is for.
    //
    // Floored at 0, and NOT because a negative exposure is merely meaningless: by the
    // arithmetic above it renders WHITE, not black, and non-monotonically. One character
    // in a scene file -- `-2` where `2` was meant -- would otherwise produce a confident,
    // plausible-looking, completely wrong image. The top end is deliberately NOT bounded;
    // a huge exposure overflows to NaN near 1.2e19, the same garbage-in posture roughness
    // already documents, and capping it would invent a maximum nobody asked for.
    // BLOOM IS ADDED INSIDE THE CURVE, and exposure scales it too, because bloom is LIGHT.
    // Added after the curve it would be untonemapped glow pasted onto a tone-mapped scene:
    // it clips immediately and reads as a white smear rather than as something glowing.
    // Left outside the exposure multiply, an exposure change would move the scene but not
    // its glow, which looks like a compositing error.
    outColor = vec4(tonemap_aces((hdr + bloom * pc.bloom_intensity) * max(pc.exposure, 0.0)), 1.0);
}
