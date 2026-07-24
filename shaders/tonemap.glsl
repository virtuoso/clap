#ifndef CLAP_TONEMAP_GLSL
#define CLAP_TONEMAP_GLSL

f16vec3 reinhard_tonemap(f16vec3 hdr_color)
{
    return H(1.0) - exp(-hdr_color);
}

f16vec3 aces_tonemap(f16vec3 hdr_color)
{
    return hdr_color * (hdr_color + H(0.25)) / (hdr_color * (hdr_color + H(0.5)) + H(0.1));
}

/*
 * HDR display map: the SDR picture, with the headroom on top. The lighting
 * operator runs per channel exactly as it does for SDR output, so below
 * knee_rel (scene-linear, after exposure) the two outputs agree and a LUT
 * dialled against the operator holds on HDR too. Above the knee the
 * brightest channel decides how much of the headroom a highlight gets, and
 * the whole colour is scaled up by that, so hue and saturation stay what SDR
 * shows and only the brightness climbs toward peak_rel, over about
 * `softness` scene-linear units. (Scaling per channel instead lets one
 * strong channel run to the peak alone, which turns an imbalanced grade
 * like bloodveil into a far deeper red than SDR ever shows.) Monotone, and
 * smooth at the knee: the headroom term starts with zero slope.
 */
vec3 hdr_display_map(vec3 rgb, float paper_white_nits, float peak_nits,
                     float knee_rel, float softness, float op)
{
    float peak_rel = peak_nits / paper_white_nits;
    vec3 sdr = vec3(mix(reinhard_tonemap(HVEC3(rgb)), aces_tonemap(HVEC3(rgb)), H(op)));
    float top = max(sdr.r, max(sdr.g, sdr.b));
    float t = max(max(rgb.r, max(rgb.g, rgb.b)) - knee_rel, 0.0) / softness;
    float head = (peak_rel - 1.0) * (1.0 - exp(-t * t));

    return sdr * (1.0 + head / max(top, 1e-3));
}

#endif /* CLAP_TONEMAP_GLSL */
