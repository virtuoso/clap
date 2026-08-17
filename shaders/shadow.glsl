#ifndef SHADERS_SHADOW_GLSL
#define SHADERS_SHADOW_GLSL

#include "ubo_lighting.glsl"
#include "ubo_shadow.glsl"
#include "pass-tex.glsl"
#include "ndc-z.glsl"
#include "linearize-depth.glsl"

/*
 * The shadow maps and everything sampling them are declared iff the
 * variant samples shadows at all (declared iff used, see model.frag);
 * the shadow UBO stays outside the gate: lighting.glsl reads
 * shadow_tint unconditionally
 */
#ifdef SHADER_VARIANT_SHADOWS
#ifndef SHADER_SHADOW_MAP_ARRAY
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map1) uniform sampler2D shadow_map1;
layout (binding=SAMPLER_BINDING_shadow_map2) uniform sampler2D shadow_map2;
layout (binding=SAMPLER_BINDING_shadow_map3) uniform sampler2D shadow_map3;
#else
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2DArray shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map_ms) uniform sampler2DMSArray shadow_map_ms;
#endif /* SHADER_SHADOW_MAP_ARRAY */

float shadow_factor_pcf(in sampler2DArray map, in vec4 pos, in int layer, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float total = 0.0;

    for (int x = -pcf_count; x <= pcf_count; x++)
        for (int y = -pcf_count; y <= pcf_count; y++) {
            float shadow_distance = texel_fetch_2darray(map, vec3(pos.xy, float(layer)), ivec2(x, y)).r;
            if (pos.z + bias < shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

float shadow_factor_pcf(in sampler2D map, in vec4 pos, in float bias)
{
    const int pcf_count = 2;
    const float pcf_total_texels = pow(float(pcf_count) * 2.0 + 1.0, 2.0);
    float total = 0.0;

    for (int x = -pcf_count; x <= pcf_count; x++)
        for (int y = -pcf_count; y <= pcf_count; y++) {
            float shadow_distance = texel_fetch_2d(map, pos.xy, ivec2(x, y)).r;
            if (pos.z + bias < shadow_distance)
                total += 1.0;
        }

    return 1.0 - total / pcf_total_texels * pos.w;
}

#ifndef SHADER_GLES
float shadow_factor_msaa(in sampler2DMSArray map, in vec4 pos, in int layer, in float bias)
{
    float total = 0.0;

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        float shadow_distance = texel_fetch_2dms_array_sample(map, vec3(pos.xy, float(layer)), i).r;

        if (pos.z + bias < shadow_distance)
            total += 1.0;
    }
    total /= MSAA_SAMPLES;

    return 1.0 - total * pos.w;
}

float shadow_factor_msaa_weighted(in sampler2DMSArray map, in vec4 pos, in int layer, in float bias)
{
    float total = 0.0;
    float mean = 0.0, variance = 0.0;
    float samples[MSAA_SAMPLES];

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        samples[i] = texel_fetch_2dms_array_sample(map, vec3(pos.xy, float(layer)), i).r;
        mean += samples[i];
    }
    mean /= MSAA_SAMPLES;

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        variance += pow(samples[i] - mean, 2);
    }
    variance /= MSAA_SAMPLES;

    float adaptWeight = exp(-variance * 16.0); // Higher variance = more conservative filtering

    for (int i = 0; i < MSAA_SAMPLES; i++) {
        float weight = mix(1.0, adaptWeight, variance);
        if (pos.z + bias < samples[i]) total += weight;
    }
    total /= MSAA_SAMPLES;

    return 1.0 - total * pos.w;
}
#endif /* SHADER_GLES */

/*
 * A spotlight's shadow view is a perspective projection, a cascade's an
 * orthographic one: only the former carries the depth into w, so the
 * bottom row of its matrix isn't (0, 0, 0, 1)
 */
bool shadow_is_perspective(in int layer)
{
    return shadow_mvp[layer][0][3] != 0.0 || shadow_mvp[layer][1][3] != 0.0 || shadow_mvp[layer][2][3] != 0.0;
}

/*
 * Slope-scaled receiver bias, in texels of the shadow view: the depth
 * error across the sampling filter's footprint at the receiver's slope
 * is the magnitude of the self-shadowing (acne) it produces, so back the
 * receiver off by that much plus half a texel for rasterization
 * snapping. @radius is the filter's reach in texels: 1 for bilinear
 * moment fetches, pcf_count + 0.5 for the PCF kernel. Expressing the
 * bias in the view's own texel size keeps the residual level equal
 * across cascades -- a fixed constant under-biases every coarser
 * cascade progressively (visible as moire worsening with cascade order
 * and a level step at every divide). shadow_texel is the texel's world
 * size for a cascade and its size per unit of depth for a perspective
 * view, whose texels grow with the distance from the light.
 */
float shadow_receiver_bias(in int layer, in float light_dot, in float radius)
{
    float slope = sqrt(1.0 - light_dot * light_dot) / max(light_dot, 0.125);

    return shadow_texel[layer] * (0.5 + radius * slope);
}

/*
 * The receiver's depth in the moments' units, the linearized depth
 * shadow_vsm.frag stores, backed off toward the light by @bias from
 * shadow_receiver_bias(). A perspective view's w is that depth exactly,
 * and the bias scales with it; a cascade's window depth is linear over
 * the light's far plane.
 */
float shadow_vsm_depth(in int layer, in vec4 proj_coords, in float bias)
{
    if (shadow_is_perspective(layer))   return proj_coords.w * (1.0 - bias);

    return linearize_depth(proj_coords.z - bias / light_far[layer], 0.1, light_far[layer]);
}

/* @d is the receiver's depth from shadow_vsm_depth() */
float shadow_factor_vsm_calc(in vec2 moments, in float d, in int layer)
{
    if (moments.y < 0.0)    return 1.0;

    float m1 = moments.x;
    float m2 = max(moments.y, m1 * m1);

    /* Early out if fully lit */
    if (d < m1)
        return 1.0;

    float distance = d - m1;
    /*
     * The moments are in world units, so the fp32 cancellation noise
     * of m2 - m1^2 scales with m1^2 (~2^-23 relative): an absolute
     * floor can't cover it across the cascades' depth ranges, and
     * uncovered noise reads as sub-1.0 light in fully lit areas,
     * deepening with cascade order
     */
    float variance = max(m2 - m1 * m1, 1e-6 * m1 * m1);

    /* Chebyshev upper bound */
    float p_max = variance / (variance + distance * distance);

    /*
     * Chebyshev gives an upper bound on visibility: with multiple
     * occluders in the filter footprint the bound's lower range is light
     * bleeding, not penumbra. Write the tail below shadow_bleed off as
     * shadow and renormalize the rest (linstep). No further shaping: the
     * penumbra profile comes from the variance itself.
     */
    return clamp((p_max - shadow_bleed) / (1.0 - shadow_bleed), 0.0, 1.0);
}

float shadow_factor_vsm(in sampler2DArray map, in vec4 pos, in int layer)
{
    vec2 moments = texel_fetch_2darray(map, vec3(pos.xy, float(layer)), ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z, layer);
}

float shadow_factor_vsm(in sampler2D map, in vec4 pos, in int layer)
{
    vec2 moments = texel_fetch_2d(map, pos.xy, ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z, layer);
}

float shadow_factor_calc(in vec3 unit_normal, in vec4 view_pos, in vec3 light_dir,
                         in bool use_vsm, in bool use_msaa)
{
#ifndef SHADER_VARIANT_SHADOW_PCF
    /* the PCF path is compiled out: VSM is the only shadow filter */
    use_vsm = true;
#endif /* !SHADER_VARIANT_SHADOW_PCF */
    float shadow_factor = 1.0;

    float light_dot = dot(unit_normal, normalize(-light_dir));
    if (light_dot < 0)
        return shadow_factor;

    int layer = -1;

    for (int i = 0; i < nr_cascades; i++) {
        if (-view_pos.z < cascade_distances[i]) {
            layer = i;
            break;
        }
    }

    layer = clamp(layer, 0, max(nr_cascades - 1, 0));

    vec4 shadow_pos = shadow_mvp[layer] * world_pos;

    /* w near zero means the fragment can't be projected -- bail before the perspective divide */
    if (shadow_pos.w < 1e-3)
        return shadow_factor;

    vec4 proj_coords = vec4(shadow_pos.xyz / shadow_pos.w, shadow_pos.w);
    proj_coords.xy = convert_pass_tex(proj_coords.xy * 0.5 + 0.5);
    proj_coords.z = convert_from_ndc_z(proj_coords.z);

    /*
     * Outside the shadow map there is no occluder information, so the
     * fragment is lit. CSM cascades cover the camera frustum by
     * construction, but a spotlight's perspective map only covers its
     * cone, and everything else would sample edge-clamped moments or
     * depths.
     */
    bool outside = any(lessThan(proj_coords.xyz, vec3(0.0))) || any(greaterThan(proj_coords.xyz, vec3(1.0)));
    if (outside)    return shadow_factor;

    /* the PCF kernel reaches pcf_count + 0.5 = 2.5 texels out */
    float bias = shadow_receiver_bias(layer, light_dot, use_vsm ? 1.0 : 2.5);

    /*
     * The moment views render with a regular depth range, CSM with a
     * reversed one: the moments compare against a receiver depth backed
     * off toward the light, the pcf/msaa samplers add the bias in
     * normalized depth
     */
    vec4 vsm_coords = proj_coords;
    vsm_coords.z = shadow_vsm_depth(layer, proj_coords, bias);
    bias /= light_far[layer];
#ifndef SHADER_SHADOW_MAP_ARRAY
    switch (layer) {
        case 0:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map, vsm_coords, 0) :
                shadow_factor_pcf(shadow_map, proj_coords, bias);
            break;
        case 1:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map1, vsm_coords, 1) :
                shadow_factor_pcf(shadow_map1, proj_coords, bias);
            break;
        case 2:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map2, vsm_coords, 2) :
                shadow_factor_pcf(shadow_map2, proj_coords, bias);
            break;
        default:
        case 3:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map3, vsm_coords, 3) :
                shadow_factor_pcf(shadow_map3, proj_coords, bias);
            break;
    }
#else
    if (use_msaa)
        shadow_factor = shadow_factor_msaa_weighted(shadow_map_ms, proj_coords, layer, bias);
    else
        shadow_factor = use_vsm ?
            shadow_factor_vsm(shadow_map, vsm_coords, layer) :
            shadow_factor_pcf(shadow_map, proj_coords, layer, bias);
#endif /* SHADER_SHADOW_MAP_ARRAY */

    return mix(shadow_factor, 1.0, pow(1 - light_dot, 1.3));
}

#endif /* SHADER_VARIANT_SHADOWS */

#endif /* SHADERS_SHADOW_GLSL */
