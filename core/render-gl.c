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

int texture_init_target(texture_t *tex, GLuint target)
{
    ref_embed(texture, tex);
    tex->type = GL_UNSIGNED_BYTE;
    tex->wrap = GL_CLAMP_TO_EDGE;
    tex->filter = GL_LINEAR;
    tex->target = target;
    GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GL(glActiveTexture(tex->target));
    GL(glGenTextures(1, &tex->id));

    return 0;
}

int texture_init(texture_t *tex)
{
    return texture_init_target(tex, GL_TEXTURE0);
}

texture_t *texture_clone(texture_t *tex)
{
    texture_t *ret = ref_new(texture);

    if (ret) {
        ret->id     = tex->id;
        ret->wrap   = tex->wrap; 
        ret->type   = tex->type;
        ret->target = tex->target; 
        ret->filter = tex->filter;
        ret->width  = tex->width;
        ret->height = tex->height;
        ret->format = tex->format;
        ret->loaded = tex->loaded;
        tex->loaded = false;
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

void texture_resize(texture_t *tex, unsigned int width, unsigned int height)
{
    if (!tex->loaded || (tex->width == width && tex->height == height))
        return;

    GL(glBindTexture(GL_TEXTURE_2D, tex->id));
    GL(glTexImage2D(GL_TEXTURE_2D, 0, tex->format, width, height,
                 0, tex->format, tex->type, NULL));
    GL(glBindTexture(GL_TEXTURE_2D, 0));
    tex->width = width;
    tex->height = height;
}

void texture_filters(texture_t *tex, GLint wrap, GLint filter)
{
    tex->wrap = wrap;
    tex->filter = filter;
}

static void texture_setup_begin(texture_t *tex, void *buf)
{
    GL(glActiveTexture(tex->target));
    GL(glBindTexture(GL_TEXTURE_2D, tex->id));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, tex->wrap));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, tex->wrap));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex->filter));
    GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, tex->filter));
    GL(glTexImage2D(GL_TEXTURE_2D, 0, tex->internal_format, tex->width, tex->height,
                 0, tex->format, tex->type, buf));
}

static void texture_setup_end(texture_t *tex)
{
    GL(glBindTexture(GL_TEXTURE_2D, 0));
}

void texture_load(texture_t *tex, GLenum format, unsigned int width, unsigned int height,
                  void *buf)
{
    tex->format = format;
    tex->internal_format = format;
    tex->width  = width;
    tex->height = height;
    texture_setup_begin(tex, buf);
    texture_setup_end(tex);
    tex->loaded = true;
}

void texture_fbo(texture_t *tex, GLuint attachment, GLenum format, unsigned int width,
                 unsigned int height)
{
    tex->format = format;
    tex->internal_format = format;
    tex->width  = width;
    tex->height = height;
    if (attachment == GL_DEPTH_ATTACHMENT) {
        tex->type = GL_UNSIGNED_SHORT;
        tex->internal_format = GL_DEPTH_COMPONENT16;
    }
    texture_setup_begin(tex, NULL);
    GL(glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, tex->id, 0));
    texture_setup_end(tex);
    tex->loaded = true;
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

static void fbo_texture_init(struct fbo *fbo)
{
    texture_init(&fbo->tex);
    texture_filters(&fbo->tex, GL_CLAMP_TO_EDGE, GL_LINEAR);
    texture_fbo(&fbo->tex, GL_COLOR_ATTACHMENT0, GL_RGBA, fbo->width, fbo->height);
}

static void fbo_depth_texture_init(struct fbo *fbo)
{
    texture_init(&fbo->tex);
    texture_filters(&fbo->tex, GL_REPEAT, GL_NEAREST);
    texture_fbo(&fbo->tex, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT, fbo->width, fbo->height);

    GLenum buffers[1] = { GL_NONE };
    GL(glDrawBuffers(1, buffers));
    GL(glReadBuffer(GL_NONE));
}

static void __fbo_color_buffer_setup(struct fbo *fbo)
{
    if (fbo->ms)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, fbo->width, fbo->height));
}

static int fbo_color_buffer(struct fbo *fbo, int output)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_color_buffer_setup(fbo);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + output, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

static void __fbo_depth_buffer_setup(struct fbo *fbo)
{
    if (fbo->ms)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
}

static int fbo_depth_buffer(struct fbo *fbo)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    __fbo_depth_buffer_setup(fbo);
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

void fbo_resize(struct fbo *fbo, int width, int height)
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
void fbo_prepare(struct fbo *fbo)
{
    GLenum buffers[NR_TARGETS];
    int target;

    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo));
    GL(glViewport(0, 0, fbo->width, fbo->height));

    if (!darray_count(fbo->color_buf))
        return;

    for (target = 0; target < darray_count(fbo->color_buf); target++)
        buffers[target] = GL_COLOR_ATTACHMENT0 + target;
    GL(glDrawBuffers(darray_count(fbo->color_buf), buffers));
}

void fbo_done(struct fbo *fbo, int width, int height)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, width, height));
}

void fbo_blit_from_fbo(struct fbo *fbo, struct fbo *src_fbo, int attachment)
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
    struct fbo *fbo = container_of(ref, struct fbo, ref);

    darray_init(&fbo->color_buf);
    fbo->depth_buf = -1;
    fbo->ms        = false;

    return 0;
}

static void fbo_drop(struct ref *ref)
{
    struct fbo *fbo = container_of(ref, struct fbo, ref);

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
static void fbo_init(struct fbo *fbo, int nr_attachments)
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
    if (err != GL_FRAMEBUFFER_COMPLETE)
        warn("## framebuffer status: %d\n", err);
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

struct fbo *fbo_new_ms(int width, int height, bool ms, int nr_attachments)
{
    struct fbo *fbo;

    /* multisampled buffer requires color attachment buffers */
    if (ms && nr_attachments <= 0)
        return NULL;

    CHECK(fbo = ref_new(fbo));
    fbo->width = width;
    fbo->height = height;
    fbo->ms = ms;
    fbo_init(fbo, nr_attachments);

    return fbo;
}

struct fbo *fbo_new(int width, int height)
{
    return fbo_new_ms(width, height, false, FBO_COLOR_TEXTURE);
}
