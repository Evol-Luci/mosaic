#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
} pc;
layout(location = 0) out vec4 outColor;
void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l));
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;
    outColor = vec4(albedo * (0.1 + lambert * pc.light_color.xyz), 1.0);
}
