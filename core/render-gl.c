// SPDX-License-Identifier: Apache-2.0
#include "shader_constants.h"
#include "error.h"
#include "logger.h"
#include "object.h"

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

#ifdef CLAP_DEBUG
static inline bool __gl_check_error(const char *str)
{
    GLenum err;

    err = glGetError();
    if (err != GL_NO_ERROR) {
        err("%s: GL error: 0x%04X: %s\n", str ? str : "<>", err, gluErrorString(err));
        abort();
        return true;
    }

    return false;
}

#define GL(__x) do {                    \
    __x;                                \
    __gl_check_error(__stringify(__x)); \
} while (0)
#else
#define GL(__x) __x
#endif

static struct gl_limits {
    GLint gl_max_texture_size;
    GLint gl_max_texture_units;
    GLint gl_max_texture_layers;
    GLint gl_max_color_attachments;
    GLint gl_max_color_texture_samples;
    GLint gl_max_depth_texture_samples;
} gl_limits;

static const GLuint gl_comp_type[] = {
    [DT_FLOAT]  = GL_FLOAT,
    [DT_INT]    = GL_INT,
    [DT_SHORT]  = GL_SHORT,
    [DT_USHORT] = GL_UNSIGNED_SHORT,
    [DT_BYTE]   = GL_UNSIGNED_BYTE,
    [DT_VEC3]   = GL_FLOAT,
    [DT_VEC4]   = GL_FLOAT,
    [DT_MAT4]   = GL_FLOAT,
};

static const unsigned int gl_comp_count[] = {
    [DT_FLOAT]  = 1,
    [DT_INT]    = 1,
    [DT_SHORT]  = 1,
    [DT_BYTE]   = 1,
    [DT_VEC3]   = 3,
    [DT_VEC4]   = 4,
    [DT_MAT4]   = 16,
};

/****************************************************************************
 * Buffer
 ****************************************************************************/

static void buffer_drop(struct ref *ref)
{
    buffer_t *buf = container_of(ref, struct buffer, ref);
    buffer_deinit(buf);
}
DECLARE_REFCLASS(buffer);

static GLenum gl_buffer_type(buffer_type type)
{
    switch (type) {
        case BUF_ARRAY:             return GL_ARRAY_BUFFER;
        case BUF_ELEMENT_ARRAY:     return GL_ELEMENT_ARRAY_BUFFER;
        default:                    break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_buffer_usage(buffer_usage usage)
{
    switch (usage) {
        case BUF_STATIC:            return GL_STATIC_DRAW;
        case BUF_DYNAMIC:           return GL_DYNAMIC_DRAW;
        default:                    break;
    }

    clap_unreachable();

    return GL_NONE;
}

bool buffer_loaded(buffer_t *buf)
{
    return buf->loaded;
}

void _buffer_init(buffer_t *buf, const buffer_init_options *opts)
{
    ref_embed(buffer, buf);

    data_type comp_type = opts->comp_type;
    if (comp_type == DT_NONE)
        comp_type = DT_FLOAT;

    unsigned int comp_count = opts->comp_count;
    if (!comp_count)
        comp_count = gl_comp_count[comp_type];

    buf->type = gl_buffer_type(opts->type);
    buf->usage = gl_buffer_usage(opts->usage);
    buf->comp_type = gl_comp_type[opts->comp_type];
    buf->comp_count = comp_count;

    if (opts->data && opts->size)
        buffer_load(buf, opts->data, opts->size,
                    buf->type == GL_ELEMENT_ARRAY_BUFFER ? -1 : 0);
}

void buffer_deinit(buffer_t *buf)
{
    if (!buf->loaded)
        return;
    GL(glDeleteBuffers(1, &buf->id));
    buf->loaded = false;
}

static inline void _buffer_bind(buffer_t *buf, int loc)
{
    GL(glVertexAttribPointer(loc, buf->comp_count, buf->comp_type, GL_FALSE, 0, (void *)0));
}

void buffer_bind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    GL(glBindBuffer(buf->type, buf->id));
    if (loc < 0)
        return;

    _buffer_bind(buf, loc);
    GL(glEnableVertexAttribArray(loc));
}

void buffer_unbind(buffer_t *buf, int loc)
{
    if (!buf->loaded)
        return;

    GL(glBindBuffer(buf->type, 0));
    if (loc < 0)
        return;

    GL(glDisableVertexAttribArray(loc));
}

void buffer_load(buffer_t *buf, void *data, size_t sz, int loc)
{
    GL(glGenBuffers(1, &buf->id));
    GL(glBindBuffer(buf->type, buf->id));
    GL(glBufferData(buf->type, sz, data, buf->usage));

    if (loc >= 0)
        _buffer_bind(buf, loc);

    GL(glBindBuffer(buf->type, 0));
    buf->loaded = true;
}

/****************************************************************************
 * Vertex Array Object
 ****************************************************************************/

static bool gl_does_vao(void)
{
#if defined(CONFIG_GLES) && !defined(CONFIG_BROWSER)
    return false;
#else
    return true;
#endif
}

static void vertex_array_drop(struct ref *ref)
{
    vertex_array_t *va = container_of(ref, vertex_array_t, ref);
    vertex_array_done(va);
}

DECLARE_REFCLASS(vertex_array);

void vertex_array_init(vertex_array_t *va)
{
    ref_embed(vertex_array, va);
    if (!gl_does_vao())
        return;

    GL(glGenVertexArrays(1, &va->vao));
    GL(glBindVertexArray(va->vao));
}

void vertex_array_done(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glDeleteVertexArrays(1, &va->vao));
}

void vertex_array_bind(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glBindVertexArray(va->vao));
}

void vertex_array_unbind(vertex_array_t *va)
{
    if (gl_does_vao())
        GL(glBindVertexArray(0));
}

/****************************************************************************
 * Texture
 ****************************************************************************/

static void texture_drop(struct ref *ref)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    texture_deinit(tex);
}
DECLARE_REFCLASS(texture);

static GLenum gl_texture_type(texture_type type, bool multisampled)
{
    switch (type) {
#ifdef CONFIG_GLES
        case TEX_2D:        return GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return GL_TEXTURE_2D_ARRAY;
#else
        case TEX_2D:        return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return multisampled ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
#endif
        case TEX_3D:        return GL_TEXTURE_3D;
        default:            break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_wrap(texture_wrap wrap)
{
    switch (wrap) {
        case TEX_WRAP_REPEAT:           return GL_REPEAT;
        case TEX_WRAP_MIRRORED_REPEAT:  return GL_MIRRORED_REPEAT;
        case TEX_CLAMP_TO_EDGE:         return GL_CLAMP_TO_EDGE;
        case TEX_CLAMP_TO_BORDER:       return GL_CLAMP_TO_BORDER;
        default:                        break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_filter(texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_LINEAR:            return GL_LINEAR;
        case TEX_FLT_NEAREST:           return GL_NEAREST;
        default:                        break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_format(texture_format format)
{
    switch (format) {
        case TEX_FMT_RGBA:  return GL_RGBA;
        case TEX_FMT_RGB:   return GL_RGB;
        case TEX_FMT_DEPTH: return GL_DEPTH_COMPONENT;
        default:            break;
    }

    clap_unreachable();

    return GL_NONE;
}

bool texture_is_array(texture_t *tex)
{
    return tex->type == GL_TEXTURE_2D_ARRAY ||
           tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
}

bool texture_is_multisampled(texture_t *tex)
{
    return tex->multisampled;
}

void _texture_init(texture_t *tex, const texture_init_options *opts)
{
    bool multisampled   = opts->multisampled;
#ifdef CONFIG_GLES
    multisampled  = false;
#endif /* CONFIG_GLES */

    ref_embed(texture, tex);
    tex->component_type = GL_UNSIGNED_BYTE;
    tex->wrap           = gl_texture_wrap(opts->wrap);
    tex->min_filter     = gl_texture_filter(opts->min_filter);
    tex->mag_filter     = gl_texture_filter(opts->mag_filter);
    tex->target         = GL_TEXTURE0 + opts->target;
    tex->type           = gl_texture_type(opts->type, multisampled);
    tex->layers         = opts->layers;
    tex->multisampled   = multisampled;
    if (opts->border)
        memcpy(tex->border, opts->border, sizeof(tex->border));
    GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL(glActiveTexture(tex->target));
    GL(glGenTextures(1, &tex->id));
}

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = ref_new(texture);

    if (ret) {
        ret->id             = tex->id;
        ret->wrap           = tex->wrap;
        ret->component_type = tex->component_type;
        ret->type           = tex->type;
        ret->target         = tex->target;
        ret->min_filter     = tex->min_filter;
        ret->mag_filter     = tex->mag_filter;
        ret->width          = tex->width;
        ret->height         = tex->height;
        ret->layers         = tex->layers;
        ret->format         = tex->format;
        ret->loaded         = tex->loaded;
        tex->loaded         = false;
    }

    return ret;
}

void texture_deinit(texture_t *tex)
{
    if (!tex->loaded)
        return;
    GL(glDeleteTextures(1, &tex->id));
    tex->loaded = false;
}

static cerr texture_storage(texture_t *tex, void *buf)
{
    if (tex->type == GL_TEXTURE_2D)
        GL(glTexImage2D(tex->type, 0, tex->internal_format, tex->width, tex->height,
                        0, tex->format, tex->component_type, buf));
    else if (tex->type == GL_TEXTURE_2D_ARRAY || tex->type == GL_TEXTURE_3D)
        GL(glTexImage3D(tex->type, 0, tex->internal_format, tex->width, tex->height, tex->layers,
                        0, tex->format, tex->component_type, buf));
#ifdef CONFIG_GLES
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE ||
             tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
         return CERR_NOT_SUPPORTED;
#else
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glTexImage2DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
                                   GL_TRUE));
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glTexImage3DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
           tex->layers, GL_TRUE));
#endif /* CONFIG_GLES */

    return CERR_OK;
}

static bool texture_size_valid(unsigned int width, unsigned int height)
{
    return width < gl_limits.gl_max_texture_size &&
           height < gl_limits.gl_max_texture_size;
}

cerr texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded)
        return CERR_TEXTURE_NOT_LOADED;

    if (tex->width == width && tex->height == height)
        return CERR_OK;

    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    tex->width = width;
    tex->height = height;
    GL(glBindTexture(tex->type, tex->id));
    cerr err = texture_storage(tex, NULL);
    GL(glBindTexture(tex->type, 0));

    return err;
}

static cerr texture_setup_begin(texture_t *tex, void *buf)
{
    GL(glActiveTexture(tex->target));
    GL(glBindTexture(tex->type, tex->id));
    if (tex->type != GL_TEXTURE_2D_MULTISAMPLE &&
        tex->type != GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
        GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_S, tex->wrap));
        GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_T, tex->wrap));
        if (tex->type == TEX_3D)
            GL(glTexParameteri(tex->type, GL_TEXTURE_WRAP_R, tex->wrap));
        GL(glTexParameteri(tex->type, GL_TEXTURE_MIN_FILTER, tex->min_filter));
        GL(glTexParameteri(tex->type, GL_TEXTURE_MAG_FILTER, tex->mag_filter));
#ifndef CONFIG_GLES
        if (tex->wrap == GL_CLAMP_TO_BORDER)
            GL(glTexParameterfv(tex->type, GL_TEXTURE_BORDER_COLOR, tex->border));
#endif /* CONFIG_GLES */
    }
    return texture_storage(tex, buf);
}

static void texture_setup_end(texture_t *tex)
{
    GL(glBindTexture(tex->type, 0));
}

cerr_check texture_load(texture_t *tex, texture_format format,
                        unsigned int width, unsigned int height, void *buf)
{
    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    tex->format = gl_texture_format(format);
    tex->internal_format = tex->format;
    tex->width  = width;
    tex->height = height;

    cerr err = texture_setup_begin(tex, buf);
    if (err)
        return err;

    texture_setup_end(tex);
    tex->loaded = true;

    return CERR_OK;
}

static cerr_check texture_fbo(texture_t *tex, GLuint attachment, GLenum format,
                              unsigned int width, unsigned int height)
{
    if (!texture_size_valid(width, height))
        return CERR_INVALID_TEXTURE_SIZE;

    tex->format = format;
    tex->internal_format = format;
    tex->width  = width;
    tex->height = height;
    if (attachment == GL_DEPTH_ATTACHMENT) {
#ifdef CONFIG_GLES
        tex->component_type = GL_UNSIGNED_SHORT;
        tex->internal_format = GL_DEPTH_COMPONENT16;
#else
        tex->component_type = GL_FLOAT;
        tex->internal_format = GL_DEPTH_COMPONENT32F;
#endif /* CONFIG_GLES */
    }
    cerr err = texture_setup_begin(tex, NULL);
    if (err)
        return err;

    if (tex->type == GL_TEXTURE_2D ||
        tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0));
#ifdef CONFIG_GLES
    else
        return CERR_NOT_SUPPORTED;
#else
    else if (tex->type == GL_TEXTURE_2D_ARRAY ||
             tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glFramebufferTexture(GL_FRAMEBUFFER, attachment, tex->id, 0));
    else
        GL(glFramebufferTexture3D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0, 0));
#endif
    texture_setup_end(tex);
    tex->loaded = true;

    return CERR_OK;
}

void texture_bind(texture_t *tex, unsigned int target)
{
    /* XXX: make tex->target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->type, tex->id));
}

void texture_unbind(texture_t *tex, unsigned int target)
{
    /* XXX: make tex->target useful for this instead */
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->type, 0));
}

void texture_get_dimesnions(texture_t *tex, unsigned int *pwidth, unsigned int *pheight)
{
    *pwidth = tex->width;
    *pheight = tex->height;
}

void texture_done(struct texture *tex)
{
    if (!ref_is_static(&tex->ref))
        ref_put_last(tex);
}

texid_t texture_id(struct texture *tex)
{
    if (!tex)
        return 0;
    return tex->id;
}

bool texture_loaded(struct texture *tex)
{
    return tex->loaded;
}

cerr_check texture_pixel_init(texture_t *tex, float color[4])
{
    texture_init(tex);
    return texture_load(tex, TEX_FMT_RGBA, 1, 1, color);
}

static texture_t _white_pixel;
static texture_t _black_pixel;
static texture_t _transparent_pixel;

texture_t *white_pixel(void) { return &_white_pixel; }
texture_t *black_pixel(void) { return &_black_pixel; }
texture_t *transparent_pixel(void) { return &_transparent_pixel; }

void textures_init(void)
{
    float white[] = { 1, 1, 1, 1 };
    CHECK0(texture_pixel_init(&_white_pixel, white));
    float black[] = { 0, 0, 0, 1 };
    CHECK0(texture_pixel_init(&_black_pixel, black));
    float transparent[] = { 0, 0, 0, 0 };
    CHECK0(texture_pixel_init(&_transparent_pixel, transparent));
}

void textures_done(void)
{
    texture_done(&_white_pixel);
    texture_done(&_black_pixel);
    texture_done(&_transparent_pixel);
}

/****************************************************************************
 * Framebuffer
 ****************************************************************************/

static int fbo_create(void)
{
    unsigned int fbo;

    GL(glGenFramebuffers(1, &fbo));
    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    return fbo;
}

static cerr_check fbo_texture_init(fbo_t *fbo)
{
    texture_init(&fbo->tex,
                 .multisampled  = fbo_is_multisampled(fbo),
                 .wrap          = TEX_CLAMP_TO_EDGE,
                 .min_filter    = TEX_FLT_LINEAR,
                 .mag_filter    = TEX_FLT_LINEAR);
    cerr err = texture_fbo(&fbo->tex, GL_COLOR_ATTACHMENT0, GL_RGBA, fbo->width, fbo->height);
    if (err)
        return err;

    fbo->attachment = GL_COLOR_ATTACHMENT0;

    return CERR_OK;
}

static cerr_check fbo_depth_texture_init(fbo_t *fbo)
{
    float border[] = { 1, 1, 1, 1 };
    texture_init(&fbo->tex,
#ifndef CONFIG_GLES
                 .type          = TEX_2D_ARRAY,
                 .layers        = CASCADES_MAX,
 #endif /* CONFIG_GLES */
                 .multisampled  = fbo_is_multisampled(fbo),
                 .wrap          = TEX_CLAMP_TO_BORDER,
                 .border        = border,
                 .min_filter    = TEX_FLT_NEAREST,
                 .mag_filter    = TEX_FLT_NEAREST);
    cerr err = texture_fbo(&fbo->tex, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT,
                           fbo->width, fbo->height);
    if (err)
        return err;

    fbo->attachment = GL_DEPTH_ATTACHMENT;

    return CERR_OK;
}

texture_t *fbo_texture(fbo_t *fbo)
{
    if (fbo_nr_attachments(fbo))
        return NULL;

    return &fbo->tex;
}

int fbo_width(fbo_t *fbo)
{
    return fbo->width;
}

int fbo_height(fbo_t *fbo)
{
    return fbo->height;
}

int fbo_nr_attachments(fbo_t *fbo)
{
    return darray_count(fbo->color_buf);
}

fbo_attachment fbo_get_attachment(fbo_t *fbo)
{
    if (fbo_nr_attachments(fbo))
        return FBO_ATTACHMENT_COLOR0;

    switch (fbo->attachment) {
        case GL_DEPTH_ATTACHMENT:
            return FBO_ATTACHMENT_DEPTH;
        case GL_COLOR_ATTACHMENT0:
            return FBO_ATTACHMENT_COLOR0;
        case GL_STENCIL_ATTACHMENT:
            return FBO_ATTACHMENT_STENCIL;
        default:
            break;
    }

    clap_unreachable();

    return GL_NONE;
}

static void __fbo_color_buffer_setup(fbo_t *fbo)
{
    if (fbo_is_multisampled(fbo))
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, fbo->nr_samples, GL_RGBA8,
                                            fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, fbo->width, fbo->height));
}

static int fbo_color_buffer(fbo_t *fbo, int output)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_color_buffer_setup(fbo);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + output, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

static void __fbo_depth_buffer_setup(fbo_t *fbo)
{
    if (fbo_is_multisampled(fbo))
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, fbo->nr_samples,
                                            GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
}

static int fbo_depth_buffer(fbo_t *fbo)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_depth_buffer_setup(fbo);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

cerr_check fbo_resize(fbo_t *fbo, int width, int height)
{
    if (!fbo)
        return CERR_INVALID_ARGUMENTS;

    fbo->width = width;
    fbo->height = height;
    GL(glFinish());

    cerr err = CERR_OK;
    if (texture_loaded(&fbo->tex))
        err = texture_resize(&fbo->tex, width, height);
    if (err)
        return err;

    int *color_buf;
    darray_for_each(color_buf, fbo->color_buf) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, *color_buf));
        __fbo_color_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    if (fbo->depth_buf >= 0) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->depth_buf));
        __fbo_depth_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    return CERR_OK;
}

#define NR_TARGETS 4
void fbo_prepare(fbo_t *fbo)
{
    GLenum buffers[NR_TARGETS];
    int target;

    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo));
    GL(glViewport(0, 0, fbo->width, fbo->height));

    if (!darray_count(fbo->color_buf)) {
        if (fbo->attachment == GL_DEPTH_ATTACHMENT) {
            buffers[0] = GL_NONE;
            GL(glDrawBuffers(1, buffers));
            GL(glReadBuffer(GL_NONE));
        }

        return;
    }

    for (target = 0; target < darray_count(fbo->color_buf); target++)
        buffers[target] = GL_COLOR_ATTACHMENT0 + target;
    GL(glDrawBuffers(darray_count(fbo->color_buf), buffers));
}

void fbo_done(fbo_t *fbo, int width, int height)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, width, height));
}

void fbo_blit_from_fbo(fbo_t *fbo, fbo_t *src_fbo, int attachment)
{
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo->fbo));
    GL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbo->fbo));
    GL(glReadBuffer(GL_COLOR_ATTACHMENT0 + attachment));
    GL(glBlitFramebuffer(0, 0, src_fbo->width, src_fbo->height,
                         0, 0, fbo->width, fbo->height,
                         GL_COLOR_BUFFER_BIT, GL_LINEAR));
}

static cerr fbo_make(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    darray_init(fbo->color_buf);
    fbo->depth_buf = -1;

    return CERR_OK;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    GL(glDeleteFramebuffers(1, &fbo->fbo));
    /* if the texture was cloned, its ->loaded==false making this a nop */
    texture_deinit(&fbo->tex);

    int *color_buf;
    darray_for_each(color_buf, fbo->color_buf)
        glDeleteRenderbuffers(1, (const GLuint *)color_buf);
    darray_clearout(fbo->color_buf);

    if (fbo->depth_buf >= 0)
        GL(glDeleteRenderbuffers(1, (GLuint *)&fbo->depth_buf));
}
DECLARE_REFCLASS2(fbo);

/*
 * nr_attachments:
 *  < 0: depth texture
 *  = 0: color texture
 *  > 0: number of color buffer attachments
 */
static cerr_check fbo_init(fbo_t *fbo, int nr_attachments)
{
    cerr err = CERR_OK;

    fbo->fbo = fbo_create();

    if (nr_attachments < 0) {
        err = fbo_depth_texture_init(fbo);
    } else if (!nr_attachments) {
        err = fbo_texture_init(fbo);
    } else {
        int target;

        for (target = 0; target < nr_attachments; target++) {
            int *color_buf = darray_add(fbo->color_buf);
            if (!color_buf)
                err = CERR_NOMEM;

            *color_buf = fbo_color_buffer(fbo, target);
        }
    }

    if (err)
        goto err;

    if (nr_attachments > 0)
        fbo->depth_buf = fbo_depth_buffer(fbo);

    int fb_err = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_err != GL_FRAMEBUFFER_COMPLETE) {
        err("framebuffer status: 0x%04X: %s\n", fb_err, gluErrorString(fb_err));
        err = CERR_FRAMEBUFFER_INCOMPLETE;
    }
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    return err;

err:
    darray_clearout(fbo->color_buf);
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glDeleteFramebuffers(1, &fbo->fbo));

    return err;
}

bool fbo_is_multisampled(fbo_t *fbo)
{
    return !!fbo->nr_samples;
}

must_check fbo_t *_fbo_new(const fbo_init_options *opts)
{
    fbo_t *fbo;

    fbo = ref_new(fbo);
    if (!fbo)
        return fbo;

    fbo->width        = opts->width;
    fbo->height       = opts->height;

    if (opts->nr_samples)
        fbo->nr_samples = opts->nr_samples;
    else if (opts->multisampled)
        fbo->nr_samples = MSAA_SAMPLES;

    cerr err = fbo_init(fbo, opts->nr_attachments);
    if (err) {
        ref_put_last(fbo);
        return NULL;
    }

    return fbo;
}

void fbo_put(fbo_t *fbo)
{
    ref_put(fbo);
}

void fbo_put_last(fbo_t *fbo)
{
    ref_put_last(fbo);
}

/****************************************************************************
 * Shaders
 ****************************************************************************/

static GLuint load_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) {
        err("couldn't create shader\n");
        return -1;
    }
    GL(glShaderSource(shader, 1, &source, NULL));
    GL(glCompileShader(shader));
    GLint compiled = 0;
    GL(glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled));
    if (!compiled) {
        GLint infoLen = 0;
        GL(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen));
        if (infoLen) {
            char *buf = mem_alloc(infoLen);
            if (buf) {
                GL(glGetShaderInfoLog(shader, infoLen, NULL, buf));
                err("Could not Compile Shader %d:\n%s\n", type, buf);
                mem_free(buf);
                err("--> %s <--\n", source);
            }
            GL(glDeleteShader(shader));
            shader = 0;
        }
    }
    return shader;
}

cerr shader_init(shader_t *shader, const char *vertex, const char *geometry, const char *fragment)
{
    shader->vert = load_shader(GL_VERTEX_SHADER, vertex);
    shader->geom =
#ifndef CONFIG_BROWSER
        geometry ? load_shader(GL_GEOMETRY_SHADER, geometry) : 0;
#else
        0;
#endif
    shader->frag = load_shader(GL_FRAGMENT_SHADER, fragment);
    shader->prog = glCreateProgram();
    GLint linkStatus = GL_FALSE;
    cerr ret = CERR_INVALID_SHADER;

    if (!shader->vert || (geometry && !shader->geom) || !shader->frag || !shader->prog) {
        err("vshader: %d gshader: %d fshader: %d program: %d\n",
            shader->vert, shader->geom, shader->frag, shader->prog);
        return ret;
    }

    GL(glAttachShader(shader->prog, shader->vert));
    GL(glAttachShader(shader->prog, shader->frag));
    if (shader->geom)
        GL(glAttachShader(shader->prog, shader->geom));
    GL(glLinkProgram(shader->prog));
    GL(glGetProgramiv(shader->prog, GL_LINK_STATUS, &linkStatus));
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        GL(glGetProgramiv(shader->prog, GL_INFO_LOG_LENGTH, &bufLength));
        if (bufLength) {
            char *buf = mem_alloc(bufLength);
            if (buf) {
                GL(glGetProgramInfoLog(shader->prog, bufLength, NULL, buf));
                err("Could not link program:\n%s\n", buf);
                mem_free(buf);
                GL(glDeleteShader(shader->vert));
                GL(glDeleteShader(shader->frag));
                if (shader->geom)
                    GL(glDeleteShader(shader->geom));
            }
        }
        GL(glDeleteProgram(shader->prog));
        shader->prog = 0;
    } else {
        ret = CERR_OK;
    }
    dbg("vshader: %d gshader: %d fshader: %d program: %d link: %d\n",
        shader->vert, shader->geom, shader->frag, shader->prog, linkStatus);

    return ret;
}

void shader_done(shader_t *shader)
{
    GL(glDeleteProgram(shader->prog));
    GL(glDeleteShader(shader->vert));
    GL(glDeleteShader(shader->frag));
    if (shader->geom)
        GL(glDeleteShader(shader->geom));
}

attr_t shader_attribute(shader_t *shader, const char *name)
{
    return glGetAttribLocation(shader->prog, name);
}

uniform_t shader_uniform(shader_t *shader, const char *name)
{
    return glGetUniformLocation(shader->prog, name);
}

void shader_use(shader_t *shader)
{
    GL(glUseProgram(shader->prog));
}

void shader_unuse(shader_t *shader)
{
    GL(glUseProgram(0));
}

void uniform_set_ptr(uniform_t uniform, data_type type, unsigned int count, const void *value)
{
    switch (type) {
        case DT_FLOAT: {
            GL(glUniform1fv(uniform, count, value));
            break;
        }
        case DT_INT: {
            GL(glUniform1iv(uniform, count, value));
            break;
        }
        case DT_VEC3: {
            GL(glUniform3fv(uniform, count, value));
            break;
        }
        case DT_VEC4: {
            GL(glUniform4fv(uniform, count, value));
            break;
        }
        case DT_MAT4: {
            GL(glUniformMatrix4fv(uniform, count, GL_FALSE, value));
            break;
        }
        default:
            break;
    }
}

/****************************************************************************
 * Renderer context
 ****************************************************************************/

void renderer_init(renderer_t *renderer)
{
    static_assert(sizeof(GLint) == sizeof(int), "GLint doesn't match int");
    static_assert(sizeof(GLenum) == sizeof(unsigned int), "GLenum doesn't match unsigned int");
    static_assert(sizeof(GLuint) == sizeof(unsigned int), "GLuint doesn't match unsigned int");
    static_assert(sizeof(GLsizei) == sizeof(int), "GLsizei doesn't match int");
    static_assert(sizeof(GLfloat) == sizeof(float), "GLfloat doesn't match float");
    static_assert(sizeof(GLushort) == sizeof(unsigned short), "GLushort doesn't match unsigned short");

    /* Clear the any error state */
    (void)glGetError();

    int i, nr_exts;
    const unsigned char *ext;

    GL(glGetIntegerv(GL_NUM_EXTENSIONS, &nr_exts));
    for (i = 0; i < nr_exts; i++) {
        GL(ext = glGetStringi(GL_EXTENSIONS, i));
        msg("GL extension: '%s'\n", ext);
    }

    GL(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_limits.gl_max_texture_size));
    GL(glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &gl_limits.gl_max_texture_units));
    GL(glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &gl_limits.gl_max_texture_layers));
    GL(glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &gl_limits.gl_max_color_attachments));
#ifndef CONFIG_GLES
    GL(glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &gl_limits.gl_max_color_texture_samples));
    GL(glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &gl_limits.gl_max_depth_texture_samples));
    GL(glEnable(GL_MULTISAMPLE));
#endif /* CONFIG_GLES */

    GL(glEnable(GL_CULL_FACE));
    GL(glCullFace(GL_BACK));
    renderer_cull_face(renderer, CULL_FACE_BACK);
    GL(glDisable(GL_BLEND));
    renderer_blend(renderer, false, BLEND_NONE, BLEND_NONE);
    GL(glEnable(GL_DEPTH_TEST));
    renderer_depth_test(renderer, true);
#ifndef EGL_EGL_PROTOTYPES
    GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif /* EGL_EGL_PROTOTYPES */
    renderer_wireframe(renderer, false);
}

void renderer_set_version(renderer_t *renderer, int major, int minor, renderer_profile profile)
{
    renderer->major     = major;
    renderer->minor     = minor;
    renderer->profile   = profile;
}

void renderer_viewport(renderer_t *r, int x, int y, int width, int height)
{
    if (r->x == x && r->y == y && r->width == width && r->height == height)
        return;

    r->x = x;
    r->y = y;
    r->width = width;
    r->height = height;
    GL(glViewport(x, y, width, height));
}

void renderer_get_viewport(renderer_t *r, int *px, int *py, int *pwidth, int *pheight)
{
    if (px)
        *px      = r->x;
    if (py)
        *py      = r->y;
    if (pwidth)
        *pwidth  = r->width;
    if (pheight)
        *pheight = r->height;
}

static GLenum gl_cull_face(cull_face cull)
{
    switch (cull) {
        case CULL_FACE_NONE:
            return GL_NONE;
        case CULL_FACE_FRONT:
            return GL_FRONT;
        case CULL_FACE_BACK:
            return GL_BACK;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

void renderer_cull_face(renderer_t *r, cull_face cull)
{
    GLenum gl_cull = gl_cull_face(cull);

    if (r->cull_face == gl_cull)
        return;

    r->cull_face = gl_cull;

    if (gl_cull != GL_NONE) {
        GL(glEnable(GL_CULL_FACE));
    } else {
        GL(glDisable(GL_CULL_FACE));
        return;
    }

    GL(glCullFace(r->cull_face));
}

static GLenum gl_blend(blend blend)
{
    switch (blend) {
        case BLEND_NONE:
            return GL_NONE;
        case BLEND_SRC_ALPHA:
            return GL_SRC_ALPHA;
        case BLEND_ONE_MINUS_SRC_ALPHA:
            return GL_ONE_MINUS_SRC_ALPHA;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

void renderer_blend(renderer_t *r, bool _blend, blend sfactor, blend dfactor)
{
    GLenum _sfactor = gl_blend(sfactor);
    GLenum _dfactor = gl_blend(dfactor);

    if (r->blend == _blend)
        return;

    r->blend = _blend;

    if (_blend) {
        GL(glEnable(GL_BLEND));
    } else {
        GL(glDisable(GL_BLEND));
        return;
    }

    if (r->blend_sfactor != _sfactor || r->blend_dfactor != _dfactor) {
        r->blend_sfactor = _sfactor;
        r->blend_dfactor = _dfactor;
    }
    GL(glBlendFunc(r->blend_sfactor, r->blend_dfactor));
}

void renderer_depth_test(renderer_t *r, bool enable)
{
    if (r->depth_test == enable)
        return;

    r->depth_test = enable;
    if (enable)
        GL(glEnable(GL_DEPTH_TEST));
    else
        GL(glDisable(GL_DEPTH_TEST));
}

void renderer_wireframe(renderer_t *r, bool enable)
{
    if (r->wireframe == enable)
        return;

    r->wireframe = enable;

#ifndef EGL_EGL_PROTOTYPES
    if (enable)
        GL(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
    else
        GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
#endif /* EGL_EGL_PROTOTYPES */
}

static GLenum gl_draw_type(draw_type draw_type)
{
    switch (draw_type) {
        case DRAW_TYPE_POINTS:
            return GL_POINTS;
        case DRAW_TYPE_LINE_STRIP:
            return GL_LINE_STRIP;
        case DRAW_TYPE_LINE_LOOP:
            return GL_LINE_LOOP;
        case DRAW_TYPE_LINES:
            return GL_LINES;
#ifndef CONFIG_GLES
        case DRAW_TYPE_LINE_STRIP_ADJACENCY:
            return GL_LINE_STRIP_ADJACENCY;
        case DRAW_TYPE_LINES_ADJACENCY:
            return GL_LINES_ADJACENCY;
        case DRAW_TYPE_TRIANGLES_ADJACENCY:
            return GL_TRIANGLES_ADJACENCY;
        case DRAW_TYPE_TRIANGLE_STRIP_ADJACENCY:
            return GL_TRIANGLE_STRIP_ADJACENCY;
#endif /* CONFIG_GLES */
        case DRAW_TYPE_TRIANGLE_STRIP:
            return GL_TRIANGLE_STRIP;
        case DRAW_TYPE_TRIANGLE_FAN:
            return GL_TRIANGLE_FAN;
        case DRAW_TYPE_TRIANGLES:
            return GL_TRIANGLES;
        case DRAW_TYPE_PATCHES:
            return GL_PATCHES;
        default:    break;
    }

    clap_unreachable();

    return GL_NONE;
}

void renderer_draw(renderer_t *r, draw_type draw_type, unsigned int nr_faces, data_type idx_type)
{
    err_on(idx_type >= array_size(gl_comp_type), "invalid draw type %u\n", idx_type);

    GLenum _idx_type = gl_comp_type[idx_type];
    GLenum _draw_type = gl_draw_type(draw_type);
    GL(glDrawElements(_draw_type, nr_faces, _idx_type, 0));
}

void renderer_clearcolor(renderer_t *r, vec4 color)
{
    if (!memcmp(r->clear_color, color, sizeof(vec4)))
        return;

    vec4_dup(r->clear_color, color);
    GL(glClearColor(color[0], color[1], color[2], color[3]));
}

void renderer_clear(renderer_t *r, bool color, bool depth, bool stencil)
{
    GLbitfield flags = 0;

    if (color)
        flags |= GL_COLOR_BUFFER_BIT;
    if (depth)
        flags |= GL_DEPTH_BUFFER_BIT;
    if (stencil)
        flags |= GL_STENCIL_BUFFER_BIT;

    GL(glClear(flags));
}
