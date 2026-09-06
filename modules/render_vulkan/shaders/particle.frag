#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragAlpha;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main()
{
    // Soft round sprite: fade to 0 at the quad's inscribed circle edge.
    float d = length(fragUV - vec2(0.5)) * 2.0;
    float falloff = smoothstep(1.0, 0.0, d);
    outColor = vec4(fragColor, fragAlpha * falloff);
}
