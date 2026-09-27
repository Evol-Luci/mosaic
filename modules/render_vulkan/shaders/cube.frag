#version 450
layout(location = 0) in vec3 fragNormal;   // world space
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;      // world space
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;
    mat4 normal_matrix;
    vec4 base_color;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    mat4 light_view_proj;
} fl;
layout(location = 0) out vec4 outColor;

// Hard shadow (no PCF): 1.0 = fully lit, 0.0 = in shadow. A fragment whose
// light-clip-space position falls outside the fixed frustum's [0,1] UV range is
// treated as not shadowed (fully lit). Carried over unchanged from shadows v1.
float shadow_factor()
{
    vec3 proj = fragLightSpacePos.xyz / fragLightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
    {
        return 1.0;
    }
    const float bias = 0.0015;
    float closest = texture(shadowMap, uv).r;
    return (proj.z - bias > closest) ? 0.0 : 1.0;
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(fl.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l)) * shadow_factor();
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (world space): windowed inverse-square attenuation. Formula
    // unchanged from 36e-1; only the space changed, which is what makes the
    // distance correct under non-uniform mesh scale.
    vec3  toL   = fl.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = fl.point_position.w;
    float atten = 0.0;
    if (range > 0.0)
    {
        float win = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
        atten = (win * win) / (d * d + 1.0);
    }
    float pl = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    // Spot light (world space): same falloff plus the smooth cone edge. Formula
    // unchanged from the spot-lights slice.
    vec3  toSpotL   = fl.spot_position.xyz - fragPos;
    float spotD     = length(toSpotL);
    float spotRange = fl.spot_position.w;
    float spotAtten = 0.0;
    if (spotRange > 0.0)
    {
        float win = clamp(1.0 - pow(spotD / spotRange, 4.0), 0.0, 1.0);
        spotAtten = (win * win) / (spotD * spotD + 1.0);
    }
    float cosAngle = dot(normalize(-toSpotL), fl.spot_direction.xyz);
    float coneAtten = smoothstep(fl.spot_direction.w, fl.spot_color.w, cosAngle); // (cos_outer, cos_inner)
    float spotL = max(0.0, dot(n, toSpotL / max(spotD, 1e-4))) * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * fl.light_color.xyz)
               + albedo * pl * fl.point_color.xyz
               + albedo * spotL * fl.spot_color.xyz;
    outColor = vec4(color, 1.0);
}
