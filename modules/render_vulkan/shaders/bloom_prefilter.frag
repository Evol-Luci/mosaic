#version 450

// Bloom's first pass: the HDR scene target -> mip 0 of the chain, at HALF resolution,
// applying the threshold and the soft knee on the way.
//
// The prefilter IS the first downsample rather than a separate full-resolution
// bright-pass. A full-resolution pass would cost a 1920x1080 target for a result that is
// immediately halved.

layout(set = 0, binding = 0) uniform sampler2D hdrScene;

// Shared byte for byte with bloom_blur.frag and with the backend's BloomPush. The two
// vec2s come first: they have the larger alignment, so putting a float ahead of them
// would cost padding and, worse, make the layouts disagree the moment someone reorders
// one side.
layout(push_constant) uniform Push {
    vec2  inv_dest;  // 1 / this mip's size -- uv comes from gl_FragCoord, see below
    vec2  inv_src;   // 1 / the source's size. Unused here: the prefilter is a single tap.
    float threshold; // display white by default; light above this blooms
    float knee;      // the soft ramp below the threshold
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    // UV FROM gl_FragCoord, not from an interpolated clip position. Every render pass in
    // this backend sets a negative-height (Y-flipped) viewport, so a fullscreen triangle
    // that derives its UV from clip space samples upside down -- screen_effect.vert has
    // exactly that bug and nobody noticed, because a vignette is radially symmetric.
    // gl_FragCoord is framebuffer space and so correct by construction, which is also why
    // the bloom passes can use a POSITIVE-height viewport without any of this changing.
    vec2 uv = gl_FragCoord.xy * pc.inv_dest;

    // Floored for the reason tonemap_aces floors its own input: DirectionalLight::color is
    // a reflected Vec3 with no clamp anywhere, and the inspector renders it as three
    // unbounded DragFloats. A negative colour reaching the max() below picks the
    // least-negative channel and thresholds a dark pixel against it.
    vec3 c = max(texture(hdrScene, uv).rgb, vec3(0.0));

    // Brightness as the max channel, not luminance. A saturated red at (3, 0, 0) is above
    // display white and should bloom; its luminance is 0.64 and would not.
    float br = max(c.r, max(c.g, c.b));

    // A negative threshold makes the contribution term (br + |t|) / br, which exceeds 1
    // and AMPLIFIES rather than selecting -- a scene glowing brighter than it is lit, from
    // one typed minus sign.
    float th = max(pc.threshold, 0.0);

    // FLOORED, and this is the sharpest edge in the feature: kn is a divisor four lines
    // down. knee is a reflected float on an unbounded DragFloat, so 0 is one drag away,
    // and a zero divisor here makes every bloomed pixel NaN -- which the chain spreads
    // across the whole image and the resolve adds in. NaN survives the tone curve's clamp,
    // so the result is not "too bright", it is garbage. Floored rather than branched so a
    // zero knee degrades to exactly the hard threshold, which is what it means.
    float kn = max(pc.knee, 1e-4);

    // The soft knee (Karis). Below th - kn nothing passes; across the 2*kn window
    // straddling the threshold the contribution ramps quadratically; above th + kn it is
    // the hard br - th. NOT optional polish: with a hard knee a specular highlight
    // drifting across the threshold pops in and out between frames on a moving object,
    // which reads as a bug rather than as an effect.
    float soft = clamp(br - th + kn, 0.0, 2.0 * kn);
    soft = (soft * soft) / (4.0 * kn);

    // Scale the COLOUR by how much of its brightness passed, rather than subtracting the
    // threshold per channel. Subtracting per channel shifts hue -- it takes the same
    // amount from a channel at 3.0 and one at 1.1 -- and a hue shift in a glow reads as
    // chromatic aberration.
    float contrib = max(soft, br - th) / max(br, 1e-4);

    outColor = vec4(c * contrib, 1.0);
}
