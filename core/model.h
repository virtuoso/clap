/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MODEL_H__
#define __CLAP_MODEL_H__

#include "common.h"
#include "object.h"
#include "librarian.h"
#include "particle.h"
#include "physics.h"
#include "mesh.h"
#include "render.h"
#include "shader.h"
#include "sound.h"
#include "ssao.h"
#include "transform.h"

struct scene;
struct light;
struct camera;
struct shader_prog;
typedef struct render_options render_options;

#define LOD_MAX 4
typedef struct model3d {
    char                *name;
    struct ref          ref;
    struct shader_prog  *prog;
    bool                cull_face;
    bool                alpha_blend;
    bool                depth_testing;
    bool                skip_shadow;
    unsigned int        draw_type;
    unsigned int        nr_joints;
    unsigned int        root_joint;
    unsigned int        nr_lods;
    unsigned int        lod_min;
    unsigned int        lod_max;
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
    sfx_container       sfxc;
    unsigned int        nr_vertices;
    unsigned int        nr_faces[LOD_MAX];
    float               lod_errors[LOD_MAX];
    bool                skip_aabb;
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
    bool                fix_origin;
    bool                skip_aabb;
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

struct queued_animation;
typedef void (*frame_fn)(struct queued_animation *qa, entity3d *e, struct scene *s, double time);

struct animation {
    char            *name;
    model3d         *model;
    struct channel  *channels;
    unsigned int    nr_channels;
    unsigned int    cur_channel;
    unsigned int    nr_segments;
    float           time_end;
    frame_fn        frame_sfx;
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

typedef struct material {
    /*
     * Roughness or roughness floor, in case of procedurally generated
     * roughness
     */
    float   roughness;
    /* Procedural roughness ceiling */
    float   roughness_ceil;
    /* Procedural roughness: FBM amplitude scale per octave */
    float   roughness_amp;
    /* Procedural roughness: number of FBM octaves */
    int     roughness_oct;
    /* Procedural roughness: vertex coordinate scale */
    float   roughness_scale;
    /*
     * Metallic or metallic floor, in case of procedurally generated
     * metallic
     */
    float   metallic;
    /* Procedural metallic ceiling */
    float   metallic_ceil;
    /* Procedural metallic: FBM amplitude scale per octave */
    float   metallic_amp;
    /* Procedural metallic: number of FBM octaves */
    int     metallic_oct;
    /* Procedural metallic: vertex coordinate scale */
    float   metallic_scale;
    /*
     * Procedural metallic noise mode:
     * - MAT_METALLIC_INDEPENDENT: independent,
     * - MAT_METALLIC_ROUGHNESS: == roughness,
     * - MAT_METALLIC_ONE_MINUS_ROUGHNESS: == 1 - roughness
     */
    int     metallic_mode;
    /* Procedural metallic: use the same scale as roughness */
    bool    shared_scale;
} material;

typedef struct model3dtx {
    model3d        *model;
    texture_t      _texture;
    texture_t      _normals;
    texture_t      _emission;
    texture_t      _sobel;
    texture_t      _shadow;
    texture_t      _lut;
    texture_t      *texture;
    texture_t      *normals;
    texture_t      *emission;
    texture_t      *sobel;
    texture_t      *shadow;
    texture_t      *lut;
    material       mat;
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
    float       metallic;
    float       roughness;
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
void model3dtx_set_texture(model3dtx *txm, enum shader_vars var, texture_t *tex);

static inline const char *txmodel_name(model3dtx *txm)
{
    return txm->model->name;
}

typedef enum entity3d_flags {
    ENTITY3D_ALIVE  = 1u << 31,
    ENTITY3D_DEAD   = ~ENTITY3D_ALIVE,
    ENTITY3D_ANY    = ~0u,
} entity3d_flags;

struct mq {
    struct list     txmodels;
    void            *priv;
    unsigned int    nr_characters;
};

void mq_init(struct mq *mq, void *priv);
void mq_release(struct mq *mq);
void mq_update(struct mq *mq);
void mq_for_each_matching(struct mq *mq, entity3d_flags flags, void (*cb)(entity3d *, void *), void *data);
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
    unsigned int    sfx_state;
    frame_fn        frame_cb;
};

typedef struct entity3d {
    model3dtx           *txmodel;
    mat4x4              mx;
    struct ref          ref;
    struct list         entry;     /* link to txmodel->entities */
    entity3d_flags      flags;
    unsigned int        visible;
    int                 animation;
    double              ani_time;
    char                *name;
    darray(struct queued_animation, aniq);
    /* these both have model->nr_joints elements */
    struct joint        *joints;
    mat4x4              *joint_transforms;

    struct phys_body    *phys_body;
    particle_system     *particles;
    float               bloom_intensity;
    float               bloom_threshold;
    float               color[4];
    int                 color_pt;
    transform_t         xform;
    float               scale;
    int                 light_idx;
    int                 cur_lod;
    int                 force_lod;
    int                 updated;
    bool                skip_culling;
    bool                ani_cleared;
    bool                outline_exclude;
    /* 1 byte hole */
    float               aabb[6];
    vec3                aabb_center;
    float               light_off[3];
    int                 (*update)(entity3d *e, void *data);
    void                (*connect)(entity3d *e, entity3d *connection, void *data);
    void                (*disconnect)(entity3d *e, entity3d *connection, void *data);
    void                *connect_priv;
    void                (*destroy)(entity3d *e);
    void                *priv;
} entity3d;

DEFINE_REFCLASS_INIT_OPTIONS(entity3d,
    model3dtx   *txmodel;
);
DECLARE_REFCLASS(entity3d);

typedef struct models_render_options {
    struct shader_prog  *shader_override;
    struct light        *light;
    struct camera       *camera;
    unsigned long       *entity_count;
    unsigned long       *txm_count;
    unsigned long       *culled_count;
    render_options      *render_options;
    ssao_state          *ssao_state;
    unsigned int        width;
    unsigned int        height;
    int                 cascade;
    float               near_plane;
    float               far_plane;
} models_render_options;

#define models_render(_r, _mq, args...) \
    _models_render((_r), (_mq), &(models_render_options){ args })
void _models_render(renderer_t *r, struct mq *mq, const models_render_options *opts);

static inline const char *entity_name(entity3d *e)
{
    if (e && e->name)
        return e->name;

    return e ? txmodel_name(e->txmodel) : "<none>";
}

static inline bool entity_animated(entity3d *e)
{
    return e->txmodel->model->anis.da.nr_el;
}

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
float entity3d_aabb_avg_edge(entity3d *e);
void entity3d_update(entity3d *e, void *data);
void entity3d_delete(entity3d *e);
bool entity3d_matches(entity3d *e, entity3d_flags flags);

/* Set entity's LOD */
void entity3d_set_lod(entity3d *e, int lod, bool force);
/* Set entity's color override mode and color */
void entity3d_color(entity3d *e, int color_pt, vec4 color);

/* Set entity's scale */
void entity3d_scale(entity3d *e, float scale);

/* Move entity by an offset */
void entity3d_move(entity3d *e, vec3 off);

/* Set entity's absolute position */
void entity3d_position(entity3d *e, vec3 pos);

/* Set entity's visibility */
void entity3d_visible(entity3d *e, unsigned int visible);

/* Set entity's rotation */
void entity3d_rotate(entity3d *e, float rx, float ry, float rz);

void entity3d_add_physics(entity3d *e, struct phys *phys, double mass, int class, int type, double geom_off, double geom_radius, double geom_length);

void animation_start(entity3d *e, struct scene *scene, int ani);
bool animation_push_by_name(entity3d *e, struct scene *s, const char *name,
                            bool clear, bool repeat);
void animation_set_end_callback(entity3d *e, void (*end)(struct scene *, void *), void *priv);
void animation_set_frame_callback(entity3d *e, frame_fn cb);
void animation_set_speed(entity3d *e, struct scene *s, float speed);


struct instantiator;
entity3d *instantiate_entity(model3dtx *txm, struct instantiator *instor,
                                    bool randomize_yrot, float randomize_scale, struct scene *scene);

#endif /* __CLAP_MODEL_H__ */
