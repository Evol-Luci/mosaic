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
    float shininess;
    float specular_strength;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    vec4 camera_position; // world space .xyz
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

// Blinn-Phong specular for one light. L points FROM the fragment TOWARD the light
// and is unit length. Returns the specular coefficient; the caller multiplies by
// that light's own attenuation/shadow/cone factor and by the LIGHT's color --
// never by albedo, which would make every surface read as painted metal.
float blinn_spec(vec3 n, vec3 v, vec3 L, float shininess, float strength)
{
    vec3 hv = v + L;
    // v == -L (viewer exactly opposite the light) gives a zero-length half-vector,
    // and normalize() would produce NaN that propagates into the output color as an
    // undiagnosable pixel. Fall back to the normal, where dot(n, h) == 1.
    vec3  h = (dot(hv, hv) > 1e-8) ? normalize(hv) : n;
    // pow(x, 0.0) is 1 everywhere and pow(0, 0) is undefined, so a zero exponent
    // would wash the entire surface. Clamp rather than trusting authored input.
    float e = max(shininess, 1.0);
    // The step() gate kills highlights on geometry facing away from the light, which
    // the half-vector alone would happily produce -- it reads as light leaking
    // through the object.
    return pow(max(dot(n, h), 0.0), e) * strength * step(0.0, dot(n, L));
}

// ACES filmic approximation (Narkowicz). Maps linear HDR to the linear display range
// so bright highlights roll off instead of clipping flat against the 8-bit target.
//
// This must NOT also gamma-correct: the swapchain is B8G8R8A8_SRGB, so the hardware
// encodes linear -> sRGB on write. A pow(c, 1.0/2.2) here would double-correct.
//
// Applied in cube.frag only. The other scene shaders are unlit -- they output authored
// colour times alpha and cannot exceed 1.0 -- so mapping them would darken authored
// colour, since this curve takes 1.0 to roughly 0.8.
vec3 tonemap_aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(fl.light_direction.xyz);
    float shadow = shadow_factor();
    float lambert = max(0.0, dot(n, -l)) * shadow;
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;
    vec3 v = normalize(fl.camera_position.xyz - fragPos);

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
    vec3 pointL = toL / max(d, 1e-4);

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
    vec3 spotLdir = toSpotL / max(spotD, 1e-4);

    // Specular is tinted by LIGHT color, not albedo, and each light's highlight is
    // gated by that same light's own factors -- so shadowed ground gets no sun glint
    // and a spot's highlight respects its cone.
    float spec_dir  = blinn_spec(n, v, -l, pc.shininess, pc.specular_strength) * shadow;
    float spec_pt   = blinn_spec(n, v, pointL, pc.shininess, pc.specular_strength) * atten;
    float spec_spot = blinn_spec(n, v, spotLdir, pc.shininess, pc.specular_strength)
                    * spotAtten * coneAtten;

    vec3 color = albedo * (0.1 + lambert * fl.light_color.xyz)
               + albedo * pl * fl.point_color.xyz
               + albedo * spotL * fl.spot_color.xyz
               + spec_dir * fl.light_color.xyz
               + spec_pt * fl.point_color.xyz
               + spec_spot * fl.spot_color.xyz;
    outColor = vec4(tonemap_aces(color), 1.0);
}
