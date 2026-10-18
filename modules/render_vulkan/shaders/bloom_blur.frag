#version 450

// The 4-tap kernel, and it serves BOTH directions of the chain -- the downsample and the
// additive upsample are the same shader against different sources, differing only in the
// pipeline's blend state.
//
// That works because the taps are offset by a SOURCE texel. Against a source twice the
// destination's size the four taps straddle a 4x4 region: a box downsample. Against a
// source half the destination's size they straddle a region twice as wide in destination
// terms: a tent upsample, which is exactly what stops the upsampled glow looking blocky.
// Two copies of this would be two places for one of them to start offsetting by the
// destination instead.
//
// The sampler is LINEAR, so each tap is already a bilinear average of four source texels:
// a destination centre maps to a source texel CORNER, not a centre. Four taps is therefore
// a 16-texel kernel, not a 4-texel one.

layout(set = 0, binding = 0) uniform sampler2D src;

// Shared byte for byte with bloom_prefilter.frag and the backend's BloomPush. threshold
// and knee are declared and not read -- the layout is shared, and a block that differs
// between two shaders using one pipeline layout is a validation error at best.
layout(push_constant) uniform Push {
    vec2  inv_dest;
    vec2  inv_src;
    float threshold; // not read here
    float knee;      // not read here
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    // gl_FragCoord, for the negative-height-viewport reason bloom_prefilter.frag states.
    vec2 uv = gl_FragCoord.xy * pc.inv_dest;

    // SOURCE texel, never destination. Getting this backwards makes the upsample a point
    // resample and the glow visibly blocky -- the single easiest thing here to invert.
    vec2 t = pc.inv_src;

    vec3 s = texture(src, uv + vec2(-t.x, -t.y)).rgb
           + texture(src, uv + vec2( t.x, -t.y)).rgb
           + texture(src, uv + vec2(-t.x,  t.y)).rgb
           + texture(src, uv + vec2( t.x,  t.y)).rgb;

    // Alpha 1.0 and not the summed alpha: the upsample pipeline blends additively with
    // (ONE, ONE) on the colour, and the chain's alpha is never read by anything.
    outColor = vec4(s * 0.25, 1.0);
}
