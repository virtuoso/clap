// SPDX-License-Identifier: Apache-2.0
#include "display.h"
#include "logger.h"
#include "object.h"

#define IMPLEMENTOR
#include "render.h"
#undef IMPLEMENTOR

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
