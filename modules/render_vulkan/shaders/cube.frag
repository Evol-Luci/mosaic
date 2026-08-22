#version 450
layout(location = 0) in vec3 fragNormal;
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
    float ambient = 0.1;
    vec3 rgb = pc.base_color.xyz * (ambient + lambert * pc.light_color.xyz);
    outColor = vec4(rgb, 1.0);
}
