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

float compress_knee(float x, float knee, float max_x, float s)
{
    /* x,knee,max_x in "relative to paper white" units, s > 0 controls softness */
    x = max(x, 0.0);
    if (x <= knee) return x;
    float t = (x - knee) / max(1e-6, (max_x - knee));
    /* log rolloff to [knee..max_x] */
    return knee + (max_x - knee) * (log(1.0 + s * t) / log(1.0 + s));
}

vec3 hdr_display_map(vec3 rgb, float paper_white_nits, float peak_nits,
                     float knee_rel, float softness)
{
    float peak_rel = peak_nits / paper_white_nits;

    /* scene-linear luminance (use your working primaries; this is sRGB-ish) */
    float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float y2 = compress_knee(y, knee_rel, peak_rel, softness);

    float s = (y > 1e-6) ? (y2 / y) : 0.0;
    return rgb * s; /* can be > 1.0, capped by peak_rel */
}

#endif /* CLAP_TONEMAP_GLSL */

