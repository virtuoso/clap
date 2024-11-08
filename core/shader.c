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

static void shader_prog_drop(struct ref *ref)
{
    struct shader_prog *p = container_of(ref, struct shader_prog, ref);

    GL(glDeleteProgram(p->prog));
    list_del(&p->entry);
    dbg("dropping shader '%s'\n", p->name);
}

DECLARE_REFCLASS(shader_prog);

static void shader_prog_link(struct shader_prog *p)
{
    p->data.width        = glGetUniformLocation(p->prog, "width");
    p->data.height       = glGetUniformLocation(p->prog, "height");
    p->data.projmx       = glGetUniformLocation(p->prog, "proj");
    p->data.viewmx       = glGetUniformLocation(p->prog, "view");
    p->data.transmx      = glGetUniformLocation(p->prog, "trans");
    p->data.inv_viewmx   = glGetUniformLocation(p->prog, "inverse_view");
    p->data.lightp       = glGetUniformLocation(p->prog, "light_pos");
    p->data.lightc       = glGetUniformLocation(p->prog, "light_color");
    p->data.attenuation  = glGetUniformLocation(p->prog, "attenuation");
    p->data.shine_damper = glGetUniformLocation(p->prog, "shine_damper");
    p->data.reflectivity = glGetUniformLocation(p->prog, "reflectivity");
    p->data.highlight    = glGetUniformLocation(p->prog, "highlight_color");
    p->data.ray          = glGetUniformLocation(p->prog, "ray");
    p->data.color        = glGetUniformLocation(p->prog, "in_color");
    p->data.colorpt      = glGetUniformLocation(p->prog, "color_passthrough");
    p->data.entity_hash  = glGetUniformLocation(p->prog, "entity_hash");
    p->data.use_normals  = glGetUniformLocation(p->prog, "use_normals");
    p->data.use_skinning  = glGetUniformLocation(p->prog, "use_skinning");
    p->data.albedo_texture  = glGetUniformLocation(p->prog, "albedo_texture");
    p->data.joint_transforms = glGetUniformLocation(p->prog, "joint_transforms");
}

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *fsh)
{
    struct shader_prog *p;

    p = ref_new(shader_prog);
    if (!p)
        return NULL;

    list_init(&p->entry);
    p->name = name;
    p->prog = createProgram(vsh, fsh);
    if (!p->prog) {
        err("couldn't create program '%s'\n", name);
        ref_put(p);
        return NULL;
    }

    shader_prog_use(p);
    p->pos         = glGetAttribLocation(p->prog, "position");
    p->norm        = glGetAttribLocation(p->prog, "normal");
    p->tex         = glGetAttribLocation(p->prog, "tex");
    p->tangent     = glGetAttribLocation(p->prog, "tangent");
    p->joints      = glGetAttribLocation(p->prog, "joints");
    p->weights     = glGetAttribLocation(p->prog, "weights");
    p->texture_map = glGetUniformLocation(p->prog, "model_tex");
    p->normal_map  = glGetUniformLocation(p->prog, "normal_map");
    p->sobel_tex   = glGetUniformLocation(p->prog, "sobel_tex");
    p->emission_map = glGetUniformLocation(p->prog, "emission_map");
    shader_prog_link(p);
    shader_prog_done(p);
    dbg("model '%s' %d/%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
        p->name, p->pos, p->norm, p->tex, p->tangent,
        p->texture_map, p->normal_map, p->emission_map, p->sobel_tex, p->joints, p->weights);
    if (p->pos < 0) {
        err("program '%s' doesn't have position attribute\n", p->name);
        ref_put_last(p);
        return NULL;
    }
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

struct shader_prog *shader_prog_find(struct list *shaders, const char *name)
{
    struct shader_prog *prog;

    list_for_each_entry(prog, shaders, entry)
        if (!strcmp(prog->name, name))
            return ref_get(prog);

    return NULL;
}

int lib_request_shaders(const char *name, struct list *shaders)
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

    list_append(shaders, &p->entry);
    ref_put_last(hv);
    ref_put_last(hf);

    return 0;
}
