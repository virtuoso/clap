// SPDX-License-Identifier: Apache-2.0
#include "render.h"

static const struct {
    const char  *name;
    size_t      comp_sz;
    size_t      nr_comps;
} texture_format_desc[TEX_FMT_MAX] = {
    [TEX_FMT_R8]        = { .name = "R8",           .comp_sz = 1, .nr_comps = 1 },
    [TEX_FMT_R16F]      = { .name = "R16F",         .comp_sz = 2, .nr_comps = 1 },
    [TEX_FMT_R32F]      = { .name = "R32F",         .comp_sz = 4, .nr_comps = 1 },
    [TEX_FMT_RG8]       = { .name = "RG8",          .comp_sz = 1, .nr_comps = 2 },
    [TEX_FMT_RG16F]     = { .name = "RG16F",        .comp_sz = 2, .nr_comps = 2 },
    [TEX_FMT_RG32F]     = { .name = "RG32F",        .comp_sz = 4, .nr_comps = 2 },
    [TEX_FMT_RGBA8]     = { .name = "RGBA8",        .comp_sz = 1, .nr_comps = 4 },
    [TEX_FMT_RGB8]      = { .name = "RGB8",         .comp_sz = 1, .nr_comps = 3 },
    [TEX_FMT_RGBA8_SRGB]= { .name = "RGBA8_sRGB",   .comp_sz = 1, .nr_comps = 4 },
    [TEX_FMT_RGB8_SRGB] = { .name = "RGB8_sRGB",    .comp_sz = 1, .nr_comps = 3 },
    [TEX_FMT_RGBA16F]   = { .name = "RGBA16F",      .comp_sz = 2, .nr_comps = 4 },
    [TEX_FMT_RGB16F]    = { .name = "RGB16F",       .comp_sz = 2, .nr_comps = 3 },
    [TEX_FMT_RGBA32F]   = { .name = "RGBA32F",      .comp_sz = 4, .nr_comps = 4 },
    [TEX_FMT_RGB32F]    = { .name = "RGB32F",       .comp_sz = 4, .nr_comps = 3 },
    [TEX_FMT_R32UI]     = { .name = "R32UI",        .comp_sz = 4, .nr_comps = 1 },
    [TEX_FMT_RG32UI]    = { .name = "RG32UI",       .comp_sz = 4, .nr_comps = 2 },
    [TEX_FMT_RGBA32UI]  = { .name = "RGBA32UI",     .comp_sz = 4, .nr_comps = 4 },
    [TEX_FMT_DEPTH32F]  = { .name = "DEPTH32F",     .comp_sz = 4, .nr_comps = 1 },
    [TEX_FMT_DEPTH24F]  = { .name = "DEPTH24F",     .comp_sz = 3, .nr_comps = 1 },
    [TEX_FMT_DEPTH16F]  = { .name = "DEPTH16F",     .comp_sz = 2, .nr_comps = 1 },
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
