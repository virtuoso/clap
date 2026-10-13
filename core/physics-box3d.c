// SPDX-License-Identifier: Apache-2.0
#include <box3d/box3d.h>
#include "common.h"
#include "character.h"
#include "linmath.h"
#include "messagebus.h"
#include "model.h"
#include "physics.h"
#include "physics-common.h"

static_assert(sizeof(b3Vec3) == 3 * sizeof(float), "b3Vec3 must be 3 packed floats");

DEFINE_CLEANUP(int32_t, if (*p) mem_free(*p);)
DEFINE_CLEANUP(b3Vec3, if (*p) mem_free(*p);)

/*
 * Collision filter categories. Every shape collides with everything
 * (Box3D skips static-static pairs on its own, which matches the ODE
 * backend's space setup); the categories only document intent and leave
 * room for future mask tuning.
 */
#define PHYS_CAT_STATIC     (1ull << 0)
#define PHYS_CAT_DYNAMIC    (1ull << 1)
#define PHYS_CAT_CHARACTER  (1ull << 2)

/** PHYS_SWEEP_BLOCK_DOT - sweep contacts with dir dot normal above this graze instead of blocking */
#define PHYS_SWEEP_BLOCK_DOT (-0.1f)

/**
 * PHYS_HULL_MAX_VERTICES - vertex cap requested when cooking a dynamic hull
 *
 * b3CreateHull() simplifies the hull down to the requested vertex count,
 * but its face and edge limits (B3_MAX_HULL_FACES, B3_MAX_HULL_EDGES) are
 * only checked after the fact. A smooth hull is fully triangulated, so by
 * Euler's formula the half-edge count is 6 * V - 12, which stays within
 * Box3D's 2 * B3_MAX_HULL_EDGES cap only for V <= 44 (faces, at 2 * V - 4,
 * then fit too). Asking for more fails cooking on any smooth shape.
 */
#define PHYS_HULL_MAX_VERTICES 44

/**
 * struct phys - global physics state
 * @clap_ctx:           clap context
 * @bodies:             all phys_bodies, for the contact debug draw pass
 * @time_acc:           accumulates time until the next fixed step
 * @world:              Box3D world
 * @draw_contacts:      debug draw contact points
 * @draw_capsules:      debug draw character capsules
 * @draw_velocities:    debug draw body velocities
 */
struct phys {
    clap_context    *clap_ctx;
    struct list     bodies;
    double          time_acc;
    b3WorldId       world;
    bool            draw_contacts;
    bool            draw_capsules;
    bool            draw_velocities;
};

/**
 * struct phys_body - a physical body with its collision shape
 * @phys:       core physics handle
 * @entity:     entity3d that owns this phys_body
 * @body_id:    Box3D body: static for PHYS_GEOM, kinematic for characters,
 *              dynamic for the rest
 * @shape_id:   the body's collision shape
 * @mesh_data:  cooked collision data owned by this body (static trimesh)
 * @hull_data:  cooked collision data owned by this body (dynamic trimesh)
 * @yoffset:    vertical offset of the capsule center relative to entity->dy
 * @ray_off:    vertical offset of the downward ray origin (capsule bottom cap)
 * @radius:     capsule/sphere radius
 * @half_len:   half of the capsule's cylinder length, 0 for spheres
 * @mass:       total mass as requested by phys_body_new()
 * @joint:      the joint a limb rides (phys_body_new_limb()); -1 for an
 *              entity's own body
 * @offset:     a limb's shape center in the joint's frame
 * @bounce:     contact restitution (see &struct phys_contact_params)
 * @bounce_vel: stored only: Box3D uses the world level restitution threshold
 * @mu:         contact friction coefficient
 * @soft_erp:   stored only: ODE-only contact parameter
 * @soft_cfm:   stored only: ODE-only contact parameter
 * @entry:      link on phys::bodies
 * @updated:    set by phys_body_update() so that phys_body_set_position()
 *              skips the position echo-back
 * @type:       PHYS_BODY or PHYS_GEOM (see &enum phys_type)
 * @class:      geometry class (see &enum geom_class)
 *
 * Unlike ODE, Box3D has no bodiless geometries: PHYS_GEOM objects are
 * static bodies, characters are kinematic bodies and the rest are
 * dynamic bodies.
 */
struct phys_body {
    struct phys *phys;
    entity3d    *entity;
    b3BodyId    body_id;
    b3ShapeId   shape_id;
    b3MeshData  *mesh_data;
    b3HullData  *hull_data;

    float       yoffset;
    float       ray_off;
    float       radius;
    float       half_len;
    float       mass;
    int         joint;
    vec3        offset;

    float       bounce;
    float       bounce_vel;
    float       mu;
    float       soft_erp;
    float       soft_cfm;

    struct list entry;
    int         updated;
    phys_type   type;
    geom_class  class;
};

static inline b3Vec3 b3vec3_from_vec3(const vec3 v)
{
    return (b3Vec3){ v[0], v[1], v[2] };
}

static inline b3Vec3 b3vec3_scale(b3Vec3 v, float s)
{
    return (b3Vec3){ v.x * s, v.y * s, v.z * s };
}

static inline b3Quat b3quat_from_quat(const quat q)
{
    return (b3Quat){ .v = { q[0], q[1], q[2] }, .s = q[3] };
}

static inline void quat_from_b3quat(quat rot, b3Quat q)
{
    rot[0] = q.v.x;
    rot[1] = q.v.y;
    rot[2] = q.v.z;
    rot[3] = q.s;
}

bool phys_body_has_body(struct phys_body *body)
{
    return body->type == PHYS_BODY;
}

void _phys_body_set_contact_params(struct phys_body *body, const phys_contact_params *params)
{
    body->bounce     = params->bounce;
    body->bounce_vel = params->bounce_vel;
    body->mu         = params->mu;
    body->soft_erp   = params->soft_erp;
    body->soft_cfm   = params->soft_cfm;

    b3Shape_SetFriction(body->shape_id, body->mu);
    b3Shape_SetRestitution(body->shape_id, body->bounce);

#ifndef CONFIG_FINAL
    /*
     * ERP/CFM are ODE solver concepts; Box3D's counterparts (contact
     * hertz / damping ratio) are world-level. Flag any per-body tuning
     * loudly so it doesn't go silently dead.
     */
    if (params->soft_erp > 0 || params->soft_cfm > 0)
        warn("'%s': soft_erp/soft_cfm are ODE-only contact parameters, ignored by the Box3D backend\n",
             entity_name(body->entity));
#endif /* !CONFIG_FINAL */
}

entity3d *phys_body_entity(struct phys_body *body)
{
    return body->entity;
}

void phys_body_position(struct phys_body *body, vec3 pos)
{
    b3Pos _pos = b3Body_GetPosition(body->body_id);

    vec3_setup(pos, _pos.x, _pos.y, _pos.z);
}

void phys_body_rotation(struct phys_body *body, quat rot)
{
    if (!phys_body_has_body(body)) {
        quat_identity(rot);
        return;
    }

    quat_from_b3quat(rot, b3Body_GetRotation(body->body_id));
}

static void phys_body_set_rotation(struct phys_body *body, b3Quat q)
{
    b3Body_SetTransform(body->body_id, b3Body_GetPosition(body->body_id), q);
}

void phys_body_rotate_mat3x3(struct phys_body *body, mat3x3 rot)
{
    b3Matrix3 m = {
        .cx = { rot[0][0], rot[0][1], rot[0][2] },
        .cy = { rot[1][0], rot[1][1], rot[1][2] },
        .cz = { rot[2][0], rot[2][1], rot[2][2] },
    };

    phys_body_set_rotation(body, b3NormalizeQuat(b3MakeQuatFromMatrix(&m)));
}

void phys_body_rotate_mat4x4(struct phys_body *body, mat4x4 trs)
{
    mat3x3 rot = {
        { trs[0][0], trs[0][1], trs[0][2] },
        { trs[1][0], trs[1][1], trs[1][2] },
        { trs[2][0], trs[2][1], trs[2][2] },
    };

    phys_body_rotate_mat3x3(body, rot);
}

void phys_body_rotate_xform(struct phys_body *body, transform_t *xform)
{
    const float *rot = transform_rotation_quat(xform);

    phys_body_set_rotation(body, b3quat_from_quat(rot));
}

void phys_body_set_position(struct phys_body *body, const vec3 pos)
{
    /*
     * If pos comes from phys_body_update(), no need to update it
     * here: the body already has this position and setting a body's
     * transform is a (relatively expensive) teleport in Box3D.
     */
    if (body->updated) {
        body->updated = 0;
        return;
    }

    b3Body_SetTransform(body->body_id,
                        (b3Pos){ pos[0], pos[1] + body->yoffset, pos[2] },
                        b3Body_GetRotation(body->body_id));
}

void phys_body_move(struct phys_body *body, const vec3 delta)
{
    b3Pos pos = b3Body_GetPosition(body->body_id);

    b3Body_SetTransform(body->body_id,
                        (b3Pos){ pos.x + delta[0], pos.y + delta[1], pos.z + delta[2] },
                        b3Body_GetRotation(body->body_id));
}

void phys_body_enable(struct phys_body *body, bool enable)
{
    if (!phys_body_has_body(body))  return;

    /*
     * ODE's dBodyDisable() only skips the body in the solver: the geom
     * stays collidable and ray castable, and a collision from an active
     * body re-enables it. Box3D's b3Body_Disable() removes the body from
     * the broadphase entirely, which would break ray casts and capsule
     * sweeps against it; putting the body to sleep matches ODE exactly.
     */
    b3Body_SetAwake(body->body_id, enable);
}

void phys_body_get_gravity(struct phys_body *body, vec3 gravity)
{
    b3Vec3 g = b3World_GetGravity(body->phys->world);

    gravity[0] = g.x;
    gravity[1] = g.y;
    gravity[2] = g.z;
}

void phys_body_get_velocity(struct phys_body *body, vec3 vel)
{
    if (!phys_body_has_body(body))  return;

    b3Vec3 _vel = b3Body_GetLinearVelocity(body->body_id);

    vel[0] = _vel.x;
    vel[1] = _vel.y;
    vel[2] = _vel.z;
}

void phys_body_set_velocity(struct phys_body *body, vec3 vel)
{
    if (!phys_body_has_body(body))  return;

    b3Body_SetLinearVelocity(body->body_id, b3vec3_from_vec3(vel));
}

static void phys_contact_debug_draw(struct phys *phys, float x, float y, float z, vec4 color)
{
    struct message dm = {
        .type   = MT_DEBUG_DRAW,
        .debug_draw = (struct message_debug_draw){
            .color  = { color[0], color[1], color[2], color[3] },
            .radius = 10.0,
            .shape  = DEBUG_DRAW_DISC,
            .v0     = { x, y, z },
        }
    };
    message_send(phys->clap_ctx, &dm);
}

/**
 * struct ray_cast_ctx - ray cast callback state
 * @self:           the casting entity, its own shapes are skipped
 * @hit:            [out] closest hit entity
 * @dir:            normalized ray direction
 * @frac:           [out] fraction of the ray length to the closest hit
 * @point:          [out] closest hit point
 * @normal:         [out] closest hit surface normal
 * @check_self:     true if @self has physics and needs skipping
 * @caster_alive:   ENTITY3D_ALIVE state of the caster
 */
struct ray_cast_ctx {
    entity3d    *self;
    entity3d    *hit;
    vec3        dir;
    float       frac;
    vec3        point;
    vec3        normal;
    bool        check_self;
    bool        caster_alive;
};

static float ray_cast_cb(b3ShapeId shape_id, b3Pos point, b3Vec3 normal, float fraction,
                         uint64_t user_material_id, int triangle_index, int child_index,
                         void *data)
{
    struct ray_cast_ctx *c = data;
    entity3d *target = b3Shape_GetUserData(shape_id);
    vec3 n = { normal.x, normal.y, normal.z };

    /* skip self intersections */
    if (c->check_self && target == c->self)     return -1.0f;

    /*
     * Parity with the ODE backend, which checks the ray *caster* for
     * ENTITY3D_ALIVE and rejects every hit when the caster isn't alive.
     */
    if (!c->caster_alive)                       return -1.0f;

    /*
     * Cull hits whose normal faces along the ray: Box3D already ignores
     * mesh back sides, this covers exit hits on convex shapes the ray
     * starts inside of, matching ODE's dGeomRaySetBackfaceCull().
     */
    if (vec3_mul_inner(n, c->dir) > 0)          return -1.0f;

    /* the callback may receive shapes in any order: track the closest hit */
    if (fraction < c->frac || !c->hit) {
        c->frac = fraction;
        c->hit  = target;
        vec3_setup(c->point, point.x, point.y, point.z);
        vec3_dup(c->normal, n);
    }

    return fraction;
}

static entity3d *
__phys_ray_cast(struct phys *phys, entity3d *e, const vec3 start, const vec3 dir,
                double *pdist, vec3 normal)
{
    struct ray_cast_ctx c = {
        .self         = e,
        .check_self   = entity3d_matches(e, ENTITY3D_HAS_PHYSICS),
        .caster_alive = entity3d_matches(e, ENTITY3D_ALIVE),
        .frac         = 1.0f,
    };

    /* callers pass non-normalized directions (e.g. end minus start) */
    vec3_norm(c.dir, dir);

    b3Vec3 translation = b3vec3_scale(b3vec3_from_vec3(c.dir), *pdist);
    b3Pos origin = b3vec3_from_vec3(start);

    /* a degenerate start or direction (from a broken pose, say) hits nothing */
    if (!b3IsValidPosition(origin) || !b3IsValidVec3(translation)) {
        warn_once("%s: ray cast with no valid origin or direction\n", entity_name(e));
        return nullptr;
    }

    b3World_CastRay(phys->world, origin, translation, b3DefaultQueryFilter(), ray_cast_cb, &c);

    if (!c.hit) return NULL;

    *pdist *= c.frac;
    if (normal) vec3_dup(normal, c.normal);

    return c.hit;
}

entity3d *phys_ray_cast2(struct phys *phys, entity3d *e, const vec3 start,
                         const vec3 dir, double *pdist)
{
    return __phys_ray_cast(phys, e, start, dir, pdist, NULL);
}

entity3d *phys_ray_cast(entity3d *e, const vec3 start, const vec3 dir, double *pdist)
{
    if (!entity3d_matches(e, ENTITY3D_HAS_PHYSICS))     return NULL;

    return phys_ray_cast2(e->phys_body->phys, e, start, dir, pdist);
}

void phys_ground_entity(struct phys *phys, entity3d *e)
{
    const float *start = transform_pos(&e->xform, NULL);
    vec3 dir = { 0, -1, 0 };
    double dist = 1e6;

    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS))  phys = e->phys_body->phys;

    entity3d *collision = phys_ray_cast2(phys, e, start, dir, &dist);
    if (!collision)                                 return;

    entity3d_move(e, (vec3){ 0, -dist, 0 });
}

/**
 * struct sweep_ctx - sweep shape cast callback state
 * @phys:   core physics handle
 * @self:   the swept entity, its own shapes are skipped
 * @hit:    [out] closest blocking entity
 * @dir:    normalized sweep direction
 * @frac:   [out] fraction of the sweep to the closest blocking contact
 * @normal: [out] closest blocking contact normal
 */
struct sweep_ctx {
    struct phys *phys;
    entity3d    *self;
    entity3d    *hit;
    vec3        dir;
    float       frac;
    vec3        normal;
};

/**
 * struct sweep_overlap_ctx - sweep overlap pre-pass callback state
 * @phys:   core physics handle
 * @self:   the swept entity, its own shapes are skipped
 * @hit:    [out] blocking entity, NULL if the sweep isn't blocked
 * @pos:    the swept capsule's position (collision planes are relative to it)
 * @dir:    normalized sweep direction
 * @normal: [out] blocking plane normal
 * @ndot:   most opposing dir dot normal seen so far; starts at the grazing
 *          threshold so that grazing planes (e.g. the ground) never block
 */
struct sweep_overlap_ctx {
    struct phys *phys;
    entity3d    *self;
    entity3d    *hit;
    b3Pos       pos;
    vec3        dir;
    vec3        normal;
    float       ndot;
};

static bool sweep_overlap_cb(b3ShapeId shape_id, const b3PlaneResult *planes,
                             int plane_count, void *data)
{
    struct sweep_overlap_ctx *c = data;
    entity3d *obstacle = b3Shape_GetUserData(shape_id);

    if (!obstacle || obstacle == c->self)   return true;

    for (int i = 0; i < plane_count; i++) {
        /* outward pointing: away from the obstacle, toward the capsule */
        vec3 n = { planes[i].plane.normal.x, planes[i].plane.normal.y, planes[i].plane.normal.z };
        float ndot = vec3_mul_inner(c->dir, n);

        if (unlikely(c->phys->draw_contacts))
            phys_contact_debug_draw(c->phys,
                                    c->pos.x + planes[i].point.x,
                                    c->pos.y + planes[i].point.y,
                                    c->pos.z + planes[i].point.z,
                                    (vec4){ 0.5, 0.0, 1.0, 1.0 });

        /* keep the most opposing plane */
        if (ndot < c->ndot) {
            c->ndot = ndot;
            c->hit  = obstacle;
            vec3_dup(c->normal, n);
        }
    }

    return true;
}

static float sweep_cb(b3ShapeId shape_id, b3Pos point, b3Vec3 normal, float fraction,
                      uint64_t user_material_id, int triangle_index, int child_index,
                      void *data)
{
    struct sweep_ctx *c = data;
    entity3d *obstacle = b3Shape_GetUserData(shape_id);
    vec3 n = { normal.x, normal.y, normal.z };

    if (!obstacle || obstacle == c->self)   return -1.0f;

    if (unlikely(c->phys->draw_contacts))
        phys_contact_debug_draw(c->phys, point.x, point.y, point.z, (vec4){ 0.5, 0.0, 1.0, 1.0 });

    /*
     * Ignore grazing contacts: sliding along a surface is not blocking.
     * A start-in-overlap hit (fraction 0, zero normal) also lands here by
     * design: the collide-mover pre-pass in phys_body_sweep_capsule() has
     * already established that no overlapping plane opposes the motion.
     */
    float ndot = vec3_mul_inner(c->dir, n);
    if (ndot > PHYS_SWEEP_BLOCK_DOT)        return -1.0f;

    if (fraction < c->frac || !c->hit) {
        c->frac = fraction;
        c->hit  = obstacle;
        vec3_dup(c->normal, n);
    }

    return fraction;
}

float phys_body_sweep_capsule(struct phys_body *body, const vec3 delta,
                              vec3 normal, entity3d **hit_entity)
{
    struct phys *phys = body->phys;
    float delta_len = vec3_len(delta);

    vec3_dup(normal, (vec3){ 0, 1, 0 });
    if (hit_entity)                                                 *hit_entity = NULL;

    if (delta_len < 1e-6f)                                          return 1.0f;
    if (body->class != GEOM_CAPSULE && body->class != GEOM_SPHERE)  return 1.0f;

    /*
     * Cast the body's own capsule (or sphere) along @delta in one shot;
     * Box3D returns the exact fraction at which the shapes just touch,
     * replacing the ODE backend's march-the-probe loop.
     */
    b3Pos pos = b3Body_GetPosition(body->body_id);
    entity3d *self = phys_body_entity(body);
    vec3 dir;
    b3Vec3 points[2];
    int count;

    vec3_norm(dir, delta);

    if (body->half_len > 0) {
        b3Vec3 axis = b3RotateVector(b3Body_GetRotation(body->body_id),
                                     (b3Vec3){ 0, body->half_len, 0 });
        points[0] = b3vec3_scale(axis, -1.0f);
        points[1] = axis;
        count = 2;
    } else {
        points[0] = (b3Vec3){ 0, 0, 0 };
        count = 1;
    }

    /*
     * The shape cast reports a start-in-overlap hit with a zero normal
     * (b3ShapeCast() returns before computing one), which the grazing
     * filter in sweep_cb() would silently discard, obstacle and all,
     * letting the capsule walk straight into it. Collide the capsule at
     * its current position first: a contact plane opposing the motion
     * blocks the sweep outright, the way the ODE probe march did when
     * its first probe step started out penetrating.
     */
    struct sweep_overlap_ctx oc = {
        .phys = phys,
        .self = self,
        .pos  = pos,
        .ndot = PHYS_SWEEP_BLOCK_DOT,
    };
    vec3_dup(oc.dir, dir);

    b3Capsule mover = { .center1 = points[0], .center2 = points[count - 1],
                        .radius = body->radius };
    b3World_CollideMover(phys->world, pos, &mover, b3DefaultQueryFilter(),
                         sweep_overlap_cb, &oc);
    if (oc.hit) {
        vec3_dup(normal, oc.normal);
        if (hit_entity) *hit_entity = oc.hit;
        return 0.0f;
    }

    b3ShapeProxy proxy = { .points = points, .count = count, .radius = body->radius };
    struct sweep_ctx c = {
        .phys   = phys,
        .self   = self,
        .frac   = 1.0f,
        .normal = { 0, 1, 0 },
    };
    vec3_dup(c.dir, dir);

    b3World_CastShape(phys->world, pos, &proxy, b3vec3_from_vec3(delta),
                      b3DefaultQueryFilter(), sweep_cb, &c);

    vec3_dup(normal, c.normal);
    if (hit_entity)     *hit_entity = c.hit;

    return c.frac;
}

float phys_body_get_mass(struct phys_body *body)
{
    return phys_body_has_body(body) ? body->mass : 0;
}

void phys_body_push(entity3d *hit, const vec3 velocity, float pusher_mass)
{
    if (!hit || !hit->phys_body)    return;

    struct phys_body *pb = hit->phys_body;
    if (!phys_body_has_body(pb))    return;

    /* the wake flag replaces ODE's explicit dBodyEnable() */
    b3Body_ApplyForceToCenter(pb->body_id,
                              (b3Vec3){ pusher_mass * velocity[0],
                                        pusher_mass * velocity[1],
                                        pusher_mass * velocity[2] },
                              true);
}

bool phys_body_ground_collide(struct phys_body *body, bool grounded)
{
    entity3d *e = phys_body_entity(body);
    struct phys *phys = body->phys;
    float epsilon = 1e-3;
    /*
     * Lift the ray origin above the capsule bottom by a safety margin.
     * At low framerates the sweep can leave the capsule bottom a few
     * mm inside a platform's trimesh; a ray cast from inside the mesh
     * misses, which would wrongly report airborne and trigger a
     * permanent fall-of-shame. The margin is small enough not to
     * affect the natural jump-escape threshold of the original logic.
     */
    float safety = 0.05;
    float ray_off = body->ray_off - safety;
    float ray_len = body->yoffset - ray_off + epsilon;
    struct character *ch = CRES_RET(entity3d_character(e), return true);

    if (!phys_body_has_body(body))  return true;

    ch->collision = NULL;

    b3Pos pos = b3Body_GetPosition(body->body_id);
    vec3 normal;
    double dist = ray_len * 2;

    ch->collision = __phys_ray_cast(phys, e,
                                    (vec3){ pos.x, pos.y - ray_off, pos.z },
                                    (vec3){ 0, -1, 0 }, &dist, normal);
    if (!ch->collision)             return false;

    vec3_dup(ch->normal, normal);

    if (grounded && (dist > ray_len)) {
        /* Was grounded, slightly above ground (uneven terrain): move down */
        phys_body_move(body, (vec3){ 0, -(dist - ray_len), 0 });
    } else if (dist < ray_len) {
        /* Sinking into ground: move up */
        phys_body_move(body, (vec3){ 0, ray_len - dist, 0 });
    } else if (dist > ray_len) {
        /* Above ground and wasn't grounded: airborne */
        return false;
    }

    return true;
}

static bool collide_mover_cb(b3ShapeId shape_id, const b3PlaneResult *planes,
                             int plane_count, void *data)
{
    struct phys_body *body = data;
    entity3d *target = b3Shape_GetUserData(shape_id);
    b3Pos pos = b3Body_GetPosition(body->body_id);

    if (target == body->entity)     return true;

    for (int i = 0; i < plane_count; i++)
        phys_contact_debug_draw(body->phys,
                                pos.x + planes[i].point.x,
                                pos.y + planes[i].point.y,
                                pos.z + planes[i].point.z,
                                (vec4){ 1.0, 0.0, 0.0, 1.0 });

    return true;
}

/**
 * phys_step_debug_collide() - debug draw contact points of character capsules
 * @phys:   core physics handle
 *
 * Characters are kinematic and produce no contact events, so collide their
 * capsules with the world the way a character mover would. This is also the
 * hook for real character depenetration (accumulate plane pushes, apply via
 * phys_body_move()) if it's ever needed; the ODE backend's equivalent pass
 * was dead code.
 */
static void phys_step_debug_collide(struct phys *phys)
{
    struct phys_body *body;

    list_for_each_entry(body, &phys->bodies, entry) {
        if (!entity3d_matches(body->entity, ENTITY3D_IS_CHARACTER))     continue;

        b3Capsule mover = {
            .center1 = { 0, -body->half_len, 0 },
            .center2 = { 0, body->half_len, 0 },
            .radius  = body->radius,
        };
        b3World_CollideMover(phys->world, b3Body_GetPosition(body->body_id),
                             &mover, b3DefaultQueryFilter(), collide_mover_cb, body);
    }
}

/* one of a reporting entity's kinematic shapes overlaps @shape_id: the first plane says where */
static bool contact_report_cb(b3ShapeId shape_id, const b3PlaneResult *planes,
                              int plane_count, void *data)
{
    struct phys_body *body = data;
    entity3d *other = b3Shape_GetUserData(shape_id);

    if (other == body->entity || !plane_count)  return true;

    b3Pos origin = b3Body_GetPosition(body->body_id);
    vec3 pos = { origin.x + planes[0].point.x, origin.y + planes[0].point.y, origin.z + planes[0].point.z };
    vec3 normal = { planes[0].plane.normal.x, planes[0].plane.normal.y, planes[0].plane.normal.z };

    animator_contact_enqueue(body->entity, body->joint, other, pos, normal);

    return true;
}

/* the joint an entity's shape rides: its own body's or one of its limbs' */
static int shape_joint(entity3d *e, b3ShapeId shape_id)
{
    struct phys_body **limb;

    darray_for_each(limb, e->limbs)
        if (B3_ID_EQUALS((*limb)->shape_id, shape_id))  return (*limb)->joint;

    return -1;
}

/* a dynamic reporting body began touching something: the step's own contact says where */
static void contact_report_event(entity3d *e, b3ShapeId shape_id, entity3d *other,
                                 const b3ContactBeginTouchEvent *ev, bool flip)
{
    if (!entity3d_matches(e, ENTITY3D_CONTACT_EVENTS))                  return;
    if (b3Body_GetType(b3Shape_GetBody(shape_id)) != b3_dynamicBody)    return;

    vec3 pos, normal = { 0.0f, 0.0f, 0.0f };
    b3Pos p = b3Body_GetPosition(b3Shape_GetBody(shape_id));

    vec3_dup(pos, (vec3){ p.x, p.y, p.z });
    if (b3Contact_IsValid(ev->contactId)) {
        b3ContactData cd = b3Contact_GetData(ev->contactId);

        if (cd.manifoldCount && cd.manifolds[0].pointCount) {
            const b3Manifold *m = &cd.manifolds[0];
            b3Pos pa = b3Body_GetPosition(b3Shape_GetBody(cd.shapeIdA));

            vec3_dup(pos, (vec3){ pa.x + m->points[0].anchorA.x, pa.y + m->points[0].anchorA.y,
                                  pa.z + m->points[0].anchorA.z });
            /* the manifold normal points from A to B; ours from the other toward the entity */
            float s = flip ? 1.0f : -1.0f;
            vec3_dup(normal, (vec3){ s * m->normal.x, s * m->normal.y, s * m->normal.z });
        }
    }

    animator_contact_enqueue(e, shape_joint(e, shape_id), other, pos, normal);
}

/*
 * phys_step_contacts() - report what the entities asking for contacts touch
 * @phys:   core physics handle
 *
 * A kinematic body (a character capsule, a limb) makes contacts in the step
 * with dynamic shapes only, so each reporting one is collided with the world
 * the way a mover is, which finds static, kinematic and dynamic shapes
 * alike. A dynamic reporting body has the step's begin-touch events.
 */
static void phys_step_contacts(struct phys *phys)
{
    struct phys_body *body;

    list_for_each_entry(body, &phys->bodies, entry) {
        if (!entity3d_matches(body->entity, ENTITY3D_CONTACT_EVENTS))       continue;
        if (b3Body_GetType(body->body_id) != b3_kinematicBody)              continue;
        if (body->class != GEOM_CAPSULE && body->class != GEOM_SPHERE)      continue;

        b3Vec3 axis = b3RotateVector(b3Body_GetRotation(body->body_id), (b3Vec3){ 0, body->half_len, 0 });
        b3Capsule mover = { .center1 = b3Neg(axis), .center2 = axis, .radius = body->radius };

        b3World_CollideMover(phys->world, b3Body_GetPosition(body->body_id), &mover,
                             b3DefaultQueryFilter(), contact_report_cb, body);
    }

    b3ContactEvents events = b3World_GetContactEvents(phys->world);

    for (int i = 0; i < events.beginCount; i++) {
        b3ContactBeginTouchEvent *ev = &events.beginEvents[i];

        if (!b3Shape_IsValid(ev->shapeIdA) || !b3Shape_IsValid(ev->shapeIdB))   continue;

        entity3d *ea = b3Shape_GetUserData(ev->shapeIdA), *eb = b3Shape_GetUserData(ev->shapeIdB);

        if (!ea || !eb || ea == eb)     continue;

        contact_report_event(ea, ev->shapeIdA, eb, ev, false);
        contact_report_event(eb, ev->shapeIdB, ea, ev, true);
    }
}

static void __phys_step(struct phys *phys, double dt)
{
    if (unlikely(phys->draw_contacts))
        phys_step_debug_collide(phys);

    b3World_Step(phys->world, dt, 4);
    phys_step_contacts(phys);

    if (unlikely(phys->draw_contacts)) {
        b3ContactEvents events = b3World_GetContactEvents(phys->world);

        for (int i = 0; i < events.hitCount; i++) {
            b3ContactHitEvent *hit = &events.hitEvents[i];

            phys_contact_debug_draw(phys, hit->point.x, hit->point.y, hit->point.z,
                                    (vec4){ 1.0, 0.0, 0.0, 1.0 });
        }
    }
}

void phys_step(struct phys *phys, double dt)
{
    const double fixed_dt = 1.0 / 120.0;

    phys->time_acc += dt;

    int steps, max_steps;
    for (steps = 0, max_steps = 5;
         phys->time_acc >= fixed_dt && steps < max_steps;
         phys->time_acc -= fixed_dt, steps++)
        __phys_step(phys, fixed_dt);

    if (steps == max_steps)
        phys->time_acc = 0.0;
}

int phys_body_update(entity3d *e)
{
    if (!entity3d_matches(e, ENTITY3D_PHYS_IS_BODY))    return 0;

    struct phys_body *body = e->phys_body;
    b3Pos pos = b3Body_GetPosition(body->body_id);

    body->updated++;
    entity3d_position(e, (vec3){ pos.x, pos.y - body->yoffset, pos.z });
    /*
     * Set entity's rotation from the physics body, unless it's a character;
     * character capsules are always upright
     */
    if (!entity3d_matches(e, ENTITY3D_IS_CHARACTER)) {
        quat rot;
        phys_body_rotation(body, rot);
        transform_set_quat(&e->xform, rot);
    }

    b3Vec3 vel = b3Body_GetLinearVelocity(body->body_id);
    return vec3_len((vec3){ vel.x, vel.y, vel.z }) > 1e-3 ? 1 : 0;
}

static void phys_capsule_shape_params(struct phys_body *body, entity3d *e,
                                      double geom_radius, double geom_offset)
{
    phys_capsule cap = phys_capsule_size(e, geom_radius, geom_offset);

    body->radius = cap.radius;
    body->half_len = cap.length / 2;
    body->yoffset = cap.yoffset;
    body->ray_off = cap.ray_off;
}

static void phys_sphere_shape_params(struct phys_body *body, entity3d *e,
                                     double geom_radius, double geom_offset)
{
    float r = geom_radius ? geom_radius :
              min3(entity3d_aabb_X(e), entity3d_aabb_Y(e), entity3d_aabb_Z(e)) / 2;

    body->radius = r;
    body->half_len = 0;
    body->yoffset = geom_offset ? geom_offset : r;
    body->ray_off = r;
}

/* a capsule along Y of body::half_len each way, or a sphere for none, of body::radius */
static b3ShapeId phys_shape_capsule_new(struct phys_body *body, const b3ShapeDef *sd)
{
    if (body->half_len > 0) {
        b3Capsule capsule = {
            .center1 = { 0, -body->half_len, 0 },
            .center2 = { 0, body->half_len, 0 },
            .radius  = body->radius,
        };

        return b3CreateCapsuleShape(body->body_id, sd, &capsule);
    }

    b3Sphere sphere = { .center = { 0, 0, 0 }, .radius = body->radius };

    return b3CreateSphereShape(body->body_id, sd, &sphere);
}

#ifndef CONFIG_FINAL
/*
 * Cooking drops degenerate triangles, which is a modeling problem: the
 * collision mesh no longer matches the render mesh there. Ask for the
 * list, which can't be longer than one entry per triangle, and hand it
 * to the model for the debug UI to flag and draw.
 */
static b3MeshData *phys_mesh_cook(const b3MeshDef *md, model3d *m, entity3d *e)
{
    LOCAL_SET(int32_t, degenerate) = mem_alloc(sizeof(*degenerate), .nr = md->triangleCount,
                                               .fatal_fail = 1);
    b3MeshData *mesh = b3CreateMesh(md, degenerate, md->triangleCount);

    if (mesh && mesh->degenerateCount) {
        warn("'%s': %d degenerate collision triangles\n", entity_name(e), mesh->degenerateCount);
        CERR_RET(model3d_set_degenerate_tris(m, degenerate, mesh->degenerateCount),
                 err_cerr(__cerr, "'%s': can't store degenerate triangles\n", entity_name(e)));
    }

    return mesh;
}
#else
static inline b3MeshData *phys_mesh_cook(const b3MeshDef *md, model3d *m, entity3d *e)
{
    return b3CreateMesh(md, NULL, 0);
}
#endif /* CONFIG_FINAL */

static cerr phys_shape_trimesh_new(struct phys_body *body, const b3ShapeDef *sd, entity3d *e)
{
    model3d *m = e->txmodel->model;
    unsigned short *idx = m->collision_idx;
    size_t idxsz = m->collision_idxsz / sizeof(unsigned short);
    size_t vxsz = m->collision_vxsz / sizeof(float);
    float scale = e->parent ? e->parent->scale : e->scale;

    if (phys_body_has_body(body)) {
        /*
         * Box3D mesh shapes are static-only: approximate dynamic
         * trimeshes with a convex hull of the collision vertices.
         * Scale is baked in; the rotation lives on the body.
         */
        LOCAL_SET(b3Vec3, points) = mem_alloc(sizeof(*points), .nr = vxsz / 3, .fatal_fail = 1);

        for (int i = 0; i < vxsz; i += 3) {
            points[i / 3].x = m->collision_vx[i + 0] * scale;
            points[i / 3].y = m->collision_vx[i + 1] * scale;
            points[i / 3].z = m->collision_vx[i + 2] * scale;
        }

        body->hull_data = b3CreateHull(points, vxsz / 3, PHYS_HULL_MAX_VERTICES);
        if (!body->hull_data)
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "'%s': convex hull cooking failed",
                .arg0   = entity_name(e)
            );

        body->shape_id = b3CreateHullShape(body->body_id, sd, body->hull_data);
    } else {
        LOCAL_SET(int32_t, tidx) = mem_alloc(sizeof(*tidx), .nr = idxsz, .fatal_fail = 1);

        for (int i = 0; i < idxsz; i++)
            tidx[i] = idx[i];

        /*
         * The collision mesh is a copy of the render mesh, so vertices
         * are duplicated along normal/UV seams: weld them back. Edge
         * identification prevents internal-edge snags, like ODE's
         * face-angles preprocessing did.
         */
        b3MeshDef md = {
            .vertices       = (b3Vec3 *)m->collision_vx,
            .indices        = tidx,
            .vertexCount    = vxsz / 3,
            .triangleCount  = idxsz / 3,
            .weldVertices   = true,
            .weldTolerance  = 1e-4f,
            .identifyEdges  = true,
        };
        body->mesh_data = phys_mesh_cook(&md, m, e);
        if (!body->mesh_data)
            return CERR_INVALID_ARGUMENTS_REASON(
                .fmt    = "'%s': collision mesh cooking failed",
                .arg0   = entity_name(e)
            );

        body->shape_id = b3CreateMeshShape(body->body_id, sd, body->mesh_data,
                                           (b3Vec3){ scale, scale, scale });
    }

    if (B3_IS_NULL(body->shape_id))
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "'%s': trimesh shape creation failed",
            .arg0   = entity_name(e)
        );

    return CERR_OK;
}

struct phys_body *phys_body_new(struct phys *phys, entity3d *entity, geom_class class,
                                double geom_radius, double geom_offset, phys_type type,
                                double mass)
{
    bool has_body = type == PHYS_BODY;
    bool is_character = entity3d_matches(entity, ENTITY3D_IS_CHARACTER);

    if (class != GEOM_SPHERE && class != GEOM_CAPSULE && class != GEOM_TRIMESH)     return NULL;

    struct phys_body *body = mem_alloc(sizeof(*body), .zero = 1, .fatal_fail = 1);
    list_init(&body->entry);
    body->phys = phys;
    body->entity = entity;
    body->type = type;
    body->class = class;
    body->mass = mass;
    body->mu = 1.0;
    body->joint = -1;

    /* shape sizing first: the body def needs yoffset for its position */
    if (class == GEOM_CAPSULE)
        phys_capsule_shape_params(body, entity, geom_radius * entity->scale,
                                  geom_offset * entity->scale);
    else if (class == GEOM_SPHERE)
        phys_sphere_shape_params(body, entity, geom_radius * entity->scale,
                                 geom_offset * entity->scale);

    const float *pos = transform_pos(&entity->xform, NULL);
    b3BodyDef bd = b3DefaultBodyDef();

    if (!has_body)
        bd.type = b3_staticBody;
    else if (is_character)
        /*
         * Characters use kinematic movement (sweep-and-slide): the
         * dynamics solver never moves them, only the character
         * controller does, via phys_body_move().
         */
        bd.type = b3_kinematicBody;
    else
        bd.type = b3_dynamicBody;
    bd.position = (b3Pos){ pos[0], pos[1] + body->yoffset, pos[2] };
    bd.userData = entity;
    bd.name = entity_name(entity);
    /* parity with the ODE backend's world-level linear damping */
    if (bd.type == b3_dynamicBody)  bd.linearDamping = 0.001;

    body->body_id = b3CreateBody(phys->world, &bd);
    if (B3_IS_NULL(body->body_id))  goto err_free;

    b3ShapeDef sd = b3DefaultShapeDef();
    sd.userData = entity;
    sd.baseMaterial.friction = body->mu;
    sd.baseMaterial.restitution = body->bounce;
    sd.filter.categoryBits = !has_body ? PHYS_CAT_STATIC :
                             (is_character ? PHYS_CAT_CHARACTER : PHYS_CAT_DYNAMIC);
    sd.filter.maskBits = UINT64_MAX;
    /* hit events feed the contact debug draw; characters emit none anyway */
    sd.enableHitEvents = bd.type == b3_dynamicBody;

    if (class == GEOM_TRIMESH) {
        CERR_RET(phys_shape_trimesh_new(body, &sd, entity),
                 { err_cerr(__cerr, "'%s': no collision shape\n", entity_name(entity)); goto err_body; });
    } else {
        body->shape_id = phys_shape_capsule_new(body, &sd);
    }

    if (B3_IS_NULL(body->shape_id)) goto err_body;

    if (bd.type == b3_dynamicBody && mass > 0) {
        /* scale the shape-derived mass data to the requested total mass */
        b3MassData md = b3Body_GetMassData(body->body_id);

        if (md.mass > 0) {
            float mass_scale = mass / md.mass;

            md.mass = mass;
            md.inertia.cx = b3vec3_scale(md.inertia.cx, mass_scale);
            md.inertia.cy = b3vec3_scale(md.inertia.cy, mass_scale);
            md.inertia.cz = b3vec3_scale(md.inertia.cz, mass_scale);
            b3Body_SetMassData(body->body_id, md);
        }
    }

    list_append(&phys->bodies, &body->entry);

    return body;

err_body:
    b3DestroyBody(body->body_id);
    if (body->mesh_data)    b3DestroyMesh(body->mesh_data);
    if (body->hull_data)    b3DestroyHull(body->hull_data);
err_free:
    mem_free(body);

    return NULL;
}

struct phys_body *phys_body_new_limb(struct phys *phys, entity3d *e, int joint, geom_class class,
                                     double radius, double half_len, const vec3 offset)
{
    if (class != GEOM_CAPSULE && class != GEOM_SPHERE)  return NULL;
    if (!phys_limb_joint_valid(e, joint))               return NULL;

    struct phys_body *body = mem_alloc(sizeof(*body), .zero = 1, .fatal_fail = 1);
    list_init(&body->entry);
    body->phys = phys;
    body->entity = e;
    body->type = PHYS_BODY;
    body->class = class;
    body->mu = 1.0;
    body->joint = joint;
    vec3_dup(body->offset, (float *)offset);
    body->radius = radius * e->scale;
    body->half_len = class == GEOM_CAPSULE ? half_len * e->scale : 0.0f;

    /* placed by phys_limbs_sync() every frame; starts where the entity is */
    const float *pos = transform_pos(&e->xform, nullptr);
    b3BodyDef bd = b3DefaultBodyDef();

    bd.type = b3_kinematicBody;
    bd.position = (b3Pos){ pos[0], pos[1], pos[2] };
    bd.userData = e;
    bd.name = entity_name(e);

    body->body_id = b3CreateBody(phys->world, &bd);
    if (B3_IS_NULL(body->body_id))  goto err_free;

    /* a character capsule's terms: the dynamics bounce off it, kinematics pass through */
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.userData = e;
    sd.baseMaterial.friction = body->mu;
    sd.filter.categoryBits = PHYS_CAT_CHARACTER;
    sd.filter.maskBits = UINT64_MAX;
    sd.enableHitEvents = false;

    body->shape_id = phys_shape_capsule_new(body, &sd);
    if (B3_IS_NULL(body->shape_id)) goto err_body;

    struct phys_body **slot = darray_add(e->limbs);
    if (!slot)  goto err_body;
    *slot = body;

    list_append(&phys->bodies, &body->entry);

    return body;

err_body:
    b3DestroyBody(body->body_id);
err_free:
    mem_free(body);

    return NULL;
}

int phys_body_joint(struct phys_body *body)
{
    return body->joint;
}

void phys_limbs_sync(entity3d *e)
{
    struct phys_body **limb;

    if (!e->joint_transforms)   return;

    darray_for_each(limb, e->limbs) {
        mat4x4 r;
        vec3 pos;
        quat rot;

        phys_limb_place(e, (*limb)->joint, (*limb)->offset, pos, r);
        quat_from_mat4x4(rot, r);

        b3Pos p = { pos[0], pos[1], pos[2] };
        b3Quat q = b3NormalizeQuat(b3quat_from_quat(rot));

        /* a degenerate joint (a zero scale, say) makes no transform: the limb stays where it was */
        if (!b3IsValidPosition(p) || !b3IsValidQuat(q)) {
            warn_once("%s: limb at joint %d has no valid transform\n", entity_name(e), (*limb)->joint);
            continue;
        }

        b3Body_SetTransform((*limb)->body_id, p, q);
    }
}

void phys_body_done(struct phys_body *body)
{
    list_del(&body->entry);
    /* destroying the body destroys its shapes; cooked data goes last */
    b3DestroyBody(body->body_id);
    if (body->mesh_data)    b3DestroyMesh(body->mesh_data);
    if (body->hull_data)    b3DestroyHull(body->hull_data);
    mem_free(body);
}

static int box3d_assert(const char *condition, const char *file_name, int line_number)
{
    err("Box3D assertion '%s' at %s:%d\n", condition, file_name, line_number);

    /* non-zero: proceed to the debugger break / trap, like the ODE handler did */
    return 1;
}

static void box3d_log(const char *message)
{
    warn("Box3D: %s\n", message);
}

struct phys *phys_init(struct clap_context *ctx)
{
    struct phys *phys = mem_alloc(sizeof(*phys));

    if (!phys)  return NULL;

    b3SetAssertFcn(box3d_assert);
    b3SetLogFcn(box3d_log);
    /*
     * Box3D's allocator hooks are deliberately not wired to mem_alloc():
     * b3AllocFcn() must honor the requested alignment (up to SIMD vector
     * alignment), which mem_alloc() does not guarantee.
     */

    b3WorldDef def = b3DefaultWorldDef();
    def.gravity = (b3Vec3){ 0, -9.8, 0 };
    def.enableSleep = true;

    phys->world = b3CreateWorld(&def);
    if (!b3World_IsValid(phys->world)) {
        mem_free(phys);
        return NULL;
    }

    phys->clap_ctx = ctx;
    phys->time_acc = 0.0;
    phys->draw_capsules = false;
    phys->draw_contacts = false;
    phys->draw_velocities = false;
    list_init(&phys->bodies);

    return phys;
}

void phys_done(struct phys *phys)
{
    b3DestroyWorld(phys->world);
    mem_free(phys);
}

void phys_contacts_debug_enable(struct phys *phys, bool enable)
{
    phys->draw_contacts = enable;
}

void phys_capsules_debug_enable(struct phys *phys, bool enable)
{
    phys->draw_capsules = enable;
}

void phys_velocities_debug_enable(struct phys *phys, bool enable)
{
    phys->draw_velocities = enable;
}

static void phys_debug_draw_velocity(struct phys_body *body)
{
    if (likely(!body->phys->draw_velocities))   return;
    if (likely(!phys_body_has_body(body)))      return;

    b3Vec3 vel = b3Body_GetLinearVelocity(body->body_id);
    vec3 v = { vel.x, vel.y, vel.z };
    if (likely(vec3_mul_inner(v, v) < 1e-3))    return;

    vec3_norm(v, v);

    b3Pos pos = b3Body_GetPosition(body->body_id);

    message_send(body->phys->clap_ctx, &(struct message) {
        .type           = MT_DEBUG_DRAW,
        .debug_draw     = (struct message_debug_draw) {
            .color      = { 0.0, 1.0, 0.0, 1.0 },
            .thickness  = 2.0,
            .shape      = DEBUG_DRAW_LINE,
            .v0         = { pos.x, pos.y, pos.z },
            .v1         = { pos.x + v[0], pos.y + v[1], pos.z + v[2] },
        }
    });
}

void phys_debug_draw(struct scene *scene, struct phys_body *body)
{
    phys_debug_draw_velocity(body);

    if (likely(!body->phys->draw_capsules))                         return;
    if (body->class != GEOM_CAPSULE && body->class != GEOM_SPHERE)  return;

    b3Pos pos = b3Body_GetPosition(body->body_id);
    float r = body->radius, len = body->half_len * 2;

    struct message dm   = {
        .type           = MT_DEBUG_DRAW,
        .debug_draw     = (struct message_debug_draw){
            .color      = { 1.0, 0.0, 0.0, 1.0 },
            .thickness  = 4.0,
            .shape      = DEBUG_DRAW_LINE,
        }
    };
    vec3_dup(dm.debug_draw.v0, (vec3){ pos.x - r, pos.y  - len / 2 - r, pos.z - r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos.x + r, pos.y  + len / 2 + r, pos.z + r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos.x + r, pos.y - len / 2 - r, pos.z + r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos.x - r, pos.y + len / 2 + r, pos.z - r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos.x - r, pos.y - len / 2 - r, pos.z + r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos.x + r, pos.y + len / 2 + r, pos.z - r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos.x + r, pos.y - len / 2 - r, pos.z - r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos.x - r, pos.y + len / 2 + r, pos.z + r });
    message_send(body->phys->clap_ctx, &dm);
}
