#ifndef SHADERS_SHADOW_GLSL
#define SHADERS_SHADOW_GLSL

#include "ubo_lighting.glsl"
#include "ubo_shadow.glsl"

#ifdef CONFIG_GLES
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2D shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map1) uniform sampler2D shadow_map1;
layout (binding=SAMPLER_BINDING_shadow_map2) uniform sampler2D shadow_map2;
layout (binding=SAMPLER_BINDING_shadow_map3) uniform sampler2D shadow_map3;
#else
layout (binding=SAMPLER_BINDING_shadow_map) uniform sampler2DArray shadow_map;
layout (binding=SAMPLER_BINDING_shadow_map_ms) uniform sampler2DMSArray shadow_map_ms;
#endif /* CONFIG_GLES */

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

#ifndef CONFIG_GLES
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
#endif /* CONFIG_GLES */

float shadow_factor_vsm_calc(in vec2 moments, in float d)
{
    float m1 = moments.x;
    float m2 = moments.y;

    /* Early out if fully lit */
    if (d < m1)
        return 1.0;

    float distance = d - m1;
    float variance = max(m2 - m1 * m1, 1e-6);

    /* Chebyshev upper bound */
    float p_max = variance / (variance + distance * distance);

    const float light_bleed_reduction = 0.8; /* XXX: parameterize me */
    p_max = clamp((p_max - light_bleed_reduction) / (1.0 - light_bleed_reduction), 0.0, 1.0);

    return smoothstep(0.15, 0.95, clamp(p_max, 0.0, 1.0));
}

float shadow_factor_vsm(in sampler2D map, in vec4 pos)
{
    vec2 moments = texel_fetch_2d(map, pos.xy, ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z);
}

float shadow_factor_vsm(in sampler2DArray map, in vec4 pos, in int layer)
{
    vec2 moments = texel_fetch_2darray(map, vec3(pos.xy, float(layer)), ivec2(0)).rg;

    return shadow_factor_vsm_calc(moments, pos.z);
}

float shadow_factor_calc(in vec3 unit_normal, in vec4 view_pos, in vec3 light_dir,
                         in bool use_vsm, in bool use_msaa)
{
    float shadow_factor = 1.0;

    float light_dot = dot(unit_normal, normalize(-light_dir));
    if (light_dot < 0)
        return shadow_factor;

    int layer = -1;

    for (int i = 0; i < CASCADES_MAX; i++) {
        if (-view_pos.z < cascade_distances[i]) {
            layer = i;
            break;
        }
    }

    if (layer < 0)
        layer = CASCADES_MAX - 1;

    vec4 shadow_pos = shadow_mvp[layer] * world_pos;
    vec4 proj_coords = vec4(shadow_pos.xyz / shadow_pos.w, shadow_pos.w);
    proj_coords = proj_coords * 0.5 + 0.5;

    float bias = max(0.0005 * (1.0 - light_dot), 0.0008);
#ifdef CONFIG_GLES
    switch (layer) {
        case 0:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map, proj_coords) :
                shadow_factor_pcf(shadow_map, proj_coords, bias);
            break;
        case 1:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map1, proj_coords) :
                shadow_factor_pcf(shadow_map1, proj_coords, bias);
            break;
        case 2:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map2, proj_coords) :
                shadow_factor_pcf(shadow_map2, proj_coords, bias);
            break;
        default:
        case 3:
            shadow_factor = use_vsm ?
                shadow_factor_vsm(shadow_map3, proj_coords) :
                shadow_factor_pcf(shadow_map3, proj_coords, bias);
            break;
    }
#else
    if (use_msaa)
        shadow_factor = shadow_factor_msaa_weighted(shadow_map_ms, proj_coords, layer, bias);
    else
        shadow_factor = use_vsm ?
            shadow_factor_vsm(shadow_map, proj_coords, layer) :
            shadow_factor_pcf(shadow_map, proj_coords, layer, bias);
#endif /* CONFIG_GLES */

    return mix(shadow_factor, 1.0, pow(1 - light_dot, 1.3));
}

#endif /* SHADERS_SHADOW_GLSL */
