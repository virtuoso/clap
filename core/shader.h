/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_SHADER_H__
#define __CLAP_SHADER_H__

#include "display.h"
#include "object.h"
#include "scene.h"

struct shader_data {
    GLint viewmx, transmx, lightp, lightc, projmx;
    GLint inv_viewmx, shine_damper, reflectivity;
    GLint highlight, color, ray, colorpt, use_normals;
    GLint use_skinning, joint_transforms, width, height;
    GLint attenuation;
};

struct shader_var;
struct shader_prog {
    const char  *name;
    GLuint      prog;
    GLuint      pos;
    GLint       norm;
    GLint       tangent;
    GLint       texture_map;
    GLint       normal_map;
    GLint       joints;
    GLint       weights;
    GLint       tex;
    struct ref  ref;
    struct list vars;
    struct shader_data data;
    struct list entry;
};

struct shader_prog *
shader_prog_from_strings(const char *name, const char *vsh, const char *fsh);
GLint shader_prog_find_var(struct shader_prog *p, const char *var);
void shader_prog_use(struct shader_prog *p);
void shader_prog_done(struct shader_prog *p);
struct shader_prog *shader_prog_find(struct list *shaders, const char *name);
int lib_request_shaders(const char *name, struct list *shaders);

#endif /* __CLAP_SHADER_H__ */
