/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MODEL_H__
#define __CLAP_MODEL_H__

//#include <ode/ode.h>
#include "common.h"
#include "object.h"
#include "librarian.h"
#include "display.h" /* XXX: OpenGL headers are included there */
#include "physics.h"
#include "matrix.h"
#include "mesh.h"
#include "render.h"
#include "shader.h"

struct scene;
struct light;
struct camera;
struct shader_prog;

struct model_joint {
    darray(int, children);
    char        *name;
    mat4x4      invmx;
    int         id;
};

enum chan_path {
    PATH_TRANSLATION = 0,
    PATH_ROTATION,
    PATH_SCALE,
    PATH_NONE,
};

struct joint {
    vec3    translation;
    quat    rotation;
    vec3    scale;
    mat4x4  global;
    int     off[PATH_NONE];
};

struct channel {
    float           *time;
    float           *data;
    unsigned int    nr;
    unsigned int    stride;
    unsigned int    target;
    unsigned int    path;
};

struct animation {
    char            *name;
    struct model3d  *model;
    struct channel  *channels;
    unsigned int    nr_channels;
    unsigned int    cur_channel;
    float           time_end;
};

struct animation *animation_new(struct model3d *model, const char *name, unsigned int nr_channels);
/*
 * Delete an animation from the @model's animation array and free all its
 * contents
 */
void animation_delete(struct animation *an);
void animation_add_channel(struct animation *an, size_t frames, float *time, float *data,
                           size_t data_stride, unsigned int target, unsigned int path);
void animation_start(struct entity3d *e, struct scene *scene, int ani);
void animation_push_by_name(struct entity3d *e, struct scene *s, const char *name,
                            bool clear, bool repeat);
int animation_by_name(struct model3d *m, const char *name);
void animation_set_end_callback(struct entity3d *e, void (*end)(struct scene *, void *), void *priv);
void animation_set_speed(struct entity3d *e, float speed);

#define LOD_MAX 4
struct model3d {
    char                *name;
    struct ref          ref;
    struct shader_prog  *prog;
    bool                cull_face;
    bool                alpha_blend;
    bool                debug;
    unsigned int        draw_type;
    unsigned int        nr_joints;
    unsigned int        root_joint;
    unsigned int        nr_lods;
    int                 cur_lod;
    float               aabb[6];
    darray(struct animation, anis);
    mat4x4              root_pose;
    GLuint              vao;
    buffer_t            vertex;
    buffer_t            index[LOD_MAX];
    buffer_t            tex;
    buffer_t            norm;
    buffer_t            tangent;
    buffer_t            vjoints;
    buffer_t            weights;
    GLuint              nr_vertices;
    GLuint              nr_faces[LOD_MAX];
    struct model_joint  *joints;
    /* Collision mesh, if needed */
    float               *collision_vx;
    size_t              collision_vxsz;
    unsigned short      *collision_idx;
    size_t              collision_idxsz;
};

struct model3dtx {
    struct model3d *model;
    texture_t      _texture;
    texture_t      _normals;
    texture_t      _emission;
    texture_t      _sobel;
    texture_t      *texture;
    texture_t      *normals;
    texture_t      *emission;
    texture_t      *sobel;
    float          metallic;
    float          roughness;
    struct ref     ref;
    struct list    entry;              /* link to scene/ui->txmodels */
    struct list    entities;           /* links entity3d->entry */
};

struct model3d *model3d_new_from_vectors(const char *name, struct shader_prog *p, GLfloat *vx, size_t vxsz,
                                         GLushort *idx, size_t idxsz, GLfloat *tx, size_t txsz, GLfloat *norm,
                                         size_t normsz);
struct model3d *model3d_new_from_mesh(const char *name, struct shader_prog *p, struct mesh *mesh);
void model3d_add_tangents(struct model3d *m, float *tg, size_t tgsz);
int model3d_add_skinning(struct model3d *m, unsigned char *joints, size_t jointssz,
                         float *weights, size_t weightssz, size_t nr_joints, mat4x4 *invmxs);
void model3d_set_name(struct model3d *m, const char *fmt, ...);
float model3d_aabb_X(struct model3d *m);
float model3d_aabb_Y(struct model3d *m);
float model3d_aabb_Z(struct model3d *m);
struct model3dtx *model3dtx_new(struct model3d *m, const char *name);
struct model3dtx *model3dtx_new2(struct model3d *model, const char *tex, const char *norm);
struct model3dtx *model3dtx_new_from_png_buffers(struct model3d *model, void *tex, size_t texsz, void *norm, size_t normsz,
                                                 void *em, size_t emsz);
struct model3dtx *model3dtx_new_texture(struct model3d *model, texture_t *tex);
void model3dtx_set_texture(struct model3dtx *txm, enum shader_vars var, texture_t *tex);
struct model3d *model3d_new_cube(struct shader_prog *p);
struct model3d *model3d_new_quad(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_quadrev(struct shader_prog *p, float x, float y, float z, float w, float h);
struct model3d *model3d_new_frame(struct shader_prog *p, float x, float y, float z, float w, float h, float t);
void model3dtx_prepare(struct model3dtx *m, struct shader_prog *p);
void model3dtx_done(struct model3dtx *m, struct shader_prog *p);
void model3dtx_draw(struct model3dtx *m);

static inline const char *txmodel_name(struct model3dtx *txm)
{
    return txm->model->name;
}

struct mq {
    struct list     txmodels;
    void            *priv;
};

void mq_init(struct mq *mq, void *priv);
void mq_release(struct mq *mq);
void mq_update(struct mq *mq);
void mq_for_each(struct mq *mq, void (*cb)(struct entity3d *, void *), void *data);
struct model3dtx *mq_model_first(struct mq *mq);
struct model3dtx *mq_model_last(struct mq *mq);
void mq_add_model(struct mq *mq, struct model3dtx *txmodel);
void mq_add_model_tail(struct mq *mq, struct model3dtx *txmodel);
struct model3dtx *mq_nonempty_txm_next(struct mq *mq, struct model3dtx *txm, bool fwd);

/* XXX: find a better place; util.h not good */
static inline float cos_interp(float a, float b, float blend)
{
    float theta = blend * M_PI;
    float  f = (1.f - cosf(theta)) / 2.f;
    return a * (1.f - f) + b * f;
}

static inline float barrycentric(vec3 p1, vec3 p2, vec3 p3, vec2 pos)
{
    float det = (p2[2] - p3[2]) * (p1[0] - p3[0]) + (p3[0] - p2[0]) * (p1[2] - p3[2]);
    float l1  = ((p2[2] - p3[2]) * (pos[0] - p3[0]) + (p3[0] - p2[0]) * (pos[1] - p3[2])) / det;
    float l2  = ((p3[2] - p1[2]) * (pos[0] - p3[0]) + (p1[0] - p3[0]) * (pos[1] - p3[2])) / det;
    float l3  = 1.0f - l1 - l2;
    return l1 * p1[1] + l2 * p2[1] + l3 * p3[1];
}

enum color_pt {
    COLOR_PT_NONE = 0,
    COLOR_PT_ALPHA,
    COLOR_PT_ALL,
};

struct queued_animation {
    int             animation;
    bool            repeat;
    unsigned long   delay;
    void            (*end)(struct scene *s, void *end_priv);
    void            *end_priv;
    float           speed;
};

struct entity3d {
    struct model3dtx *txmodel;
    struct matrix4f  *mx;
    struct ref       ref;
    struct list      entry;     /* link to txmodel->entities */
    unsigned int     visible;
    int              animation;
    double           ani_time;
    darray(struct queued_animation, aniq);
    /* these both have model->nr_joints elements */
    struct joint     *joints;
    mat4x4           *joint_transforms;

    struct phys_body *phys_body;
    GLfloat color[4];
    enum color_pt color_pt;
    GLfloat dx, dy, dz;
    GLfloat rx, ry, rz;
    GLfloat scale;
    GLfloat _dx, _dy, _dz;
    GLfloat _rx, _ry, _rz;
    GLfloat _scale;
    int     light_idx;
    bool    skip_culling;
    bool    ani_cleared;
    /* 2 byte hole */
    float   aabb[6];
    float   light_off[3];
    int (*update)(struct entity3d *e, void *data);
    int (*contact)(struct entity3d *e1, struct entity3d *e2);
    void (*destroy)(struct entity3d *e);
    void *priv;
};

void model3dtx_add_entity(struct model3dtx *txm, struct entity3d *e);
void models_render(struct mq *mq, struct shader_prog *shader_override, struct light *light,
                   struct camera *camera, struct matrix4f *proj_mx, struct entity3d *focus,
                   int width, int height, int cascade, unsigned long *count);

static inline const char *entity_name(struct entity3d *e)
{
    return e ? txmodel_name(e->txmodel) : "<none>";
}

static inline bool entity_animated(struct entity3d *e)
{
    return e->txmodel->model->anis.da.nr_el;
}

struct entity3d *entity3d_new(struct model3dtx *txm);
void entity3d_reset(struct entity3d *e);
float entity3d_aabb_X(struct entity3d *e);
float entity3d_aabb_Y(struct entity3d *e);
float entity3d_aabb_Z(struct entity3d *e);
void entity3d_aabb_min(struct entity3d *e, vec3 min);
void entity3d_aabb_max(struct entity3d *e, vec3 max);
void entity3d_aabb_center(struct entity3d *e, vec3 center);
void entity3d_update(struct entity3d *e, void *data);
void entity3d_put(struct entity3d *e);
void entity3d_move(struct entity3d *e, float dx, float dy, float dz);
void entity3d_position(struct entity3d *e, float x, float y, float z);
void entity3d_add_physics(struct entity3d *e, struct phys *phys, double mass, int class, int type, double geom_off, double geom_radius, double geom_length);
void create_entities(struct model3dtx *txmodel);

struct instantiator;
struct entity3d *instantiate_entity(struct model3dtx *txm, struct instantiator *instor,
                                    bool randomize_yrot, float randomize_scale, struct scene *scene);

void debug_draw_line(struct scene *scene, vec3 a, vec3 b, mat4x4 *rot);
void debug_draw_clearout(struct scene *scene);

#endif /* __CLAP_MODEL_H__ */
