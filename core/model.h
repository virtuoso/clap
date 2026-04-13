/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_MODEL_H__
#define __CLAP_MODEL_H__

#include "common.h"
#include "error.h"
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

/*
 * Semantic joint identifiers that callers can resolve to the actual
 * per-model joint index via model3d_get_joint(). Gives camera and
 * attachment code a stable name for "the head joint" etc. without
 * hardcoding skeleton-specific indices.
 */
typedef enum joint_type {
    JOINT_NONE,
    JOINT_HEAD,
    JOINT_FOOT_LEFT,
    JOINT_FOOT_RIGHT,
    JOINT_HAND_LEFT,
    JOINT_HAND_RIGHT,
    JOINT_TYPE_MAX,
} joint_type;

cres_ret(joint_type);

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
    vec3                aabb[2];
    darray(struct animation, anis);
    mat4x4              root_pose;
    vertex_array_t      vao;
    buffer_t            attr[ATTR_MAX];
    buffer_t            index[LOD_MAX];
    sfx_container       sfxc;
    unsigned int        nr_vertices;
    unsigned int        nr_faces[LOD_MAX];
    float               lod_errors[LOD_MAX];
    bool                skip_aabb;
    struct model_joint  *joints;
    /* Index into joints[] keyed by joint_type; -1 when unmapped. */
    int                 joint_types[JOINT_TYPE_MAX];
    /* Collision mesh, if needed */
    float               *collision_vx;
    size_t              collision_vxsz;
    unsigned short      *collision_idx;
    size_t              collision_idxsz;
} model3d;

/**
 * struct model3d_init_options - model3d constructor parameters
 * @name:       model name
 * @prog:       shader program that will draw this model
 * @mesh:       source of combined vertex attributes and mesh indices
 * @fix_origin: recalculate vertex coordinates
 * @skip_aabb:  don't calculate AABB for this model
 *
 * Constructor parameters for model3d, passed into model3d_make().
 * * @prog and @mesh and non-optional;
 * * @mesh must contain vertices (be not empty);
 * * @fix_origin places the origin in the center of the bottom quad of the model's
 * AABB and recalculates all vertices relative to that;
 * * @skip_aabb is for models that don't participate in frustum culling, origin
 * fixing or anything else that requires a valid AABB, like the UI quads or glyphs.
 */
DEFINE_REFCLASS_INIT_OPTIONS(model3d,
    const char          *name;
    struct shader_prog  *prog;
    struct mesh         *mesh;
    bool                fix_origin;
    bool                skip_aabb;
);
DECLARE_REFCLASS(model3d);
DECLARE_CLEANUP(model3d);

struct model_joint {
    darray(int, children);
    char        *name;
    mat4x4      invmx;
    mat4x4      bind;
    int         id;
};

/**
 * enum chan_path - animation channel "path"
 * @PATH_TRANSLATION:   moving a joint's position
 * @PATH_ROTATION:      rotate a joint
 * @PATH_SCALE:         scale associated vertices
 * @PATH_NONE:          sentinel
 *
 * There are self-explanatory and correspond to GLTF's representation
 * of skeletal animations. Each channel represents changes in one of
 * these paths over time. See &struct channel.
 */
enum chan_path {
    PATH_TRANSLATION = 0,
    PATH_ROTATION,
    PATH_SCALE,
    PATH_NONE,
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
    /* Repeat texture */
    float   uv_factor;
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
    /*
     * Procedural normal perturbation mode:
     * - NOISE_NORMALS_NONE: off,
     * - NOISE_NORMALS_GPU:  on-the-fly fBm gradient (expensive, non-periodic),
     * - NOISE_NORMALS_3D:   baked periodic fBm gradient texture (cheap, tileable)
     */
    int     use_noise_normals;
    /* Tilt amplitude for procedural normals */
    float   noise_normals_amp;
    /* Sampling frequency for procedural normals */
    float   noise_normals_scale;
    /* Procedurally modulate the emission map with 3D noise */
    bool    use_noise_emission;
    /* Apply volumetric 3D fog to the lit result */
    bool    use_3d_fog;
    float   fog_3d_amp;
    float   fog_3d_scale;
    /* In case of noise emission*/
    vec3    noise_emission_color;
} material;

typedef struct model3dtx {
    model3d        *model;
    texture_t      _texture;
    texture_t      _normals;
    texture_t      _emission;
    texture_t      *textures[UNIFORM_NR_TEX];
    material       mat;
    struct ref     ref;
    struct list    entry;              /* link to scene/ui->txmodels */
    struct list    entities;           /* links entity3d->entry */
} model3dtx;

DEFINE_REFCLASS_INIT_OPTIONS(model3dtx,
    model3d         *model;
    texture_t       *tex;
    const char      *texture_file_name;
    const char      *normal_file_name;
    const char      *emission_file_name;
    void            *texture_buffer;
    void            *normal_buffer;
    void            *emission_buffer;
    size_t          texture_size;
    size_t          normal_size;
    size_t          emission_size;
    int             texture_width;
    int             texture_height;
    int             normal_width;
    int             normal_height;
    int             emission_width;
    int             emission_height;
    float           metallic;
    float           roughness;
    texture_format  texture_color_format;
    bool            buffers_png;
);
DECLARE_REFCLASS(model3dtx);
DECLARE_CLEANUP(model3dtx);

int model3d_add_skinning(model3d *m, size_t nr_joints, mat4x4 *invmxs);
cres(joint_type) model3d_get_joint(model3d *m, joint_type type);
cresp(const_char) joint_type_name(joint_type type);
cres(joint_type) joint_by_type_name(const char *type);
cres(joint_type) model3d_joint_by_type(model3d *m, const char *type);
cres(int) model3d_set_name(model3d *m, const char *fmt, ...);
float model3d_aabb_X(model3d *m);
float model3d_aabb_Y(model3d *m);
float model3d_aabb_Z(model3d *m);
void model3dtx_set_texture(model3dtx *txm, enum shader_vars var, texture_t *tex);

cresp_ret(texture_t);

cresp(texture_t) model3dtx_texture(model3dtx *txm, enum shader_vars var);
cresp(texture_t) model3dtx_loaded_texture(model3dtx *txm, enum shader_vars var);

static inline const char *txmodel_name(model3dtx *txm)
{
    return txm->model->name;
}

/**
 * enum entity3d_flags - entity flags
 * @ENTITY3D_VISIBLE:       entity is visible
 * @ENTITY3D_IS_CHARACTER:  entity is a character
 * @ENTITY3D_IS_UI:         entity is a UI element
 * @ENTITY3D_IS_PARTICLE:   entity is a particle system
 * @ENTITY3D_HAS_PHYSICS:   entity has a phys_body
 * @ENTITY3D_PHYS_IS_BODY:  entity's phys_body is a dynamic body (not just a geom)
 * @ENTITY3D_HAS_AABB:      entity has an AABB
 * @ENTITY3D_OCCLUDER:      entity is rendered in shadow passes
 * @ENTITY3D_LIGHT_SOURCE:  entity is a light source
 * @ENTITY3D_RENDERABLE:    entity is renderable
 * @ENTITY3D_EDITOR_ONLY:   entity is editor related
 * @ENTITY3D_IS_GAME_ITEM:  entity is a game item
 * @ENTITY3D_HAS_ARMATURE:  entity has an armature (skeleton)
 * @ENTITY3D_IS_ANIMATED:   entity has animations
 * @ENTITY3D_SKIP_CULLING:  entity bypasses frustum culling
 * @ENTITY3D_SKIP_UPDATE:   entity bypasses per-frame update
 * @ENTITY3D_ALIVE:         entity is alive and functional
 * @ENTITY3D_ANY:           matches any entity
 */
typedef enum entity3d_flags {
    ENTITY3D_VISIBLE        = 1u << 0,
    ENTITY3D_IS_CHARACTER   = 1u << 1,
    ENTITY3D_IS_UI          = 1u << 2,
    ENTITY3D_IS_PARTICLE    = 1u << 3,
    ENTITY3D_HAS_PHYSICS    = 1u << 4,
    ENTITY3D_PHYS_IS_BODY   = 1u << 5,
    ENTITY3D_HAS_AABB       = 1u << 6,
    ENTITY3D_OCCLUDER       = 1u << 7,
    ENTITY3D_LIGHT_SOURCE   = 1u << 8,
    ENTITY3D_RENDERABLE     = 1u << 9,
    ENTITY3D_EDITOR_ONLY    = 1u << 10,
    ENTITY3D_IS_GAME_ITEM   = 1u << 11,
    ENTITY3D_HAS_ARMATURE   = 1u << 12,
    ENTITY3D_IS_ANIMATED    = 1u << 13,
    ENTITY3D_SKIP_CULLING   = 1u << 14,
    ENTITY3D_SKIP_UPDATE    = 1u << 15,
    ENTITY3D_ALIVE          = 1u << 31,
    ENTITY3D_ANY            = ~0u,
} entity3d_flags;

/**
 * Mutually exclusive entity type flags: an entity can be at most one of
 * character, UI element, or particle system, because they all share the
 * ->priv pointer for their back-reference.
 */
#define ENTITY3D_TYPE_MASK  (ENTITY3D_IS_CHARACTER | ENTITY3D_IS_UI | ENTITY3D_IS_PARTICLE)

typedef void (*entity3d_callback)(entity3d *, void *);

typedef struct entity3d_query {
    entity3d_flags          all_of;
    entity3d_flags          any_of;
    entity3d_flags          none_of;
} entity3d_query;

#define E3DQ(args...)       (entity3d_query) { args }
#define E3DQS(args...)      (entity3d_query[]) { args, {} }

bool entity3d_is(entity3d *e, entity3d_query *query);

struct mq {
    struct list     txmodels;
    void            *priv;
    unsigned int    nr_characters;
};

void mq_init(struct mq *mq, void *priv);
void mq_release(struct mq *mq);
void mq_update(struct mq *mq);
void mq_for_each_matching(struct mq *mq, entity3d_flags flags, entity3d_callback cb, void *data);
cres(size_t) mq_for_each_query(struct mq *mq, entity3d_query *query, entity3d_callback cb, void *data);
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

struct joint {
    vec3    translation;
    quat    rotation;
    vec3    scale;
    mat4x4  global;
    vec4    pos;
    int     off[PATH_NONE];
};

typedef struct entity3d {
    model3dtx           *txmodel;
    mat4x4              mx;
    mat4x4              inverse_mx;
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

    /*
     * Optional parent attachment comes in 2 flavors: attachment to a
     * parent's joint (->parent_joint) or the parent's world transform
     * (->parent_joint==JOINT_TYPE_MAX).
     * This is handled by parent_transform_apply(). For jointless
     * attachment, we keep track of parent's updates via seq and parent_seq
     * (if they match, no need to update the child).
     * For joint attachments, parent_transform_apply() has to rebuild
     * this entity's world matrix every frame from the parent entity's
     * joint_transforms[parent_joint] (+ the joint's bind pose) so the
     * child rides the parent's animated skeleton. parent_joint is the
     * resolved joint index into parent->joint_transforms, not a
     * joint_type enum — callers look it up via model3d_get_joint() at
     * attachment time.
     */
    entity3d            *parent;
    int                 parent_joint;
    uint16_t            seq;
    uint16_t            parent_seq;
    struct phys_body    *phys_body;
    particle_system     *particles;
    float               bloom_intensity;
    float               bloom_threshold;
    float               color[4];
    int                 color_pt;
    transform_t         xform;
    float               scale;
    struct light        *light;
    int                 light_idx;
    int                 cur_lod;
    int                 force_lod;
    bool                skip_culling;
    bool                ani_cleared;
    bool                outline_exclude;
    /* 1 byte hole */
    vec3                aabb[2];
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

cerr entity3d_set(entity3d *e, entity3d_flags flags, void *priv);
void entity3d_clear(entity3d *e, entity3d_flags flags);

cresp(entity3d) mq_find_entity(struct mq *mq, const char *name);
cresp(entity3d) mq_find_first(struct mq *mq, entity3d_query *query);

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
    unsigned int        nr_cascades;
    float               near_plane;
    float               far_plane;
} models_render_options;

/**
 * define models_render - wrapper for _models_render()
 * @_r:     the main object of the rendering backend
 * @_mq:    model queue to be rendered
 * @args:   a comma separated list of &struct models_render_options' field
 *          initializers
 *
 * This is a syntax sugar for calling _models_render() with an arbitrary set
 * of optional arguments stored in transient &struct models_render_options.
 * Called like this: ``models_render(r, &mq, .light = &light, .camera = &camera);``.
 * For more information, see _models_render().
 */
#define models_render(_r, _mq, args...) \
    _models_render((_r), (_mq), &(models_render_options){ args })

/**
 * _models_render() - render a model queue
 * @r:      the main object of the rendering backend
 * @mq:     model queue to be rendered
 * @opts:   optional arguments
 *
 * This is the one that renders everything: character glyphs onto UI text FBO,
 * shadow cascades, model pass, postprocessing passes, etc.
 *
 * All options in @opts are optional. See &struct models_render_options for details.
 *
 * Don't call this one directly, instead use ``models_render()`` wrapper.
 * Context: inside the frame function path
 * Return: [must check] errors from the rendering backend or CERR_OK
 */
cerr_check _models_render(renderer_t *r, struct mq *mq, const models_render_options *opts);

/**
 * entity_name() - get entity3d's name
 * @e:  entity3d object
 *
 * By default, an entity3d has its underlying model3d's name, but if explicitly
 * configured, can have its own distinct name.
 * Context: anywhere
 * Return: entity's name string or "<none>" if neither entity3d nor its model3d
 * have names.
 */
static inline const char *entity_name(entity3d *e)
{
    if (e && e->name)
        return e->name;

    return e ? txmodel_name(e->txmodel) : "<none>";
}

/**
 * entity_animated() - does entity3d have skeletal animations
 * @e:  entity3d object
 *
 * Entities themselves don't *have* animations (they have animation queues),
 * but their underlying model3d's do.
 * Context: anywhere
 * Return: true for animated entities, false otherwise
 */
static inline bool entity_animated(entity3d *e)
{
    return e->txmodel->model->anis.da.nr_el;
}

/**
 * entity3d_reset() - explicitly reset entities TRS matrix
 * @e:  entity3d object
 *
 * Set up entity's model matrix from entity's coordinates and euler rotations.
 * Not necessary if the entity::update points to default_update(), which is the
 * default, or the new update callback calls the original callback.
 * Context: anywhere
 */
void entity3d_reset(entity3d *e);

/**
 * entity3d_aabb_X() - get entity3d's AABB size along world's X axis
 * @e:  entity3d object
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 * Context: anywhere
 * Return: entity3d's AABB X size (width)
 */
float entity3d_aabb_X(entity3d *e);

/**
 * entity3d_aabb_Y() - get entity3d's AABB size along world's Y axis
 * @e:  entity3d object
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 * Context: anywhere
 * Return: entity3d's AABB Y size (height)
 */
float entity3d_aabb_Y(entity3d *e);

/**
 * entity3d_aabb_Z() - get entity3d's AABB size along world's Z axis
 * @e:  entity3d object
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 * Context: anywhere
 * Return: entity3d's AABB Z size (depth)
 */
float entity3d_aabb_Z(entity3d *e);

/**
 * entity3d_aabb_min() - get entity3d's AABB's min corner
 * @e:      entity3d object
 * @min:    receiver of the min vertex coordinates
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 *
 * Return entity3d's AABB's corner with smallest coordinates in world space in
 * @min.
 * Context: anywhere
 */
void entity3d_aabb_min(entity3d *e, vec3 min);

/**
 * entity3d_aabb_max() - get entity3d's AABB's max corner
 * @e:      entity3d object
 * @max:    receiver of the max vertex coordinates
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 *
 * Returns entity3d's AABB's corner with biggest coordinates in world space in
 * @max.
 * Context: anywhere
 */
void entity3d_aabb_max(entity3d *e, vec3 max);

/**
 * entity3d_aabb_center() - get entity3d's AABB's center
 * @e:      entity3d object
 * @center: receiver of the AABB center coordinates
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 *
 * Returns entity3d's AABB's center in @center.
 * Context: anywhere
 */
void entity3d_aabb_center(entity3d *e, vec3 center);

/**
 * entity3d_aabb_avg_edge() - get entity3d's AABB's average edge
 * @e:  entity3d object
 *
 * AABB (axis-aligned bounding box) is a volume that envelopes a 3D mesh.
 * entity3d's AABB is different from model3d's AABB in that it covers the
 * entity in its current position, rotation and scale (not taking into account
 * any joint transforms at the moment).
 *
 * Average edge is calculated as a cubic root of the AABB's volume. It's used
 * as a scalar measure of the entity3d's size.
 * Context: anywhere
 * Return: entity3d's AABB's average edge
 */
float entity3d_aabb_avg_edge(entity3d *e);

/**
 * entity3d_update() - run entity3d's update method
 * @e:      entity3d object
 * @data:   data to be passed to the update method
 *
 * Apply any transforms that happened to the entity3d in the course of the
 * current frame. By default, it's default_update(), which recalculates the
 * TRS matrix. Users can override it and/or chain the update functions by
 * saving the original update method's address (e.g. &struct character).
 * Context: anywhere, but most useful inside the frame function
 */
void entity3d_update(entity3d *e, void *data);

/**
 * entity3d_delete() - delete an entity3d
 * @e:  entity3d object
 *
 * This marks the entity3d as ENTITY3D_DEAD and drops its current reference,
 * which may or may not destroy the object, depending on whether anything else
 * holds a reference to it. All users should respect the ENTITY3D_DEAD flag and
 * exclude such entity from their activities.
 * Context: anywhere, being mindful of entity3d's flags
 */
void entity3d_delete(entity3d *e);

/**
 * entity3d_matches() - check if entity3d's flags match any of the given flags
 * @e:      entity3d object
 * @flags:  entity flags
 *
 * Context: anywhere
 * Return: true if any of the @flags is set in the entity3d, false otherwise
 */
bool entity3d_matches(entity3d *e, entity3d_flags flags);

/**
 * entity3d_particle_system() - get particle_system from entity3d
 * @e:  entity3d object
 *
 * Context: anywhere
 * Return: particle_system pointer or CERR_INVALID_OPERATION
 */
cresp(particle_system) entity3d_particle_system(entity3d *e);

/**
 * entity3d_set_lod() - Set entity's LOD (level-of-detail)
 * @e:      entity3d object
 * @lod:    LOD level [0, LOD_MAX]
 * @force:  set force_lod property
 *
 * Set entity3d's LOD. If @force is true, and @lod >= 0, set entity3d's
 * force_lod property that bypasses distance-based LOD inference and forces
 * the given LOD at all times; if @force is true and @lod is negative, clear
 * the entity3d's force_lod property, thus reenabling distance-based LOD
 * calculations in the render path.
 * Context: anywhere
 */
void entity3d_set_lod(entity3d *e, int lod, bool force);

/**
 * entity3d_color() - set entity's color override mode and color
 * @e:          entity3d object
 * @color_pt:   bitmask of "color passthrough" COLOR_PT_* modes
 * @color:      override color
 *
 * Depending on @color_pt setting, do one of the following. Only one alpha
 * and one RGB mode are allowed:
 * * COLOR_PT_NONE:             use model3dtx's diffuse texture (default)
 * * COLOR_PT_SET_RGB:          use @color unconditionally
 * * COLOR_PT_REPLACE_RGB:      replace non-black pixels with @color
 * * COLOR_PT_SET_ALPHA:        override the alpha channel with @color[3]
 * * COLOR_PT_REPLACE_ALPHA:    replace non-zero alpha with @color[3]
 * * COLOR_PT_BLEND_ALPHA:      multiply alpha channel by @color[3]
 * * COLOR_PT_ALL:              everything with @color
 * Context: anywhere
 */
void entity3d_color(entity3d *e, int color_pt, vec4 color);

/**
 * entity3d_scale() - set entity's scale
 * @e:      entity3d object
 * @scale:  scale factor
 *
 * Set entity3d's scale. This will change the rendered entity3d's
 * appearance if called from anywhere, but not the scale of the
 * collision mesh, unless called before entity3d_add_physics().
 * Context: entity3d setup time if collision mesh is in play,
 * otherwise anywhere.
 */
void entity3d_scale(entity3d *e, float scale);

/**
 * entity3d_move() - move entity by an offset
 * @e:      entity3d object
 * @off:    translation vector
 *
 * Change the entity3d's position by @off.
 * Context: anywhere; the change takes effect in entity3d_update() /
 * entity3d_reset().
 */
void entity3d_move(entity3d *e, vec3 off);

/**
 * entity3d_position() - set entity's absolute position
 * @e:      entity3d object
 * @pos:    position coordinates
 *
 * Change the entity3d's position to @pos.
 * Context: anywhere; the change takes effect in entity3d_update() /
 * entity3d_reset().
 */
void entity3d_position(entity3d *e, vec3 pos);

/**
 * entity3d_visible() - set entity's visibility
 * @e:          entity3d object
 * @visible:    visibility boolean
 *
 * Set entity3d's visibility to @visible. Takes effect in the models_render(),
 * but may affect other entity processing code.
 * Context: anywhere
 */
void entity3d_visible(entity3d *e, unsigned int visible);

/**
 * entity3d_rotate - set entity's rotation by Euler's angles
 * @e:  entity object
 * @rx: rotation around X axis (pitch)
 * @ry: rotation around Y axis (yaw)
 * @rz: rotation around Z axiz (roll)
 *
 * Set entity3d's rotation around world XYZ axes.
 * Context: anywhere
 */
void entity3d_rotate(entity3d *e, float rx, float ry, float rz);

/**
 * entity3d_add_physics() - add physics to an entity
 * @e:              entity3d object
 * @phys:           clap's physics handle
 * @mass:           physics body's mass
 * @class:          physics body's geometry class (see &enum geom_class)
 * @type:           PHYS_GEOM or PHYS_BODY (see &enum phys_type)
 * @geom_off:       vertical offset from entity3d's position (capsules and spheres)
 * @geom_radius:    radius (capsules and spheres)
 * @geom_length:    capsule length
 *
 * Create entity3d::phys_body with the given parameters. @mass is only relevant
 * for PHYS_BODY @type. @class is typically either GEOM_TRIMESH, GEOM_CAPSULE or
 * GEOM_SPHERE. @geom_off, @geom_radius and @geom_length are only relevant for
 * GEOM_CAPSULE and GEOM_SPHERE @class.
 * PHYS_BODY participates in dynamics simulation, PHYS_GEOM is a collision mesh.
 */
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
