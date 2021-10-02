#ifndef __CLAP_RENDER_H__
#define __CLAP_RENDER_H__

#include "display.h"
#include "logger.h"

static inline void __gl_check_error(void)
{
    GLenum err;

    err = glGetError();
    if (err != GL_NO_ERROR)
        err("GL error: %d\n", err);
}

static inline GLuint texture_gen(void)
{
    GLuint tex;

    glGenTextures(1, &tex);
    // dbg("tex_id >>> %u\n", tex);
    __gl_check_error();

    return tex;
}

static inline void texture_del(GLuint tex)
{
    if (!tex)
        return;
    // dbg("tex_id <<< %u\n", tex);
    glDeleteTextures(1, &tex);
    __gl_check_error();
}

#endif /* __CLAP_RENDER_H__ */
