#ifndef SHADERS_COMBINE_COMMON_GLSL
#define SHADERS_COMBINE_COMMON_GLSL

/*
 * The composite body shared by combine.frag (separate pass, with TAA
 * resolve and/or SMAA in present between it and the screen) and
 * combine-present.frag (combine and present merged into one screen
 * pass when neither TAA nor SMAA runs). The including shader declares
 * the samplers (model_tex, emission_map, bloom_tex, bloom_half_tex,
 * sobel_tex, depth_tex, shadow_map, lut_tex, noise3d, volumetric_tex)
 * and the UBOs.
 */

#include "fog.glsl"

/*
 * Contrast in scene-linear light: a gain in log space about the exposed
 * scene's mid-grey, so the pivot follows exposure and contrast 1.0
 * doubles the slope through it. A linear gain about a fixed 0.5 would
 * pivot far above mid-grey at any sane exposure and drive darks
 * negative.
 */
f16vec3 apply_contrast(f16vec3 color, float16_t contrast, float16_t pivot)
{
    return clamp(pivot * pow(max(color, H(0.0)) / pivot, HVEC3(H(1.0) + contrast)), H(0.0), H(HDR_LUT_MAX));
}

/*
 * The haze over a surface @view_pos away, in both fog modes: the radial
 * fog's curve, which the volumetric fog patches with @patchiness, the
 * medium's mean density along the ray relative to the noise field's
 * mean (see volumetric.frag). Scaling the curve's optical depth by it
 * keeps the haze per pixel at full resolution, where only this slow
 * modulation comes from the low-res buffer, and averages out to the
 * radial fog.
 */
float16_t radial_fog_factor(vec3 view_pos, float patchiness)
{
#ifdef SHADER_VARIANT_RADIAL_FOG
    /*
     * The distance stays in full precision: squaring a far view
     * position overflows fp16 past 256 units, and the background sits
     * on the far plane
     */
    float factor = fog_factor_at(length(view_pos));

    if (volumetric_fog) factor = 1.0 - exp2(patchiness * log2(max(1.0 - factor, 1e-6)));

    return H(factor);
#else /* !SHADER_VARIANT_RADIAL_FOG */
    return H(0.0);
#endif /* !SHADER_VARIANT_RADIAL_FOG */
}

vec3 radial_fog_color(vec3 view_pos)
{
#ifdef SHADER_VARIANT_RADIAL_FOG
    vec3 noise_vec = sample_noise3d(noise3d, view_pos + vec3(noise(view_pos)), 0.05) * 0.05;
    float n = min(dot(noise_vec, noise_vec), 3.0) / 3.0;
    return fog_color * (1.0 - n);
#else /* !SHADER_VARIANT_RADIAL_FOG */
    return vec3(1.0);
#endif /* !SHADER_VARIANT_RADIAL_FOG */
}

vec4 combine_color(vec2 pass_tex)
{
    if (volumetric_debug)   return vec4(texture(volumetric_tex, pass_tex).rgb, 1.0);

    f16vec3 tex_color = HVEC3(texture(model_tex, pass_tex).rgb);
    /*
     * The model RT's alpha carries the transmittance of everything
     * blended over this pixel (1: no translucency in front): outlines
     * and SSAO attenuate by it behind glass. Blunt but uniform: telling
     * translucent boundaries apart from what they cover is a later job.
     */
    float16_t transmit = H(texture(model_tex, pass_tex).a);
    vec3 view_pos = view_pos_from_depth(depth_tex, inverse_proj, pass_tex);
    /* rgb: the light the medium scatters toward the camera; a: its patchiness, halved for unorm targets */
    f16vec4 vol = HVEC4(texture(volumetric_tex, pass_tex));
    float16_t fog_factor = radial_fog_factor(view_pos, 2.0 * float(vol.a));

    /*
     * Bloom upsample+blend, absorbed from the former "upsample" pass:
     * mix the raw emission with the blurred quarter-res chain here
     * instead of paying a full-res RT write+read for it. The raw
     * emission tap is jitter-displaced like the downsample's input, see
     * bloom-downsample.frag; sampled at 1/4-res stride it aliases per
     * frame especially hard on small emitters.
     */
    vec4 wide = texture(bloom_tex, pass_tex);

    /*
     * Halo radius falls off with the emitter's distance: a glow of a
     * fixed world radius subtends less of the screen the further away
     * it is. Peak intensity needs no compensation, because the energy
     * and the area it covers shrink at the same rate. The chain carries
     * the pyramid level the emitters want, see bloom-level.glsl. With
     * the two-chain bloom (the same blur chain at quarter and at half
     * res, the latter reaching half as far) that lerps between the two,
     * wide at the coarsest level. With the pyramid, the level selection
     * happened in bloom-upsample.frag and both samplers hold its
     * result, so the lerp is the identity.
     */
    float tightness = 1.0 - clamp(bloom_level_decode(wide), 0.0, BLOOM_LEVEL_RANGE) / BLOOM_LEVEL_RANGE;
    f16vec3 blurred = HVEC3(mix(wide.rgb, texture(bloom_half_tex, pass_tex).rgb, tightness));
    vec2 em_uv = use_taa ?
        convert_pass_tex(convert_pass_tex(pass_tex) + jitter * 0.5) :
        pass_tex;
    f16vec3 raw_emission = HVEC3(texture(emission_map, em_uv).rgb);
    f16vec3 highlight_color = use_hdr ?
        (raw_emission + blurred * H(bloom_intensity)) * H(bloom_exposure) :
        mix(raw_emission, blurred, clamp(H(bloom_intensity), H(0.0), H(1.0)));

    float16_t ao = H(texture(shadow_map, pass_tex).r);

    if (use_ssao)
        tex_color = tex_color * mix(H(1.0), ao, H(ssao_weight) * transmit);

    vec4 result;

    if (use_hdr) {
        f16vec3 hdr_color = tex_color * H(lighting_exposure) + highlight_color * (H(1.0) - fog_factor);
        /* fog */
        hdr_color = mix(hdr_color, HVEC3(radial_fog_color(view_pos)), fog_factor);
        /* volumetric in-scattering: sun shafts and light beams */
        hdr_color += vol.rgb;
        /* contrast */
        hdr_color = apply_contrast(hdr_color, H(contrast), H(0.18 * lighting_exposure));
        /* color grading */
        hdr_color = HVEC3(apply_lut(lut_tex, hdr_color));
        /* tonemapping */
        if (hdr_output)
            hdr_color = HVEC3(hdr_display_map(hdr_color, hdr_white_nits, hdr_peak_nits,
                                              hdr_compress_knee, hdr_knee_softness,
                                              lighting_operator));
        else
            hdr_color = mix(reinhard_tonemap(hdr_color), aces_tonemap(hdr_color), H(lighting_operator));
        /*
         * The one edge application, at the composite, fading into the
         * haze; SMAA (use_edge_aa) applies edges in present.frag
         * instead, where the neighbors it blends from have been
         * computed
         */
        float edge_blend = float(1.0 - fog_factor) * float(transmit);
        if (use_edges && !use_edge_aa)
            hdr_color = apply_edge(hdr_color, sobel_tex, edge_blend, pass_tex, ivec2(0));
        result = vec4(hdr_color, 1.0);
    } else {
        tex_color = mix(tex_color, HVEC3(radial_fog_color(view_pos)), fog_factor);
        tex_color += vol.rgb;
        float edge_blend = float(1.0 - fog_factor) * float(transmit);
        if (use_edges && !use_edge_aa)
            tex_color = apply_edge(tex_color, sobel_tex, edge_blend, pass_tex, ivec2(0));
        result = vec4(tex_color + highlight_color * 2.0, 1.0);
    }

    /*
     * One return: glslang emits an unreachable merge block after an if/else
     * that returns from both arms, which tint warns about
     */
    return result;
}

#endif /* SHADERS_COMBINE_COMMON_GLSL */
