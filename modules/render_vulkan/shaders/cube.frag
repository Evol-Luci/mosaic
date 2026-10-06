#version 450
layout(location = 0) in vec3 fragNormal;   // world space
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragPos;      // world space
layout(location = 3) in vec4 fragLightSpacePos;
layout(set = 0, binding = 0) uniform sampler2D tex;
// Metallic/roughness map, glTF 2.0 packing: G = roughness, B = metallic. Uploaded as
// UNORM rather than SRGB (see TextureColorSpace) because these channels are numbers,
// not colour -- the sRGB EOTF would turn an authored 0.5 into roughly 0.21. An
// untextured material binds a 1x1 white texture, so both factors below multiply by 1.
layout(set = 0, binding = 1) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(push_constant) uniform Push {
    mat4 mvp;
    mat4 model;
    mat4 normal_matrix;
    vec4 base_color;
    float metallic;
    float roughness;
} pc;
layout(set = 2, binding = 0) uniform FrameLighting {
    vec4 light_direction; // world space .xyz
    vec4 light_color;     // .xyz
    vec4 point_position;  // world space .xyz, .w = range
    vec4 point_color;     // .xyz = color * intensity
    vec4 spot_position;   // world space .xyz, .w = range
    vec4 spot_direction;  // world space .xyz, .w = cos(outer_angle)
    vec4 spot_color;      // .xyz = color * intensity, .w = cos(inner_angle)
    // .xyz is the camera in world space. .w is 1 when image-based lighting is active and
    // 0 otherwise -- it rides in this vec4's documented-unused padding slot rather than
    // growing the UBO, which would change its size assertion for one boolean.
    vec4 camera_position;
    mat4 light_view_proj;
} fl;
// Image-based lighting, in its own descriptor set because these are per-FRAME products
// resolved mid-frame -- see ibl_set_layout_ in the Vulkan backend for why that cannot
// share the per-frame UBO's set.
//
// A 1x1 stand-in is bound when there is no environment. Note the reason: the fetches
// below sit inside a branch, so they are not sampled on every path -- but they are
// STATICALLY USED by this module, and a descriptor a module statically uses must be
// valid at draw time whether or not the branch is taken.
layout(set = 3, binding = 0) uniform samplerCube irradianceMap;
layout(set = 3, binding = 1) uniform samplerCube prefilteredMap;
layout(set = 3, binding = 2) uniform sampler2D   brdfLut;

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

const float PI = 3.14159265;

// GGX / Trowbridge-Reitz normal distribution.
float distribution_ggx(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

// Smith geometry, Schlick-GGX, with the direct-lighting remap k = alpha / 2.
float geometry_smith(float NdotV, float NdotL, float alpha)
{
    float k  = alpha * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// Schlick Fresnel.
vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

// One light's Cook-Torrance contribution. L points FROM the fragment TOWARD the light
// and is unit length; the caller multiplies the result by that light's own
// shadow/attenuation/cone factor and by its colour.
//
// Returns diffuse + specular together, because kD depends on F and so the two terms
// cannot be computed independently -- that coupling IS the energy conservation.
//
// Note that specular here IS tinted by base_color for a metal, via F0. The function
// this replaced carried the opposite instruction, which was correct for Blinn-Phong and
// is wrong for PBR: a metal's specular colour is what distinguishes gold from silver.
vec3 brdf(vec3 n, vec3 v, vec3 L, vec3 albedo, float metallic, float roughness)
{
    // alpha == 0 makes D infinite at NdotH == 1. The engine does not clamp, by design,
    // so this is the only guard. It is bounded above as well: the engine's tests
    // explicitly guarantee that an out-of-range authored roughness reaches the GPU
    // unchanged, and a2 is roughness^4, which overflows float somewhere above
    // roughness ~4300 -- making D = inf/inf = NaN, and clamp(NaN, 0, 1) undefined. A
    // typo'd 1e6 in a scene file is a garbage pixel, not an error. Review 2026-10-04.
    float alpha = clamp(roughness, 0.045, 1.0);
    alpha = alpha * alpha;

    // Both appear in the 4 * NdotV * NdotL denominator, and NdotV goes to zero at every
    // silhouette edge.
    float NdotV = max(dot(n, v), 1e-4);
    float NdotL = max(dot(n, L), 1e-4);

    vec3 hv = v + L;
    // v == -L gives a zero-length half-vector and normalize() would return NaN, which
    // propagates to the output as an undiagnosable pixel. Carried over from blinn_spec.
    vec3  h = (dot(hv, hv) > 1e-8) ? normalize(hv) : n;
    float NdotH = max(dot(n, h), 0.0);
    float HdotV = max(dot(h, v), 0.0);

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = fresnel_schlick(HdotV, F0);
    float D  = distribution_ggx(NdotH, alpha);
    float G  = geometry_smith(NdotV, NdotL, alpha);

    vec3 specular = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-7);

    // (1 - metallic) is what makes a metal read as metal rather than as shiny plastic.
    // Metals have no diffuse lobe.
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // The step() gate kills contributions on geometry facing away from the light, which
    // the half-vector alone would happily produce. Carried over from blinn_spec.
    return (diffuse + specular) * NdotL * step(0.0, dot(n, L));
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
    vec3 albedo = texture(tex, fragUV).rgb * pc.base_color.xyz;

    // Factor x texel, the glTF convention: the push-constant scalar scales the map, so
    // an untextured material (white map) keeps the scalar's exact meaning and every
    // scene authored before texture maps renders identically.
    vec3  mr        = texture(metallicRoughnessMap, fragUV).rgb;
    float metallic  = pc.metallic * mr.b;
    float roughness = pc.roughness * mr.g;
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
    vec3 spotLdir = toSpotL / max(spotD, 1e-4);

    // One brdf() call per light, each gated by that light's own shadow/attenuation/cone
    // factor and tinted by that light's colour -- so shadowed ground gets no sun glint
    // and a spot's highlight respects its cone. Unlike the Blinn-Phong arrangement this
    // replaces, the specular term inside brdf() IS tinted by base_color for a metal.
    vec3 dir_term  = brdf(n, v, -l, albedo, metallic, roughness) * shadow;
    vec3 pt_term   = brdf(n, v, pointL, albedo, metallic, roughness) * atten;
    vec3 spot_term = brdf(n, v, spotLdir, albedo, metallic, roughness)
                   * spotAtten * coneAtten;

    vec3 F0_amb = mix(vec3(0.04), albedo, metallic);
    vec3 kD_amb = (vec3(1.0) - F0_amb) * (1.0 - metallic);

    vec3 ambient;
    if (fl.camera_position.w > 0.5)
    {
        // Diffuse: the cosine-convolved irradiance in the surface's own direction,
        // kD-weighted exactly as the flat term was, so metals still receive no ambient
        // diffuse.
        vec3 irradiance = texture(irradianceMap, n).rgb;
        vec3 diffuseIBL = kD_amb * albedo * irradiance;

        // Specular: the split-sum approximation. The prefiltered chain holds FIVE levels
        // at roughness 0, .25, .5, .75, 1, so the LOD is roughness * 4 and textureLod
        // interpolates between them. A * 5.0, or a chain of a different length, yields a
        // material that merely looks too shiny or too blurry.
        vec3 R = reflect(-v, n);
        vec3 prefiltered = textureLod(prefilteredMap, R, roughness * 4.0).rgb;
        // textureLod with an explicit 0, NOT texture(). create_texture gives every
        // image a mip chain, and texture() would pick a level from screen-space
        // derivatives of (NdotV, roughness) -- but those are TABLE COORDINATES, not
        // surface UVs, so the derivative means nothing. On the demo sphere's banded
        // roughness map a 0.2 jump across one pixel is ~51 texels of gradient, selecting
        // a level around 8x reduced: a visible seam of wrong scale/bias down every band
        // boundary, on the one object this slice exists to show off.
        vec2 ab = textureLod(brdfLut, vec2(max(dot(n, v), 1e-4), roughness), 0.0).rg;
        ambient = diffuseIBL + prefiltered * (F0_amb * ab.x + ab.y);
    }
    else
    {
        // The pre-IBL ambient, kept so a scene with NO environment renders exactly as it
        // did before this slice. Not dead code -- it is the other half of a runtime
        // choice, and the demo always has an environment, so nothing else would notice
        // if it were deleted.
        ambient = albedo * 0.1 * kD_amb;
    }

    vec3 color  = ambient
                + dir_term * fl.light_color.xyz
                + pt_term * fl.point_color.xyz
                + spot_term * fl.spot_color.xyz;
    outColor = vec4(tonemap_aces(color), 1.0);
}
