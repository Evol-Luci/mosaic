#version 450

layout(push_constant) uniform Push { vec4 tint; vec4 params; } pc; // tint.w = strength, params.x = vignette

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main()
{
    float strength = pc.tint.w;
    float vignette = pc.params.x;
    float r   = distance(fragUV, vec2(0.5)) / 0.7071;
    float vig = smoothstep(0.5, 1.0, r) * vignette;
    float a   = clamp(strength + vig, 0.0, 1.0);
    vec3  rgb = (a > 0.0) ? (pc.tint.rgb * strength) / a : pc.tint.rgb; // black*vig adds 0 to rgb
    outColor  = vec4(rgb, a);
}
