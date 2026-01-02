// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

/****************************************************************************
 * UBO packing: std140 and the like
 ****************************************************************************/

/* Get std140 storage size for a given type */
static inline size_t type_storage_size(data_type type)
{
    size_t storage_size = data_type_size(type);
    if (!storage_size) {
        clap_unreachable();
        return 0;
    }

    /* Matrices are basically arrays of vec4 (technically, vecN padded to 16 bytes) */
    switch (type) {
        case DT_MAT2:
            storage_size = 2 * data_type_size(DT_VEC4); /* vec2 padded to vec4 */
            break;
        case DT_MAT3:
            storage_size = 3 * data_type_size(DT_VEC4); /* vec3 padded to vec4 */
            break;
        /* And what's left is mat4, which is perfect as it is */
        default:
            /*
             * Scalars are not padded to 16 bytes unless they are followed by
             * compound types, which we don't know about here; instead,
             * uniform_buffer_set() handles the std140 compliant offset alignment.
             */
            break;
    }

    return storage_size;
}

/*
 * Calculate uniform @offset within a UBO, its total @size and set its @value
 * if @value is not NULL
 */
cerr uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
                        unsigned int count, const void *value)
{
    size_t elem_size = data_type_size(type);       /* C ABI element size */
    size_t storage_size = type_storage_size(type); /* Metal-aligned size */
    bool dirty = ubo->dirty;
    cerr err = CERR_OK;

    if (!elem_size || !storage_size)
        return CERR_INVALID_ARGUMENTS;

    /* These are only used if @value is set */
    const char *src = (const char *)value;
    char *dst = src ? (char *)ubo->data + *offset : NULL;

    *offset = *size;

    /*
     * Individual scalars are *not* padded to 16 bytes, unless they're in
     * an array; compound types are aligned to a 16 bype boundary even if
     * they follow non-padded scalars
     */
    if (storage_size < 16 && count > 1)
        storage_size = 16;

    /*
     * vec2 is aligned on 8-bype boundary;
     * non-arrayed scalars have maximum storage size of 4, if something
     * larger is following a non-padded offset, it needs to be padded first
     */
    if (storage_size > 4 && *offset % 16)
        *offset = round_up(*offset, 16);

    *size = *offset;

    /*
     * Copy elements from a C array to a UBO array. Metal UBO layout, like
     * std140, uses fun alignments for various types that don't match C at
     * all, they need to be copied one at a time.
     */
    for (unsigned int i = 0; i < count; i++) {
        if (value) {
            /* If we overshoot, the buffer is still dirty, return straight away */
            if (*size + storage_size > ubo->size) {
                err = CERR_BUFFER_OVERRUN;
                goto out;
            }

            if (type == DT_MAT2 || type == DT_MAT3) {
                /* Manually copy row-by-row with padding */
                bool is_mat3 = type == DT_MAT3;
                int rows = is_mat3 ? 3 : 2;
                /* Source row: DT_VEC2 or DT_VEC3 */
                size_t row_size = data_type_size(is_mat3 ? DT_VEC3 : DT_VEC2);
                /* Destination row: DT_VEC4 */
                size_t row_stride = type_storage_size(DT_VEC4);
                for (int r = 0; r < rows; r++) {
                    if (memcmp(dst, src, row_size)) {
                        memcpy(dst, src, row_size);
                        dirty = true;
                    }
                    src += row_size;             /* Move to next row */
                    dst += row_stride;           /* Move to next aligned row */
                }
            } else {
                /* Only update the target value if it changed */
                if (memcmp(dst, src, elem_size)) {
                    /* Copy only valid bytes */
                    memcpy(dst, src, elem_size);
                    dirty = true;
                }
                src += elem_size;            /* Move to next element (C ABI aligned) */
                dst += storage_size;         /* Move to next element (std140 aligned) */
            }
        }
        *size += storage_size;
    }

out:
    ubo->dirty = dirty;

    return err;
}

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
