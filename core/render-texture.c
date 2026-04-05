// SPDX-License-Identifier: Apache-2.0
#include "render.h"

static const struct {
    const char      *name;
    size_t          comp_sz;
    size_t          nr_comps;
    texture_format  add_alpha;
} texture_format_desc[TEX_FMT_MAX] = {
    [TEX_FMT_R8]        = { .name = "R8",           .comp_sz = 1, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_R16F]      = { .name = "R16F",         .comp_sz = 2, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_R32F]      = { .name = "R32F",         .comp_sz = 4, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG8]       = { .name = "RG8",          .comp_sz = 1, .nr_comps = 2, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG16F]     = { .name = "RG16F",        .comp_sz = 2, .nr_comps = 2, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG32F]     = { .name = "RG32F",        .comp_sz = 4, .nr_comps = 2, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGBA8]     = { .name = "RGBA8",        .comp_sz = 1, .nr_comps = 4, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB8]      = { .name = "RGB8",         .comp_sz = 1, .nr_comps = 3, .add_alpha = TEX_FMT_RGBA8 },
    [TEX_FMT_RGBA8_SRGB]= { .name = "RGBA8_sRGB",   .comp_sz = 1, .nr_comps = 4, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB8_SRGB] = { .name = "RGB8_sRGB",    .comp_sz = 1, .nr_comps = 3, .add_alpha = TEX_FMT_RGBA8_SRGB },
    [TEX_FMT_RGBA16F]   = { .name = "RGBA16F",      .comp_sz = 2, .nr_comps = 4, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB16F]    = { .name = "RGB16F",       .comp_sz = 2, .nr_comps = 3, .add_alpha = TEX_FMT_RGBA16F },
    [TEX_FMT_RGBA32F]   = { .name = "RGBA32F",      .comp_sz = 4, .nr_comps = 4, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB32F]    = { .name = "RGB32F",       .comp_sz = 4, .nr_comps = 3, .add_alpha = TEX_FMT_RGBA32F },
    [TEX_FMT_R32UI]     = { .name = "R32UI",        .comp_sz = 4, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG32UI]    = { .name = "RG32UI",       .comp_sz = 4, .nr_comps = 2, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGBA32UI]  = { .name = "RGBA32UI",     .comp_sz = 4, .nr_comps = 4, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH32F]  = { .name = "DEPTH32F",     .comp_sz = 4, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH24F]  = { .name = "DEPTH24F",     .comp_sz = 3, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH16F]  = { .name = "DEPTH16F",     .comp_sz = 2, .nr_comps = 1, .add_alpha = TEX_FMT_MAX },
};

const char *texture_format_string(texture_format fmt)
{
    if (fmt >= TEX_FMT_MAX) return "<invalid>";
    return texture_format_desc[fmt].name;
}

size_t texture_format_comp_size(texture_format fmt)
{
    if (fmt >= TEX_FMT_MAX) return 0;
    return texture_format_desc[fmt].comp_sz;
}

size_t texture_format_nr_comps(texture_format fmt)
{
    if (fmt >= TEX_FMT_MAX) return 0;
    return texture_format_desc[fmt].nr_comps;
}

bool texture_format_needs_alpha(texture_format format)
{
    if (format >= TEX_FMT_MAX) return false;
    return texture_format_desc[format].add_alpha != TEX_FMT_MAX;
}

texture_format texture_format_add_alpha(texture_format format)
{
    if (format >= TEX_FMT_MAX) return format;
    texture_format rgba = texture_format_desc[format].add_alpha;
    return rgba != TEX_FMT_MAX ? rgba : format;
}

/**
 * texture_rgb_to_rgba() - expand 3-component pixel data to 4-component
 * @format:     source texture format (must be a 3-component RGB format)
 * @buf:        source pixel data
 * @nr_pixels:  total number of pixels (width * height * layers)
 * @out_size:   receives the size of the output buffer in bytes
 *
 * Returns a newly allocated buffer with the alpha channel set to opaque
 * (0xFF for 8-bit, 0x3C00 for 16F, 1.0f for 32F), or NULL on failure.
 * Caller must free the returned buffer with mem_free().
 */
void *texture_rgb_to_rgba(texture_format format, const void *buf,
                          size_t nr_pixels, size_t *out_size)
{
    size_t comp_sz = texture_format_comp_size(format);
    size_t dst_pixel_sz = comp_sz * 4;
    size_t src_pixel_sz = comp_sz * 3;
    size_t total = dst_pixel_sz * nr_pixels;

    void *dst = mem_alloc(total);
    if (!dst)
        return NULL;

    const uint8_t alpha_8   = 0xFF;
    const uint16_t alpha_16f = 0x3C00; /* IEEE 754 half-float 1.0 */
    const float alpha_32f   = 1.0f;

    const void *alpha;
    switch (comp_sz) {
        case 1: alpha = &alpha_8;   break;
        case 2: alpha = &alpha_16f; break;
        case 4: alpha = &alpha_32f; break;
        default:
            mem_free(dst);
            return NULL;
    }

    const uint8_t *sp = buf;
    uint8_t *dp = dst;

    for (size_t i = 0; i < nr_pixels; i++) {
        memcpy(dp, sp, src_pixel_sz);
        memcpy(dp + src_pixel_sz, alpha, comp_sz);
        sp += src_pixel_sz;
        dp += dst_pixel_sz;
    }

    if (out_size)
        *out_size = total;

    return dst;
}
