// SPDX-License-Identifier: Apache-2.0
#include "error.h"
#include "render.h"
#include "util.h"

static const struct {
    const char      *name;
    size_t          comp_sz;
    size_t          nr_comps;
    data_type       comp_type;
    texture_format  add_alpha;
    bool            depth;
    /* a swapchain's format, not a render target candidate: BGR order, packed bits */
    bool            swapchain;
} texture_format_desc[TEX_FMT_MAX] = {
    [TEX_FMT_R8]        = { .name = "R8",           .comp_sz = 1, .nr_comps = 1, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_R16F]      = { .name = "R16F",         .comp_sz = 2, .nr_comps = 1, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_R32F]      = { .name = "R32F",         .comp_sz = 4, .nr_comps = 1, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG8]       = { .name = "RG8",          .comp_sz = 1, .nr_comps = 2, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG16]      = { .name = "RG16",         .comp_sz = 2, .nr_comps = 2, .comp_type = DT_USHORT, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG16F]     = { .name = "RG16F",        .comp_sz = 2, .nr_comps = 2, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG32F]     = { .name = "RG32F",        .comp_sz = 4, .nr_comps = 2, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGBA8]     = { .name = "RGBA8",        .comp_sz = 1, .nr_comps = 4, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB8]      = { .name = "RGB8",         .comp_sz = 1, .nr_comps = 3, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_RGBA8 },
    [TEX_FMT_RGBA8_SRGB]= { .name = "RGBA8_sRGB",   .comp_sz = 1, .nr_comps = 4, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB8_SRGB] = { .name = "RGB8_sRGB",    .comp_sz = 1, .nr_comps = 3, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_RGBA8_SRGB },
    [TEX_FMT_RGBA16]    = { .name = "RGBA16",       .comp_sz = 2, .nr_comps = 4, .comp_type = DT_USHORT, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGBA16F]   = { .name = "RGBA16F",      .comp_sz = 2, .nr_comps = 4, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB16F]    = { .name = "RGB16F",       .comp_sz = 2, .nr_comps = 3, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_RGBA16F },
    [TEX_FMT_RGBA32F]   = { .name = "RGBA32F",      .comp_sz = 4, .nr_comps = 4, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGB32F]    = { .name = "RGB32F",       .comp_sz = 4, .nr_comps = 3, .comp_type = DT_FLOAT,  .add_alpha = TEX_FMT_RGBA32F },
    [TEX_FMT_R32UI]     = { .name = "R32UI",        .comp_sz = 4, .nr_comps = 1, .comp_type = DT_UINT,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RG32UI]    = { .name = "RG32UI",       .comp_sz = 4, .nr_comps = 2, .comp_type = DT_UINT,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_RGBA32UI]  = { .name = "RGBA32UI",     .comp_sz = 4, .nr_comps = 4, .comp_type = DT_UINT,   .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH32F]  = { .name = "DEPTH32F",     .comp_sz = 4, .nr_comps = 1, .comp_type = DT_FLOAT,  .depth = true, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH24F]  = { .name = "DEPTH24F",     .comp_sz = 3, .nr_comps = 1, .comp_type = DT_FLOAT,  .depth = true, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_DEPTH16F]  = { .name = "DEPTH16F",     .comp_sz = 2, .nr_comps = 1, .comp_type = DT_FLOAT,  .depth = true, .add_alpha = TEX_FMT_MAX },
    [TEX_FMT_BGRA8]     = { .name = "BGRA8",        .comp_sz = 1, .nr_comps = 4, .comp_type = DT_BYTE,   .add_alpha = TEX_FMT_MAX, .swapchain = true },
    [TEX_FMT_BGR10A2]   = { .name = "BGR10A2",      .comp_sz = 4, .nr_comps = 1, .comp_type = DT_UINT,   .add_alpha = TEX_FMT_MAX, .swapchain = true },
    [TEX_FMT_BGRA10XR]  = { .name = "BGRA10XR",     .comp_sz = 2, .nr_comps = 4, .comp_type = DT_USHORT, .add_alpha = TEX_FMT_MAX, .swapchain = true },
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
 * texture_format_next_match() - iterate formats matching a spec
 * @iter:           bitmask of formats already returned; initialize to 0
 * @min_width:      minimum component width, in bits
 * @min_components: minimum number of components
 * @type_mask:      acceptable component types, DT_MASK() bits
 * @depth:          match depth formats instead of color ones
 * @stencil:        [future] match stencil formats
 *
 * Returns the best not-yet-returned format satisfying the constraints,
 * preferring narrower and fewer-component formats, and marks it in
 * @iter, which makes resumption independent of any format ordering
 * and guarantees termination; errors with CERR_NOT_FOUND once the
 * candidates are exhausted. Probing renderer support is the caller's
 * business: this only consults the static table, so it links without
 * a renderer. Stencil formats are not supported yet.
 */
cres(texture_format) texture_format_next_match(texture_format_iter *iter, size_t min_width,
                                               size_t min_components, data_type_mask type_mask,
                                               bool depth, bool stencil)
{
    if (stencil)    return cres_error(texture_format, CERR_NOT_SUPPORTED);

    // TODO: Ideally, the constants should be defined ordered, but this requires
    // audit of all texture_init() sites that rely on implicit TEX_FMT_RGBA8;
    // for now, keep an ordered lookup table, which adds an extra O(n) lookup,
    // which is not a huge problem _for_ _now_, since this is not used in any
    // hot paths.
    static const texture_format ordered_fmts[] = {
        TEX_FMT_R8,       TEX_FMT_RG8,      TEX_FMT_RGB8,     TEX_FMT_RGBA8,
        TEX_FMT_R16F,     TEX_FMT_RG16F,    TEX_FMT_RGB16F,   TEX_FMT_RGBA16F,
        TEX_FMT_R32F,     TEX_FMT_RG32F,    TEX_FMT_RGB32F,   TEX_FMT_RGBA32F,
        TEX_FMT_R32UI,    TEX_FMT_RG32UI,   TEX_FMT_RGBA32UI,
        TEX_FMT_DEPTH16F, TEX_FMT_DEPTH24F, TEX_FMT_DEPTH32F
    };
    static_assert(TEX_FMT_MAX <= sizeof(texture_format_iter) * 8,
                  "texture_format_iter can't hold all texture formats");

    for (size_t idx = 0; idx < array_size(ordered_fmts); idx++) {
        texture_format fmt = ordered_fmts[idx];
        auto desc = &texture_format_desc[fmt];

        if (*iter & (1ull << fmt))              continue;
        if (!desc->name)                        continue;
        if (desc->swapchain)                    continue;
        if (desc->comp_sz * 8 < min_width)      continue;
        if (desc->nr_comps < min_components)    continue;
        if (!(type_mask & DT_MASK(desc->comp_type)))    continue;
        if (desc->depth != depth)               continue;

        *iter |= 1ull << fmt;
        return cres_val(texture_format, fmt);
    }

    return cres_error(texture_format, CERR_NOT_FOUND);
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
