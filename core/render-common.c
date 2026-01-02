// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

static const char *texture_format_strings[TEX_FMT_MAX] = {
        [TEX_FMT_R8]        = "R8",
        [TEX_FMT_R16F]      = "R16F",
        [TEX_FMT_R32F]      = "R32F",
        [TEX_FMT_RG8]       = "RG8",
        [TEX_FMT_RG16F]     = "RG16F",
        [TEX_FMT_RG32F]     = "RG32F",
        [TEX_FMT_RGBA8]     = "RGBA8",
        [TEX_FMT_RGB8]      = "RGB8",
        [TEX_FMT_RGBA8_SRGB]= "RGBA8_sRGB",
        [TEX_FMT_RGB8_SRGB] = "RGB8_sRGB",
        [TEX_FMT_RGBA16F]   = "RGBA16F",
        [TEX_FMT_RGB16F]    = "RGB16F",
        [TEX_FMT_RGBA32F]   = "RGBA32F",
        [TEX_FMT_RGB32F]    = "RGB32F",
        [TEX_FMT_R32UI]     = "R32UI",
        [TEX_FMT_RG32UI]    = "RG32UI",
        [TEX_FMT_RGBA32UI]  = "RGBA32UI",
        [TEX_FMT_DEPTH32F]  = "DEPTH32F",
        [TEX_FMT_DEPTH24F]  = "DEPTH24F",
        [TEX_FMT_DEPTH16F]  = "DEPTH16F",
};

const char *texture_format_string(texture_format fmt)
{
    if (fmt >= TEX_FMT_MAX) return "<invalid>";
    return texture_format_strings[fmt];
}

#ifndef CONFIG_FINAL
#include "ui-debug.h"

static const char *buffer_type_str(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:         return "array";
        case BUF_ELEMENT_ARRAY: return "element array";
        default:                break;
    }

    return "<invalid type>";
};

static const char *buffer_usage_str(buffer_usage usage)
{
    switch (usage) {
        case BUF_STATIC:        return "static";
        case BUF_DYNAMIC:       return "dynamic";
        default:                break;
    }

    return "<invalid usage>";
};

void buffer_debug_header(void)
{
    ui_igTableHeader(
        "buffers",
        (const char *[]) { "attribute", "binding", "size", "type", "usage", "offset", "size", "comp" },
        8
    );
}

void buffer_debug(buffer_t *buf, const char *name)
{
    buffer_init_options *opts = &buf->opts;

    ui_igTableCell(true, "%s", name);
    ui_igTableCell(false, "%d", buf->loc);
    ui_igTableCell(false, "%zu", opts->size);
    ui_igTableCell(false, "%s", buffer_type_str(opts->type));
    ui_igTableCell(false, "%s", buffer_usage_str(opts->usage));
#ifndef CONFIG_RENDERER_METAL
    ui_igTableCell(false, "%u", buf->off);
    ui_igTableCell(false, "%u", buf->comp_count * data_comp_size(opts->comp_type));
#endif /* CONFIG_RENDERER_METAL */
    ui_igTableCell(false, "%s (%d) x %d",
                   data_type_name(opts->comp_type),
                   data_comp_size(opts->comp_type),
                   opts->comp_count);
}

static const char *texture_type_str[] = {
    [TEX_2D]        = "2D",
    [TEX_2D_ARRAY]  = "2D array",
    [TEX_3D]        = "3D",
};

static const char *texture_wrap_str[] = {
    [TEX_CLAMP_TO_EDGE]         = "clamp edge",
    [TEX_CLAMP_TO_BORDER]       = "clamp border",
    [TEX_WRAP_REPEAT]           = "repeat",
    [TEX_WRAP_MIRRORED_REPEAT]  = "mirrored repeat",
};

static const char *texture_filter_str[] = {
    [TEX_FLT_LINEAR]    = "linear",
    [TEX_FLT_NEAREST]   = "nearest",
};

void texture_debug_header(void)
{
    ui_igTableHeader(
        "buffers",
        (const char *[]) { "name", "type", "format", "size", "wrap", "min", "mag", "ms" },
        8
    );
}

void texture_debug(texture_t *tex, const char *name)
{
    texture_init_options *opts = &tex->opts;

    ui_igTableCell(true, name);
    ui_igTableCell(false, texture_type_str[opts->type]);
    ui_igTableCell(false, texture_format_strings[opts->format]);
#ifndef CONFIG_RENDERER_METAL
    if (tex->layers)
        ui_igTableCell(false, "%d x %d x %d", tex->width, tex->height, tex->layers);
    else
        ui_igTableCell(false, "%d x %d", tex->width, tex->height);
#endif /* CONFIG_RENDERER_METAL */
    ui_igTableCell(false, texture_wrap_str[opts->wrap]);
    ui_igTableCell(false, texture_filter_str[opts->min_filter]);
    ui_igTableCell(false, texture_filter_str[opts->mag_filter]);
#ifndef CONFIG_RENDERER_METAL
    ui_igTableCell(false, "%s", tex->multisampled ? "ms" : "");
#endif /* CONFIG_RENDERER_METAL */
}
#endif /* !CONFIG_FINAL */
