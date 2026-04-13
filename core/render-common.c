// SPDX-License-Identifier: Apache-2.0
#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

#include "linmath.h"

static const char *render_str[RENDER_MAX] = {
    [RENDER_OPENGL] = "OpenGL",
    [RENDER_METAL]  = "Metal",
    [RENDER_WGPU]   = "WebGPU",
};

const renderer_caps *renderer_get_caps(renderer_t *r)
{
    return r->ops->get_caps();
}

void renderer_mat4x4_ortho(renderer_t *renderer, mat4x4 M, float l, float r, float b, float t, float n, float f)
{
    renderer_get_caps(renderer)->ndc_z_zero_one
        ? mat4x4_ortho_ndc_z_1(M, l, r, b, t, n, f)
        : mat4x4_ortho_ndc_z_2(M, l, r, b, t, n, f);
}

void renderer_mat4x4_perspective(renderer_t *renderer, mat4x4 m, float y_fov, float aspect, float n, float f)
{
    renderer_get_caps(renderer)->ndc_z_zero_one
	    ? mat4x4_perspective_ndc_z_1(m, y_fov, aspect, n, f)
	    : mat4x4_perspective_ndc_z_2(m, y_fov, aspect, n, f);
}

int renderer_query_limits(renderer_t *renderer, render_limit limit)
{
    return renderer->ops->query_limits(renderer, limit);
}

cerr _renderer_init(renderer_t *renderer, const renderer_init_options *opts)
{
    if (!renderer || !opts)
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "renderer: %p opts: %p",
            .arg0   = renderer,
            .arg1   = (void *)opts
        );

    if ((unsigned)opts->backend >= RENDER_MAX)
        return CERR_NOT_SUPPORTED_REASON(
            .fmt    = "requested rendering backend %d does not exist",
            .arg0   = (void *)(uintptr_t)opts->backend
        );

    memset(renderer, 0, sizeof(*renderer));

    switch (opts->backend) {
        case RENDER_OPENGL:     CERR_RET_CERR(gl_renderer_setup(renderer, opts));   break;
        case RENDER_METAL:      CERR_RET_CERR(mtl_renderer_setup(renderer, opts));  break;
        case RENDER_WGPU:       CERR_RET_CERR(wgpu_renderer_setup(renderer, opts)); break;
        default:                return CERR_NOT_SUPPORTED_REASON(
                                    .fmt    = "requested rendering backend %s is not supported",
                                    .arg0   = render_str[opts->backend]
                                );
    }

    return renderer->ops->init(renderer, opts);
}

void renderer_done(renderer_t *renderer)
{
    if (!renderer->ops->done)   return;
    renderer->ops->done(renderer);
}

void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    if (!renderer->ops->set_version)    return;
    renderer->ops->set_version(renderer, major, minor, profile);
}

void renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    r->ops->viewport(r, x, y, width, height);
}

void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    r->ops->get_viewport(r, px, py, pwidth, pheight);
}

void renderer_hdr_enable(renderer_t *r, bool enable)
{
    if (!r->ops->hdr_enable)    return;
    r->ops->hdr_enable(r, enable);
}

void renderer_swapchain_begin(renderer_t *r)
{
    r->ops->swapchain_begin(r);
}

void renderer_frame_begin(renderer_t *r)
{
    if (!r->ops->frame_begin)   return;
    r->ops->frame_begin(r);
}

void renderer_frame_end(renderer_t *r)
{
    if (!r->ops->frame_end) return;
    r->ops->frame_end(r);
}

void renderer_swapchain_end(renderer_t *r)
{
    if (!r->ops->swapchain_end) return;
    r->ops->swapchain_end(r);
}

void renderer_debug(renderer_t *r)
{
    if (!r->ops->debug) return;
    r->ops->debug(r);
}

void renderer_cull_face(renderer_t *r, cull_face cull)
{
    r->ops->cull_face(r, cull);
}

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
    r->ops->blend(r, _blend, sfactor, dfactor);
}

cerr renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces,
                   data_type idx_type, unsigned int nr_instances)
{
    return r->ops->draw(r, draw_type, nr_faces, idx_type, nr_instances);
}

cerr _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    renderer_t *r = opts->renderer;
    cerr err = r->ops->buf_init(buf, opts);
    buf->renderer = r;
    return err;
}

void buffer_deinit(buffer_t *buf)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_deinit(buf);
}

void buffer_bind(buffer_t *buf, uniform_t loc)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_bind(buf, loc);
}

void buffer_unbind(buffer_t *buf, uniform_t loc)
{
    if (!buf->renderer)   return;
    buf->renderer->ops->buf_unbind(buf, loc);
}

bool buffer_loaded(buffer_t *buf)
{
    return buf->loaded;
}

#ifndef CONFIG_FINAL
cres(int) buffer_set_name(buffer_t *buf, const char *fmt, ...)
{
    if (!buf->renderer || !buf->renderer->ops->buf_set_name)
        return cres_val(int, 0);

    va_list ap;
    va_start(ap, fmt);
    char label[128];
    vsnprintf(label, sizeof(label), fmt, ap);
    va_end(ap);

    buf->renderer->ops->buf_set_name(buf, label);

    return cres_val(int, 0);
}
#endif /* CONFIG_FINAL */

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
cerr _uniform_buffer_set(uniform_buffer_t *ubo, data_type type, size_t *offset, size_t *size,
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
     * vec2 is aligned on 8-bype boundary [Metal];
     * non-arrayed scalars have maximum storage size of 4, if something
     * larger is following a non-padded offset, it needs to be padded first
     */
#ifdef CONFIG_RENDERER_METAL
    if (storage_size == 8 && *offset % 8)
        *offset = round_up(*offset, 8);
    else
#endif /* CONFIG_RENDERER_METAL */
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

cerr_check texture_pixel_init(renderer_t *renderer, texture_t *tex, float color[4])
{
    cerr err = texture_init(tex, .renderer = renderer);
    if (IS_CERR(err))
        return err;

    texture_format fmt = TEX_FMT_RGBA8;
    if (texture_format_supported(TEX_FMT_RGBA32F))
        for (size_t i = 0; i < 4; i++)
            if (color[i] > 1.0f) {
                fmt = TEX_FMT_RGBA32F;
                break;
            }

    if (fmt == TEX_FMT_RGBA8) {
        uchar _color[] = { color[0] * 255.0, color[1] * 255.0, color[2] * 255.0, color[3] * 255.0 };
        return texture_load(tex, fmt, 1, 1, _color);
    }

    return texture_load(tex, fmt, 1, 1, color);
}

static texture_t _white_pixel;
static texture_t _black_pixel;
static texture_t _transparent_pixel;

texture_t *white_pixel(void) { return &_white_pixel; }
texture_t *black_pixel(void) { return &_black_pixel; }
texture_t *transparent_pixel(void) { return &_transparent_pixel; }

void textures_init(renderer_t *renderer)
{
    cerr werr, berr, terr;

    float white[] = { 1, 1, 1, 1 };
    werr = texture_pixel_init(renderer, &_white_pixel, white);
    float black[] = { 0, 0, 0, 1 };
    berr = texture_pixel_init(renderer, &_black_pixel, black);
    float transparent[] = { 0, 0, 0, 0 };
    terr = texture_pixel_init(renderer, &_transparent_pixel, transparent);

    err_on(IS_CERR(werr) || IS_CERR(berr) || IS_CERR(terr), "failed: %d/%d/%d\n",
           CERR_CODE(werr), CERR_CODE(berr), CERR_CODE(terr));

    texture_set_name(&_white_pixel, "white pixel");
    texture_set_name(&_black_pixel, "black pixel");
    texture_set_name(&_transparent_pixel, "transparent pixel");
}

void textures_done(void)
{
    texture_done(&_white_pixel);
    texture_done(&_black_pixel);
    texture_done(&_transparent_pixel);
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
    ui_igTableCell(false, "%u", buf->off);
    ui_igTableCell(false, "%u", buf->comp_count * data_comp_size(opts->comp_type));
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
    ui_igTableCell(false, texture_format_string(opts->format));
    if (tex->layers)
        ui_igTableCell(false, "%d x %d x %d", tex->width, tex->height, tex->layers);
    else
        ui_igTableCell(false, "%d x %d", tex->width, tex->height);
    ui_igTableCell(false, texture_wrap_str[opts->wrap]);
    ui_igTableCell(false, texture_filter_str[opts->min_filter]);
    ui_igTableCell(false, texture_filter_str[opts->mag_filter]);
    ui_igTableCell(false, "%s", tex->multisampled ? "ms" : "");
}
#endif /* !CONFIG_FINAL */
