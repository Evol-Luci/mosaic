#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
    vec4 point_position; // object space .xyz, .w = range
    vec4 point_color;    // .xyz = color * intensity
} pc;
layout(location = 0) out vec4 outColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (object space): windowed inverse-square attenuation.
    vec3  toL   = pc.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = pc.point_position.w;
    float win   = clamp(1.0 - pow(d / max(range, 1e-4), 4.0), 0.0, 1.0);
    float atten = (range > 0.0) ? (win * win) / (d * d + 1.0) : 0.0;
    float pl    = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    vec3 color = albedo * (0.1 + lambert * pc.light_color.xyz)
               + albedo * pl * pc.point_color.xyz;
    outColor = vec4(color, 1.0);
}
