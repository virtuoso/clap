/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MODEL_H__
#define __CLAP_MODEL_H__

#include "anictl.h"
#include "common.h"
#include "object.h"
#include "librarian.h"
#include "physics.h"
#include "matrix.h"
#include "mesh.h"
#include "render.h"
#include "shader.h"

struct scene;
struct light;
struct camera;
struct shader_prog;

#define LOD_MAX 4
typedef struct model3d {
    char                *name;
    struct ref          ref;
    struct shader_prog  *prog;
    bool                cull_face;
    bool                alpha_blend;
    bool                depth_testing;
    unsigned int        draw_type;
    unsigned int        nr_joints;
    unsigned int        root_joint;
    unsigned int        nr_lods;
    int                 cur_lod;
    float               aabb[6];
    darray(struct animation, anis);
    mat4x4              root_pose;
    vertex_array_t      vao;
    buffer_t            vertex;
    buffer_t            index[LOD_MAX];
    buffer_t            tex;
    buffer_t            norm;
    buffer_t            tangent;
    buffer_t            vjoints;
    buffer_t            weights;
    unsigned int        nr_vertices;
    unsigned int        nr_faces[LOD_MAX];
    struct model_joint  *joints;
    /* Collision mesh, if needed */
    float               *collision_vx;
    size_t              collision_vxsz;
    unsigned short      *collision_idx;
    size_t              collision_idxsz;
} model3d;

DEFINE_REFCLASS_INIT_OPTIONS(model3d,
    const char          *name;
    struct shader_prog  *prog;
    struct mesh         *mesh;
    float               *vx;
    float               *tx;
    float               *norm;
    unsigned short      *idx;
    size_t              vxsz;
    size_t              idxsz;
    size_t              txsz;
    size_t              normsz;
);
DECLARE_REFCLASS(model3d);

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
    model3d         *model;
    struct channel  *channels;
    unsigned int    nr_channels;
    unsigned int    cur_channel;
    float           time_end;
};

/*
 * Delete an animation from the @model's animation array and free all its
 * contents
 */
void animation_delete(struct animation *an);
void animation_add_channel(struct animation *an, size_t frames, float *time, float *data,
                           size_t data_stride, unsigned int target, unsigned int path);
struct animation *animation_new(model3d *model, const char *name, unsigned int nr_channels);
int animation_by_name(model3d *m, const char *name);

typedef struct model3dtx {
    model3d        *model;
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
} model3dtx;

DEFINE_REFCLASS_INIT_OPTIONS(model3dtx,
    model3d     *model;
    texture_t   *tex;
    const char  *texture_file_name;
    const char  *normal_file_name;
    const char  *emission_file_name;
    void        *texture_buffer;
    void        *normal_buffer;
    void        *emission_buffer;
    size_t      texture_size;
    size_t      normal_size;
    size_t      emission_size;
    int         texture_width;
    int         texture_height;
    int         normal_width;
    int         normal_height;
    int         emission_width;
    int         emission_height;
    bool        texture_has_alpha;
    bool        buffers_png;
);
DECLARE_REFCLASS(model3dtx);

int model3d_add_skinning(model3d *m, unsigned char *joints, size_t jointssz,
                         float *weights, size_t weightssz, size_t nr_joints, mat4x4 *invmxs);
cres(int) model3d_set_name(model3d *m, const char *fmt, ...);
float model3d_aabb_X(model3d *m);
float model3d_aabb_Y(model3d *m);
float model3d_aabb_Z(model3d *m);
model3dtx *model3dtx_new2(model3d *model, const char *tex, const char *norm);
model3dtx *model3dtx_new_from_png_buffers(model3d *model, void *tex, size_t texsz, void *norm, size_t normsz,
                                          void *em, size_t emsz);
model3dtx *model3dtx_new_texture(model3d *model, texture_t *tex);
void model3dtx_set_texture(model3dtx *txm, enum shader_vars var, texture_t *tex);
void model3dtx_prepare(model3dtx *m, struct shader_prog *p);
void model3dtx_done(model3dtx *m, struct shader_prog *p);

static inline const char *txmodel_name(model3dtx *txm)
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
void mq_for_each(struct mq *mq, void (*cb)(entity3d *, void *), void *data);
model3dtx *mq_model_first(struct mq *mq);
model3dtx *mq_model_last(struct mq *mq);
void mq_add_model(struct mq *mq, model3dtx *txmodel);
void mq_add_model_tail(struct mq *mq, model3dtx *txmodel);
model3dtx *mq_nonempty_txm_next(struct mq *mq, model3dtx *txm, bool fwd);


struct queued_animation {
    int             animation;
    bool            repeat;
    unsigned long   delay;
    void            (*end)(struct scene *s, void *end_priv);
    void            *end_priv;
    float           speed;
};

typedef struct entity3d {
    model3dtx        *txmodel;
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
    struct anictl    anictl;
    float   color[4];
    int     color_pt;
    vec3    pos;
    float   rx, ry, rz;
    float   scale;
    int     light_idx;
    int     updated;
    bool    skip_culling;
    bool    ani_cleared;
    /* 2 byte hole */
    float   aabb[6];
    float   light_off[3];
    int (*update)(entity3d *e, void *data);
    int (*contact)(entity3d *e1, entity3d *e2);
    void (*destroy)(entity3d *e);
    void *priv;
} entity3d;

DEFINE_REFCLASS_INIT_OPTIONS(entity3d);
DECLARE_REFCLASS(entity3d);

void model3dtx_add_entity(model3dtx *txm, entity3d *e);
void models_render(renderer_t *r, struct mq *mq, struct shader_prog *shader_override,
                   struct light *light, struct camera *camera, struct matrix4f *proj_mx,
                   entity3d *focus, int width, int height, int cascade,
                   unsigned long *count);

static inline const char *entity_name(entity3d *e)
{
    return e ? txmodel_name(e->txmodel) : "<none>";
}

static inline bool entity_animated(entity3d *e)
{
    return e->txmodel->model->anis.da.nr_el;
}

entity3d *entity3d_new(model3dtx *txm);

/*
 * Set up entity's model matrix from entity's coordinates and euler rotations.
 * Not necessary if the entity::update points to default_update(), which is the
 * default, or the new update callback calls the original callback.
 */
void entity3d_reset(entity3d *e);
float entity3d_aabb_X(entity3d *e);
float entity3d_aabb_Y(entity3d *e);
float entity3d_aabb_Z(entity3d *e);
void entity3d_aabb_min(entity3d *e, vec3 min);
void entity3d_aabb_max(entity3d *e, vec3 max);
void entity3d_aabb_center(entity3d *e, vec3 center);
void entity3d_update(entity3d *e, void *data);
void entity3d_put(entity3d *e);

/* Set entity's scale */
void entity3d_scale(entity3d *e, float scale);

/* Move entity by an offset */
void entity3d_move(entity3d *e, vec3 off);

/* Set entity's absolute position */
void entity3d_position(entity3d *e, vec3 pos);

/* Set entity's visibility */
void entity3d_visible(entity3d *e, unsigned int visible);

/* Set entity's rotation aronud X axis */
void entity3d_rotate_X(entity3d *e, float rx);

/* Set entity's rotation aronud Y axis */
void entity3d_rotate_Y(entity3d *e, float ry);

/* Set entity's rotation aronud Z axis */
void entity3d_rotate_Z(entity3d *e, float rz);
void entity3d_add_physics(entity3d *e, struct phys *phys, double mass, int class, int type, double geom_off, double geom_radius, double geom_length);

void animation_start(entity3d *e, struct scene *scene, int ani);
void animation_push_by_name(entity3d *e, struct scene *s, const char *name,
                            bool clear, bool repeat);
void animation_set_end_callback(entity3d *e, void (*end)(struct scene *, void *), void *priv);
void animation_set_speed(entity3d *e, float speed);


struct instantiator;
entity3d *instantiate_entity(model3dtx *txm, struct instantiator *instor,
                                    bool randomize_yrot, float randomize_scale, struct scene *scene);

void debug_draw_line(struct scene *scene, vec3 a, vec3 b, mat4x4 *rot);
void debug_draw_clearout(struct scene *scene);

#endif /* __CLAP_MODEL_H__ */
