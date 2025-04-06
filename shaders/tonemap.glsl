#ifndef CLAP_TONEMAP_GLSL
#define CLAP_TONEMAP_GLSL

vec3 reinhard_tonemap(vec3 hdr_color)
{
    return 1.0 - exp(-hdr_color);
}

vec3 aces_tonemap(vec3 hdr_color)
{
    return hdr_color * (hdr_color + 0.25) / (hdr_color * (hdr_color + 0.5) + 0.1);
}

#endif /* CLAP_TONEMAP_GLSL */

