// SPDX-License-Identifier: Apache-2.0
#include "display.h"
#include "logger.h"
#include "object.h"

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

/****************************************************************************
 * Texture
 ****************************************************************************/

static void texture_drop(struct ref *ref)
{
    struct texture *tex = container_of(ref, struct texture, ref);
    texture_deinit(tex);
}
DECLARE_REFCLASS(texture);

static GLenum gl_texture_type(enum texture_type type, bool multisampled)
{
    switch (type) {
#ifdef CONFIG_GLES
        case TEX_2D:        return GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return GL_TEXTURE_2D_ARRAY;
#else
        case TEX_2D:        return multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
        case TEX_2D_ARRAY:  return multisampled ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_MULTISAMPLE;
#endif
        case TEX_3D:        return GL_TEXTURE_3D;
        default:            break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_wrap(enum texture_wrap wrap)
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

static GLenum gl_texture_filter(enum texture_filter filter)
{
    switch (filter) {
        case TEX_FLT_LINEAR:            return GL_LINEAR;
        case TEX_FLT_NEAREST:           return GL_NEAREST;
        default:                        break;
    }

    clap_unreachable();

    return GL_NONE;
}

static GLenum gl_texture_format(enum texture_format format)
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

int _texture_init(texture_t *tex, const texture_init_options *opts)
{
    ref_embed(texture, tex);
    tex->component_type = GL_UNSIGNED_BYTE;
    tex->wrap           = gl_texture_wrap(opts->wrap);
    tex->min_filter     = gl_texture_filter(opts->min_filter);
    tex->mag_filter     = gl_texture_filter(opts->mag_filter);
    tex->target         = GL_TEXTURE0 + opts->target;
    tex->type           = gl_texture_type(opts->type, opts->multisampled);
    tex->layers         = opts->layers;
    GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL(glActiveTexture(tex->target));
    GL(glGenTextures(1, &tex->id));

    return 0;
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

static void texture_storage(texture_t *tex, void *buf)
{
    if (tex->type == GL_TEXTURE_2D)
        GL(glTexImage2D(tex->type, 0, tex->internal_format, tex->width, tex->height,
                        0, tex->format, tex->component_type, buf));
#ifndef CONFIG_GLES
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glTexImage2DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
                                   GL_TRUE));
    else if (tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glTexImage3DMultisample(tex->type, 4, tex->internal_format, tex->width, tex->height,
           tex->layers, GL_TRUE));
#endif /* CONFIG_GLES */
    else if (tex->type == GL_TEXTURE_3D)
        GL(glTexImage3D(tex->type, 0, tex->internal_format, tex->width, tex->height, tex->layers,
                        0, tex->format, tex->component_type, buf));
}

void texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded || (tex->width == width && tex->height == height))
        return;

    GL(glBindTexture(tex->type, tex->id));
    GL(glTexImage2D(tex->type, 0, tex->internal_format, width, height,
                 0, tex->format, tex->component_type, NULL));
    GL(glBindTexture(tex->type, 0));
    tex->width = width;
    tex->height = height;
}

void texture_filters(texture_t *tex, GLint wrap, GLint filter)
{
    tex->wrap = wrap;
    tex->min_filter = filter;
    tex->mag_filter = filter;
}

static void texture_setup_begin(texture_t *tex, void *buf)
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
    }
    texture_storage(tex, buf);
}

static void texture_setup_end(texture_t *tex)
{
    GL(glBindTexture(tex->type, 0));
}

void texture_load(texture_t *tex, enum texture_format format,
                  unsigned int width, unsigned int height, void *buf)
{
    tex->format = gl_texture_format(format);
    tex->internal_format = tex->format;
    tex->width  = width;
    tex->height = height;
    texture_setup_begin(tex, buf);
    texture_setup_end(tex);
    tex->loaded = true;
}

static void texture_fbo(texture_t *tex, GLuint attachment, GLenum format, unsigned int width,
                        unsigned int height)
{
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
    texture_setup_begin(tex, NULL);
    if (tex->type == GL_TEXTURE_2D ||
        tex->type == GL_TEXTURE_2D_MULTISAMPLE)
        GL(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0));
    else if (tex->type == GL_TEXTURE_2D_ARRAY ||
             tex->type == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)
        GL(glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, tex->id, 0, 0));
#ifndef CONFIG_GLES
    else
        GL(glFramebufferTexture3D(GL_FRAMEBUFFER, attachment, tex->type, tex->id, 0, 0));
#endif
    texture_setup_end(tex);
    tex->loaded = true;
}

void texture_bind(texture_t *tex, unsigned int target)
{
    GL(glActiveTexture(GL_TEXTURE0 + target));
    GL(glBindTexture(tex->type, tex->id));
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

GLuint texture_id(struct texture *tex)
{
    if (!tex)
        return -1;
    return tex->id;
}

bool texture_loaded(struct texture *tex)
{
    return tex->loaded;
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

static void fbo_texture_init(fbo_t *fbo)
{
    texture_init(&fbo->tex,
                 .wrap          = TEX_CLAMP_TO_EDGE,
                 .min_filter    = TEX_FLT_LINEAR,
                 .mag_filter    = TEX_FLT_LINEAR);
    texture_fbo(&fbo->tex, GL_COLOR_ATTACHMENT0, GL_RGBA, fbo->width, fbo->height);
    fbo->attachment = GL_COLOR_ATTACHMENT0;
}

static void fbo_depth_texture_init(fbo_t *fbo)
{
    texture_init(&fbo->tex,
#ifndef CONFIG_GLES
                 .type          = TEX_2D_ARRAY,
                 .layers        = 3,
 #endif /* CONFIG_GLES */
                 .multisampled  = true,
                 .wrap          = TEX_WRAP_REPEAT,
                 .min_filter    = TEX_FLT_NEAREST,
                 .mag_filter    = TEX_FLT_NEAREST);
    texture_fbo(&fbo->tex, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT, fbo->width, fbo->height);

    fbo->attachment = GL_DEPTH_ATTACHMENT;
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

static void __fbo_color_buffer_setup(fbo_t *fbo)
{
    if (fbo->multisampled)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, fbo->width, fbo->height));
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
    if (fbo->multisampled)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
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

void fbo_resize(fbo_t *fbo, int width, int height)
{
    if (!fbo)
        return;
    fbo->width = width;
    fbo->height = height;
    GL(glFinish());
    if (texture_loaded(&fbo->tex))
        texture_resize(&fbo->tex, width, height);

    int *color_buf;
    darray_for_each(color_buf, &fbo->color_buf) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, *color_buf));
        __fbo_color_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }

    if (fbo->depth_buf >= 0) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->depth_buf));
        __fbo_depth_buffer_setup(fbo);
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }
}

#define NR_TARGETS 4
void fbo_prepare(fbo_t *fbo)
{
    GLenum buffers[NR_TARGETS];
    texture_t *tex = &fbo->tex;
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

static int fbo_make(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    darray_init(&fbo->color_buf);
    fbo->depth_buf = -1;
    fbo->multisampled = false;

    return 0;
}

static void fbo_drop(struct ref *ref)
{
    fbo_t *fbo = container_of(ref, fbo_t, ref);

    // dbg("dropping FBO %d: %d/%d/%d\n", fbo->fbo, fbo->tex, fbo->depth_tex, fbo->depth_buf);
    GL(glDeleteFramebuffers(1, &fbo->fbo));
    /* if the texture was cloned, its ->loaded==false making this a nop */
    texture_deinit(&fbo->tex);

    int *color_buf;
    darray_for_each(color_buf, &fbo->color_buf)
        glDeleteRenderbuffers(1, (const GLuint *)color_buf);
    darray_clearout(&fbo->color_buf.da);

    if (fbo->depth_buf >= 0)
        GL(glDeleteRenderbuffers(1, (GLuint *)&fbo->depth_buf));
    // ref_free(fbo);
}
DECLARE_REFCLASS2(fbo);

/*
 * nr_attachments:
 *  < 0: depth texture
 *  = 0: color texture
 *  > 0: number of color buffer attachments
 */
static void fbo_init(fbo_t *fbo, int nr_attachments)
{
    int err;

    fbo->fbo = fbo_create();

    if (nr_attachments < 0) {
        fbo_depth_texture_init(fbo);
    } else if (!nr_attachments) {
        fbo_texture_init(fbo);
    } else {
        int target;

        for (target = 0; target < nr_attachments; target++) {
            int *color_buf = darray_add(&fbo->color_buf.da);
            *color_buf = fbo_color_buffer(fbo, target);
        }
    }

    if (nr_attachments > 0)
        fbo->depth_buf = fbo_depth_buffer(fbo);

    err = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    err_on(err != GL_FRAMEBUFFER_COMPLETE, "framebuffer status: 0x%04X: %s\n",
           err, gluErrorString(err));
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

fbo_t *fbo_new_ms(int width, int height, bool multisampled, int nr_attachments)
{
    fbo_t *fbo;

    /* multisampled buffer requires color attachment buffers */
    if (multisampled && nr_attachments <= 0)
        return NULL;

    CHECK(fbo = ref_new(fbo));
    fbo->width = width;
    fbo->height = height;
    fbo->multisampled = multisampled;
    fbo_init(fbo, nr_attachments);

    return fbo;
}

fbo_t *fbo_new(int width, int height)
{
    return fbo_new_ms(width, height, false, FBO_COLOR_TEXTURE);
}

void fbo_put(fbo_t *fbo)
{
    ref_put(fbo);
}

void fbo_put_last(fbo_t *fbo)
{
    ref_put_last(fbo);
}
