// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"
//#include "matrix.h"
#include "display.h"
#include "util.h"
#include "object.h"
#include "shader.h"
#include "common.h"
#include "librarian.h"
#include "scene.h"

static GLuint loadShader(GLenum shaderType, const char *shaderSource)
{
    GLuint shader = glCreateShader(shaderType);
    if (!shader) {
        err("couldn't create shader\n");
        return -1;
    }
    glShaderSource(shader, 1, &shaderSource, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen) {
            char *buf = malloc(infoLen);
            if (buf) {
                glGetShaderInfoLog(shader, infoLen, NULL, buf);
                err("Could not Compile Shader %d:\n%s\n", shaderType, buf);
                free(buf);
                err("--> %s <--\n", shaderSource);
            }
            glDeleteShader(shader);
            shader = 0;
        }
    }
    return shader;
}

static GLuint createProgram(const char* vertexSource, const char * fragmentSource)
{
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);
    GLuint program = glCreateProgram();
    GLint linkStatus = GL_FALSE;

    if (!vertexShader || !fragmentShader || !program) {
        err("vshader: %d fshader: %d program: %d\n",
            vertexShader, fragmentShader, program);
        return 0;
    }

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (linkStatus != GL_TRUE) {
        GLint bufLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
        if (bufLength) {
            char *buf = malloc(bufLength);
            if (buf) {
                glGetProgramInfoLog(program, bufLength, NULL, buf);
                err("Could not link program:\n%s\n", buf);
                free(buf);
            }
        }
        glDeleteProgram(program);
        program = 0;
    }
    dbg("vshader: %d fshader: %d program: %d link: %d\n",
        vertexShader, fragmentShader, program, linkStatus);

    return program;
}

struct shader_var {
    char *name;
    GLint loc;
    struct shader_var *next;
};

/* XXX use a hashlist */
GLint shader_prog_find_var(struct shader_prog *p, const char *var)
{
    struct shader_var *v;

    for (v = p->var; v; v = v->next)
        if (!strcmp(v->name, var))
            return v->loc;
    return -1;
}

static int shader_prog_scan(struct shader_prog *p, const char *txt)
{
    static const char *vars[] = {"in", "uniform", "attribute"};/* "varying"? */
    const char *pos, *pend;
    struct shader_var *v;
    int i;

    for (i = 0; i < array_size(vars); i++) {
        for (pos = txt;;) {
            pos = strstr(pos, vars[i]);
            if (!pos)
                break;

            pos += strlen(vars[i]);
            if (!isspace(*pos))
                continue;

            /* skip the variable qualifier */
            pos = skip_nonspace(pos);
            /* whitespace */
            pos = skip_space(pos);
            /* skip the type */
            pos = skip_nonspace(pos);
            /* whitespace */
            pos = skip_space(pos);
            /* the actual variable */
            for (pend = pos; *pend && !isspace(*pend) && *pend != ';' && *pend != '['; pend++)
                ;
            v = malloc(sizeof(*v));
            if (!v)
                return -ENOMEM;
            v->name = strndup(pos, pend - pos);
            switch (i) {
                case 0:
                case 2:
                    v->loc = glGetAttribLocation(p->prog, v->name);
                    break;
                case 1:
                    v->loc = glGetUniformLocation(p->prog, v->name);
                    break;
            }
            //dbg("# found var '%s' @%d\n", v->name, v->loc);
            v->next = p->var;
            p->var = v;
            pos = pend;
        }
    }

    return 0;
}

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    dbg("dropping shader '%s'\n", p->name);
}

DECLARE_REFCLASS(shader_prog);

static void shader_prog_link(struct shader_prog *p)
{
    p->data.width        = shader_prog_find_var(p, "width");
    p->data.height       = shader_prog_find_var(p, "height");
    p->data.projmx       = shader_prog_find_var(p, "proj");
    p->data.viewmx       = shader_prog_find_var(p, "view");
    p->data.transmx      = shader_prog_find_var(p, "trans");
    p->data.inv_viewmx   = shader_prog_find_var(p, "inverse_view");
    p->data.lightp       = shader_prog_find_var(p, "light_pos");
    p->data.lightc       = shader_prog_find_var(p, "light_color");
    p->data.shine_damper = shader_prog_find_var(p, "shine_damper");
    p->data.reflectivity = shader_prog_find_var(p, "reflectivity");
    p->data.highlight    = shader_prog_find_var(p, "highlight_color");
    p->data.ray          = shader_prog_find_var(p, "ray");
    p->data.color        = shader_prog_find_var(p, "in_color");
    p->data.colorpt      = shader_prog_find_var(p, "color_passthrough");
    p->data.use_normals  = shader_prog_find_var(p, "use_normals");
    p->data.use_skinning  = shader_prog_find_var(p, "use_skinning");
    p->data.joint_transforms = shader_prog_find_var(p, "joint_transforms");
}

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *fsh)
{
    struct shader_prog *p;

    p = ref_new(shader_prog);
    if (!p)
        return NULL;

    p->name = name;
    p->prog = createProgram(vsh, fsh);
    if (!p->prog) {
        err("couldn't create program '%s'\n", name);
        free(p);
        return NULL;
    }

    shader_prog_use(p);
    shader_prog_scan(p, vsh);
    shader_prog_scan(p, fsh);
    shader_prog_done(p);
    p->pos         = shader_prog_find_var(p, "position");
    p->norm        = shader_prog_find_var(p, "normal");
    p->tex         = shader_prog_find_var(p, "tex");
    p->tangent     = shader_prog_find_var(p, "tangent");
    p->texture_map = shader_prog_find_var(p, "model_tex");
    p->normal_map  = shader_prog_find_var(p, "normal_map");
    p->joints      = shader_prog_find_var(p, "joints");
    p->weights     = shader_prog_find_var(p, "weights");
    dbg("model '%s' %d/%d/%d/%d/%d/%d/%d/%d\n",
        p->name, p->pos, p->norm, p->tex, p->tangent,
        p->texture_map, p->normal_map, p->joints, p->weights);
    if (p->pos < 0) {
        shader_prog_done(p);
	return NULL;
    }
    shader_prog_link(p);
    return p;
}

void shader_prog_use(struct shader_prog *p)
{
    ref_get(p);
    glUseProgram(p->prog);
}

void shader_prog_done(struct shader_prog *p)
{
    glUseProgram(0);
    ref_put(p);
}

struct shader_prog *shader_prog_find(struct shader_prog *prog, const char *name)
{
    for (; prog; prog = prog->next)
        if (!strcmp(prog->name, name))
            return ref_get(prog);

    return NULL;
}

int lib_request_shaders(const char *name, struct shader_prog **progp)
{
    //char *nvert CUX(string), *nfrag CUX(string), *vert CUX(string), *frag CUX(string);
    struct lib_handle *hv, *hf;
    LOCAL(char, nvert);
    LOCAL(char, nfrag);
    char *vert;
    char *frag;
    struct shader_prog *p;
    size_t vsz, fsz;

    CHECK(asprintf(&nvert, "%s.vert", name));
    CHECK(asprintf(&nfrag, "%s.frag", name));
    hv = lib_read_file(RES_SHADER, nvert, (void **)&vert, &vsz);
    hf = lib_read_file(RES_SHADER, nfrag, (void **)&frag, &fsz);
    /* XXX: if handle(s) exist, but in error state, this leaks them */
    if (!hv || !hf || hv->state == RES_ERROR || hf->state == RES_ERROR)
        return -1;

    p = shader_prog_from_strings(name, vert, frag);
    if (!p)
        return -1;

    p->next = *progp;
    *progp = p;
    ref_put_last(hv);
    ref_put_last(hf);

    return 0;
}
