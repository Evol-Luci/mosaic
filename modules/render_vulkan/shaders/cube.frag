#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 light_direction;
    vec4 light_color;
    vec4 base_color;
    vec4 point_position; // object space .xyz, .w = range
    vec4 point_color;    // .xyz = color * intensity
    mat4 light_mvp;
    vec4 spot_position;  // object space .xyz, .w = range
    vec4 spot_direction; // object space .xyz, .w = cos(outer_angle)
    vec4 spot_color;     // .xyz = color * intensity, .w = cos(inner_angle)
} pc;
layout(location = 0) out vec4 outColor;

// Hard shadow (v1, no PCF): 1.0 = fully lit, 0.0 = in shadow. A fragment whose
// light-clip-space position falls outside the fixed frustum's [0,1] UV range is
// treated as not shadowed (fully lit) -- see the design doc's Shading section.
float shadow_factor()
{
    vec3 proj = fragLightSpacePos.xyz / fragLightSpacePos.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
    {
        return 1.0;
    }
    const float bias = 0.0015; // constant bias; slope-scaling deferred (see design doc)
    float closest = texture(shadowMap, uv).r;
    return (proj.z - bias > closest) ? 0.0 : 1.0;
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(pc.light_direction.xyz);
    float lambert = max(0.0, dot(n, -l)) * shadow_factor();
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Point light (object space): windowed inverse-square attenuation. Unaffected
    // by shadows -- only the directional light casts shadows.
    vec3  toL   = pc.point_position.xyz - fragPos;
    float d     = length(toL);
    float range = pc.point_position.w;
    float atten = 0.0;
    if (range > 0.0)
    {
        float win = clamp(1.0 - pow(d / range, 4.0), 0.0, 1.0);
        atten = (win * win) / (d * d + 1.0);
    }
    float pl = max(0.0, dot(n, toL / max(d, 1e-4))) * atten;

    // Spot light (object space): same windowed inverse-square falloff as the point
    // light, additionally scaled by a smooth cone edge (smoothstep between the
    // outer and inner cosines -- full intensity inside the inner cone, smoothly
    // fading to zero at the outer cone, never a hard/aliased cutoff). No shadows.
    vec3  toSpotL   = pc.spot_position.xyz - fragPos;
    float spotD     = length(toSpotL);
    float spotRange = pc.spot_position.w;
    float spotAtten = 0.0;
    if (spotRange > 0.0)
    {
        float win = clamp(1.0 - pow(spotD / spotRange, 4.0), 0.0, 1.0);
        spotAtten = (win * win) / (spotD * spotD + 1.0);
    }
    float cosAngle = dot(normalize(-toSpotL), pc.spot_direction.xyz);
    float coneAtten = smoothstep(pc.spot_direction.w, pc.spot_color.w, cosAngle); // (cos_outer, cos_inner)
    float spotL = max(0.0, dot(n, toSpotL / max(spotD, 1e-4))) * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * pc.light_color.xyz)
               + albedo * pl * pc.point_color.xyz
               + albedo * spotL * pc.spot_color.xyz;
    outColor = vec4(color, 1.0);
}
