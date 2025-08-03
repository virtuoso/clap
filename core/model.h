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
    buffer_t            attr[ATTR_MAX];
    buffer_t            index[LOD_MAX];
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

struct model_joint {
    darray(int, children);
    char        *name;
    mat4x4      invmx;
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

/**
 * struct material - material parameters
 */
typedef struct material {
    /**
     * Roughness or roughness floor, in case of procedurally generated
     * roughness
     */
    float   roughness;
    /** Procedural roughness ceiling */
    float   roughness_ceil;
    /** Procedural roughness: FBM amplitude scale per octave */
    float   roughness_amp;
    /** Procedural roughness: number of FBM octaves */
    int     roughness_oct;
    /** Procedural roughness: vertex coordinate scale */
    float   roughness_scale;
    /**
     * Metallic or metallic floor, in case of procedurally generated
     * metallic
     */
    float   metallic;
    /** Procedural metallic ceiling */
    float   metallic_ceil;
    /** Procedural metallic: FBM amplitude scale per octave */
    float   metallic_amp;
    /** Procedural metallic: number of FBM octaves */
    int     metallic_oct;
    /** Procedural metallic: vertex coordinate scale */
    float   metallic_scale;
    /**
     * Procedural metallic noise mode:
     * - MAT_METALLIC_INDEPENDENT: independent,
     * - MAT_METALLIC_ROUGHNESS: == roughness,
     * - MAT_METALLIC_ONE_MINUS_ROUGHNESS: == 1 - roughness
     */
    int     metallic_mode;
    /** Procedural metallic: use the same scale as roughness */
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

int model3d_add_skinning(model3d *m, size_t nr_joints, mat4x4 *invmxs);
cres(int) model3d_set_name(model3d *m, const char *fmt, ...);
float model3d_aabb_X(model3d *m);
float model3d_aabb_Y(model3d *m);
float model3d_aabb_Z(model3d *m);
void model3dtx_set_texture(model3dtx *txm, enum shader_vars var, texture_t *tex);

static inline const char *txmodel_name(model3dtx *txm)
{
    return txm->model->name;
}

/**
 * enum entity3d_flags - entity flags
 * @ENTITY3D_ALIVE: entity is alive and functional
 * @ENTITY3D_DEAD:  entity is about to be destroyed
 * @ENTITY3D_ANY:   matches any entity
 */
typedef enum entity3d_flags {
    ENTITY3D_ALIVE  = 1u << 31,
    /* XXX: this matches any non-ALIVE flags, which is not what we want */
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
 */
void _models_render(renderer_t *r, struct mq *mq, const models_render_options *opts);

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
 * @color_pt:   "color passthrough" mode
 * @color:      override color
 *
 * Depending on @color_pt setting, do one of the 3 things:
 * * COLOR_PT_NONE:     use model3dtx's diffuse texture (default)
 * * COLOR_PT_ALPHA:    override the alpha channel with @color[3]
 * * COLOR_PT_ALL:      override all color channels with @color
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
