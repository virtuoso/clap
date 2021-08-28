#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "display.h"
#include "object.h"
#include "scene.h"

struct shader_var;
struct shader_prog {
    const char  *name;
    GLuint      prog;
    /* XXX: we can now look these up */
    GLuint      pos;
    GLuint      norm;
    GLuint      tex;
    struct ref  ref;
    struct shader_var *var;
    struct shader_prog *next;
};

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *fsh);
GLint shader_prog_find_var(struct shader_prog *p, const char *var);
void shader_prog_use(struct shader_prog *p);
void shader_prog_done(struct shader_prog *p);
struct shader_prog *shader_prog_find(struct shader_prog *prog, const char *name);
int lib_request_shaders(const char *name, struct shader_prog **progp);

#endif /* __CLAP_SHADER_H__ */
