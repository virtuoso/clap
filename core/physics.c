// SPDX-License-Identifier: Apache-2.0
#include <ode/ode.h>
#include "common.h"
#include "character.h"
#include "linmath.h"
#include "messagebus.h"
#include "model.h"
#include "physics.h"
#include "ui-debug.h"

/*
 * Global physics state
 */
struct phys {
    dWorldID    world;
    dSpaceID    space;
    dSpaceID    character_space;
    dSpaceID    ground_space;
    dSpaceID    collision;
    dJointGroupID contact;
    ground_contact_fn ground_contact;
    double      time_acc;
    bool        draw_contacts;
    bool        draw_capsules;
    bool        draw_velocities;
    struct clap_context *clap_ctx;
};

/*
 * An internal representation of a physical object that participates
 * in collision detection and, if if has a "body", in dynamics simulation
 */
struct phys_body {
    struct phys *phys;
    /* geom is always set */
    dGeomID     geom;
    /* body may not be, in case of collision geom */
    dBodyID     body;

    /*
     * capsule specific:
     * @yoffset: vertical offset of the center of mass
     *           relative to entity->dy
     * @ray_off: vertical offset for the beginning of
     *           ray cast downwards (capsule cap)
     */
    dReal       yoffset;
    dReal       ray_off;
    dReal       radius;
    /* motor that fixes us in space and moves us around */
    dJointID    lmotor;

    /* contact.surface parameters */
    dReal       bounce;
    dReal       bounce_vel;
    /* not sure we even need to store the mass */
    dMass       mass;
    struct list entry;

    /* stuff communicated from near_callback() */
    struct list pen_entry;
    vec3        pen_norm;
    dReal       pen_depth;
    dReal       *trimesh_vx;
    dTriIndex   *trimesh_idx;
    int         updated;
    geom_class  class;
};

bool phys_body_has_body(struct phys_body *body)
{
    return !!body->body;
}

void _phys_body_set_contact_params(struct phys_body *body, const phys_contact_params *params)
{
    body->bounce = params->bounce;
    body->bounce_vel = params->bounce_vel;
}

entity3d *phys_body_entity(struct phys_body *body)
{
    return dGeomGetData(body->geom);
}

void phys_body_position(struct phys_body *body, vec3 pos)
{
    const dReal *_pos = dGeomGetPosition(body->geom);

    vec3_setup(pos, _pos[0], _pos[1], _pos[2]);
}

void phys_body_rotation(struct phys_body *body, quat rot)
{
    if (!phys_body_has_body(body)) {
        quat_identity(rot);
        return;
    }

    const dReal *_rot = dBodyGetQuaternion(body->body);

    rot[0] = _rot[1];
    rot[1] = _rot[2];
    rot[2] = _rot[3];
    rot[3] = _rot[0];
}

void phys_body_rotate_mat4x4(struct phys_body *body, mat4x4 trs)
{
    dMatrix3 r = {
        trs[0][0], trs[1][0], trs[2][0], 0.0,
        trs[0][1], trs[1][1], trs[2][1], 0.0,
        trs[0][2], trs[1][2], trs[2][2], 0.0,
    };

    if (phys_body_has_body(body))
        dBodySetRotation(body->body, r);
    else
        dGeomSetRotation(body->geom, r);
}

void phys_body_rotate_xform(struct phys_body *body, transform_t *xform)
{
    const float *rot = transform_rotation_quat(xform);
    const dReal _rot[] = { rot[3], rot[0], rot[1], rot[2] };

    if (phys_body_has_body(body))
        dBodySetQuaternion(body->body, _rot);
    else
        dGeomSetQuaternion(body->geom, _rot);
}

/*
 * Separate thread? Emscripten won't be happy.
 */
#define MAX_CONTACTS 16

/*
 * Given the contact information, return the geometry with the class @class in
 * *match and the other one in *other. Both pointers can be NULL. Note that
 * dGeomID is already a pointer.
 */
static bool geom_and_other_by_class(dContactGeom *geom, int class, dGeomID *match, dGeomID *other)
{
    if (match)
        *match = NULL;
    if (other)
        *other = NULL;

    if (geom->g1 && dGeomGetClass(geom->g1) == class) {
        if (match)
            *match = geom->g1;
        if (other)
            *other = geom->g2;
        return true;
    } else if (geom->g2 && dGeomGetClass(geom->g2) == class) {
        if (match)
            *match = geom->g2;
        if (other)
            *other = geom->g1;
        return true;
    }

    return false;
}

/*
 * Given the contact information, return the entity with the geometry matching
 * @class in *match and the other one in *other. Both pointers can be NULL.
 * Useful to get the entity that a ray (dRayClass) hits or the character
 * (dCapsuleClass) that collides with the ground.
 */
static bool entity_and_other_by_class(dContactGeom *geom, int class, entity3d **match,
                                      entity3d **other)
{
    dGeomID _match, _other;

    if (match)
        *match = NULL;
    if (other)
        *other = NULL;

    if (geom_and_other_by_class(geom, class, &_match, &_other)) {
        if (match)
            *match = dGeomGetData(_match);
        if (other)
            *other = dGeomGetData(_other);
        return true;
    }

    return false;
}

void phys_body_set_position(struct phys_body *body, const vec3 pos)
{
    /*
     * If pos comes from dBodyGetPosition(), no need to update it
     * here. Also, if dReal is double, some precision will be lost
     * and this will end up setting a slightly different position,
     * which can't happen in the collider path.
     */
    if (body->updated) {
        body->updated = 0;
        return;
    }

    if (phys_body_has_body(body))
        dBodySetPosition(body->body, pos[0], pos[1] + body->yoffset, pos[2]);
    else
        dGeomSetPosition(body->geom, pos[0], pos[1] + body->yoffset, pos[2]);
}

void phys_body_enable(struct phys_body *body, bool enable)
{
    if (!phys_body_has_body(body))
        return;

    if (enable)
        dBodyEnable(body->body);
    else
        dBodyDisable(body->body);
}

void phys_body_get_velocity(struct phys_body *body, vec3 vel)
{
    if (!phys_body_has_body(body))
        return;

    const dReal *_vel = dBodyGetLinearVel(body->body);
    vel[0] = _vel[0];
    vel[1] = _vel[1];
    vel[2] = _vel[2];
}

void phys_body_set_velocity(struct phys_body *body, vec3 vel)
{
    if (!phys_body_has_body(body))
        return;

    dBodySetLinearVel(body->body, vel[0], vel[1], vel[2]);
}

void phys_body_attach_motor(struct phys_body *body, bool attach)
{
    dJointAttach(body->lmotor, attach ? body->body : NULL, NULL);
}

void phys_body_set_motor_velocity(struct phys_body *body, bool body_also, vec3 vel)
{
    if (!phys_body_has_body(body))
        return;

    if (!dJointGetBody(body->lmotor, 0))
        phys_body_attach_motor(body, true);

    dJointSetLMotorParam(body->lmotor, dParamVel1, vel[0]);
    dJointSetLMotorParam(body->lmotor, dParamVel2, vel[1]);
    dJointSetLMotorParam(body->lmotor, dParamVel3, vel[2]);
    if (body_also)
        phys_body_set_velocity(body, vel);
    dBodySetAngularVel(body->body, 0.0, 0.0, 0.0);
}

void phys_body_stop(struct phys_body *body)
{
    if (!phys_body_has_body(body))
        return;

    phys_body_set_motor_velocity(body, true, (vec3){ 0, 0, 0 });
    dBodySetLinearDampingThreshold(body->body, 0.001);
}

static void phys_body_stick(struct phys_body *body, dContact *contact)
{
    entity3d *e = phys_body_entity(body);
    struct phys *phys = body->phys;
    struct character *c = e->priv;
    dJointID j;

    /* should also warn */
    if (!phys_body_has_body(body))
        return;

    if (c) {
        c->normal[0] = contact->geom.normal[0];
        c->normal[1] = contact->geom.normal[1];
        c->normal[2] = contact->geom.normal[2];
    }

    j = dJointCreateContact(phys->world, phys->contact, contact);
    dJointAttach(j, body->body, NULL);
}

static void phys_contact_surface(entity3d *e1, entity3d *e2, dContact *contact, int nc)
{
    int i;

    for (i = 0; i < nc; i++) {
        memset(&contact[i], 0, sizeof(dContact));
        contact[i].surface.mode = /*dContactBounce | */dContactSoftCFM | dContactSoftERP;
        contact[i].surface.mu = /*bounce != 0 ? dInfinity : */0;
        contact[i].surface.mu2 = 0;
        contact[i].surface.bounce = 0.01;
        contact[i].surface.bounce_vel = 10.0;
        contact[i].surface.soft_cfm = 0.01;
        contact[i].surface.soft_erp = 0;
    }
}

#ifndef CONFIG_FINAL
static __unused const char *class_str(int class)
{
    static const char *classes[] = {
        [dSphereClass]             = "sphere",
        [dBoxClass]                = "box",
        [dCapsuleClass]            = "capsule",
        [dCylinderClass]           = "cylinder",
        [dPlaneClass]              = "plane",
        [dRayClass]                = "ray",
        [dConvexClass]             = "convex",
        [dGeomTransformClass]      = "geom_transform",
        [dTriMeshClass]            = "trimesh",
        [dHeightfieldClass]        = "heightfield",
        [dSimpleSpaceClass]        = "simple_space",
        [dHashSpaceClass]          = "hash_space",
        [dSweepAndPruneSpaceClass] = "sap_space",
        [dQuadTreeSpaceClass]      = "quadtree_space",
    };

    if (class < array_size(classes) && classes[class])
        return classes[class];
    return "<unknown>";
}
#endif /* CONFIG_FINAL */

/*
 * It's not possible to move bodies inside collider call paths, so they are put
 * on a list that is then handled in phys_step() after the collider functions
 * are done.
 */
static void entity_pen_push(entity3d *e, dContact *contact, struct list *pen)
{
    vec3 norm = { contact->geom.normal[0], contact->geom.normal[1], contact->geom.normal[2] };

    if (!e->phys_body)
        return;

    e->phys_body->pen_depth += contact->geom.depth;
    vec3_scale(norm, norm, contact->geom.depth);
    vec3_add(e->phys_body->pen_norm, e->phys_body->pen_norm, norm);
    list_del(&e->phys_body->pen_entry);
    list_append(pen, &e->phys_body->pen_entry);
}

/*
 * Get contact points between two potentially colliding geometries and if they
 * do, put them on the collision list, where they later get resolved in phys_step().
 */
static void near_callback(void *data, dGeomID o1, dGeomID o2)
{
    dContact contact[MAX_CONTACTS];
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);
    struct list *pen = data;
    dJointID j;
    int i, nc;

    phys_contact_surface(NULL, NULL, contact, MAX_CONTACTS);

    /*
     * There might be a reason to handle subspaces here, like got_contact does,
     * except for currently there are only 2 subspaces of phys->space, and only
     * phys->characters can have (useful) collisions between themselves.
     */
    nc = dCollide(o1, o2, MAX_CONTACTS, &contact[0].geom, sizeof(dContact));
    if (nc) {
        for (i = 0; i < nc; i++) {
            dGeomID g1 = contact[i].geom.g1;
            dGeomID g2 = contact[i].geom.g2;
            entity3d *e1 = dGeomGetData(g1);
            entity3d *e2 = dGeomGetData(g2);
            struct phys *phys = e1->phys_body->phys;

            if (unlikely(phys->draw_contacts)) {
                struct message dm = {
                    .type   = MT_DEBUG_DRAW,
                    .debug_draw = (struct message_debug_draw){
                        .color  = { 1.0, 0.0, 0.0, 1.0 },
                        .radius = 10.0,
                        .shape  = DEBUG_DRAW_DISC,
                        .v0     = { contact[i].geom.pos[0], contact[i].geom.pos[1], contact[i].geom.pos[2] },
                    }
                };
                message_send(phys->clap_ctx, &dm);
            }

            b1 = dGeomGetBody(g1);
            b2 = dGeomGetBody(g2);
            phys_body_update(e1);
            phys_body_update(e2);
            j = dJointCreateContact(phys->world, phys->contact, &contact[i]);
            dJointAttach(j, b1, b2);
            if (phys_body_has_body(e1->phys_body) && phys_body_has_body(e2->phys_body)) {
                entity_pen_push(e1, &contact[i], pen);
                entity_pen_push(e2, &contact[i], pen);
            } else if (!phys_body_has_body(e1->phys_body)) {
                entity_pen_push(e2, &contact[i], pen);
            } else if (!phys_body_has_body(e2->phys_body)) {
                entity_pen_push(e1, &contact[i], pen);
            } else {
                if (e1->priv)
                    entity_pen_push(e1, &contact[i], pen);
                else if (e2->priv)
                    entity_pen_push(e2, &contact[i], pen);
            }
        }
    }
}

struct contact {
    dContact contact[MAX_CONTACTS];
    int nc;
};

static void got_contact(void *data, dGeomID o1, dGeomID o2)
{
    struct contact *c = data;

    if (c->nc >= array_size(c->contact))
        return;

    if (o1 == o2)
        return;

    if (dGeomIsSpace(o1) || dGeomIsSpace(o2))
        dSpaceCollide2(o1, o2, c, got_contact);
    else
        c->nc += dCollide(o1, o2, 1, &c->contact[c->nc].geom, sizeof(dContact));
}

static entity3d *
__phys_ray_cast(struct phys *phys, entity3d *e, const vec3 start, const vec3 dir,
                double *pdist, dContact *contact)
{
    entity3d *target = NULL, *ret = NULL;
    dGeomID ray = NULL;
    struct contact c = {};
    vec3 _start;
    bool check_self = !!e->phys_body;

    vec3_dup(_start, start);
    ray = dCreateRay(phys->space, *pdist);
    dGeomRaySetFirstContact(ray, 0);
    dGeomRaySetClosestHit(ray, 1);
    dGeomRaySetBackfaceCull(ray, 1);
    dGeomRaySet(ray, _start[0], _start[1], _start[2], dir[0], dir[1], dir[2]);
    phys_contact_surface(NULL, NULL, c.contact, array_size(c.contact));
    dSpaceCollide2(ray, (dGeomID)phys->space, &c, &got_contact);

    /* find the closest hit that's not self */
    int i, min_i = -1;
    dReal depth = dInfinity;
    dContactGeom *cgeom;
    for (i = 0; i < c.nc; i++) {
        cgeom = &c.contact[i].geom;
        if (entity_and_other_by_class(cgeom, dRayClass, NULL, &target)) {
            /* skip self intersections */
            if (check_self && e == target)
                continue;

            if (!entity3d_matches(e, ENTITY3D_ALIVE))
                continue;

            if (depth > cgeom->depth) {
                depth = cgeom->depth;
                min_i = i;
                ret   = target;
            }
        }
    }

    if (min_i < 0)
        goto out;

    cgeom = &c.contact[min_i].geom;
    if (contact)
        memcpy(contact, &c.contact[min_i], sizeof(*contact));

    *pdist = cgeom->depth;
out:
    dGeomDestroy(ray);

    return ret;
}

entity3d *phys_ray_cast2(struct phys *phys, entity3d *e, const vec3 start,
                         const vec3 dir, double *pdist)
{
    return __phys_ray_cast(phys, e, start, dir, pdist, NULL);
}

entity3d *phys_ray_cast(entity3d *e, const vec3 start, const vec3 dir, double *pdist)
{
    if (!e->phys_body)
        return NULL;

    return phys_ray_cast2(e->phys_body->phys, e, start, dir, pdist);
}

void phys_ground_entity(struct phys *phys, entity3d *e)
{
    entity3d *collision;
    const float *start = transform_pos(&e->xform, NULL);
    vec3 dir = { 0, -1, 0 };
    double dist = 1e6;

    if (e->phys_body)
        phys = e->phys_body->phys;
    collision = phys_ray_cast2(phys, e, start, dir, &dist);
    if (!collision)
        return;

    entity3d_move(e, (vec3){ 0, -dist, 0 });
}

bool phys_body_ground_collide(struct phys_body *body, bool grounded)
{
    entity3d *e = phys_body_entity(body);
    struct phys *phys = body->phys;
    dReal epsilon = 1e-3;
    dReal ray_len = body->yoffset - body->ray_off + epsilon;
    struct character *ch = e->priv;
    struct contact c = {};
    const dReal *pos;
    bool ret = false;

    if (!phys_body_has_body(body))
        return true;

    phys_contact_surface(NULL, NULL, c.contact, array_size(c.contact));
    ch->collision = NULL;

    /*
     * Check if our capsule intersects with anything
     */
    dSpaceCollide2(body->geom, (dGeomID)phys->ground_space, &c, got_contact);
    int i;
    for (i = 0; i < c.nc; i++) {
        dVector3 up = { 0, 1, 0 };
        dReal upness = dDot(c.contact[i].geom.normal, up, 3);

        entity_and_other_by_class(&c.contact[i].geom, dCapsuleClass,
                                  NULL, &ch->collision);
        /*
         * if the bottom of the capsule collides (almost) vertically,
         * our legs are under the ground, shouldn't happen, but if it
         * does, correct the height; if the angle with the normal is
         * greater than that, we ran into an obstacle, either way,
         * stop the body
         */
        if (upness > 0.95) {
            entity3d_move(e, (vec3){ 0, ray_len + c.contact->geom.depth, 0 });
            ret = true;
        }

        phys_body_stop(body);
        break;
    }

    pos = dGeomGetPosition(body->geom);

    /*
     * Cast a longer ray than the capsule offset to correct for the motion
     * resulting in character leaving the ground, which is the side effect
     * of the velocity vector pointing in the correct direction, but the
     * terrain being uneven and the character ending up airborne.
     *
     * ray_len * 2 might not be enough, but works well for the moment.
     */
    dContact contact;
    double dist = ray_len * 2;
    ch->collision = __phys_ray_cast(phys, e, (vec3){ pos[0], pos[1] - body->ray_off, pos[2] },
                                    (vec3){ 0, -1, 0 }, &dist, &contact);
    if (ch->collision)
        goto got_it;

    ch->collision = __phys_ray_cast(phys, e, (vec3){ pos[0] + body->radius, pos[1] - body->ray_off, pos[2] },
                                    (vec3){ 0, -1, 0 }, &dist, &contact);
    if (ch->collision)
        goto got_it;

    ch->collision = __phys_ray_cast(phys, e, (vec3){ pos[0] - body->radius, pos[1] - body->ray_off, pos[2] },
                                    (vec3){ 0, -1, 0 }, &dist, &contact);
    if (ch->collision)
        goto got_it;

    ch->collision = __phys_ray_cast(phys, e, (vec3){ pos[0], pos[1] - body->ray_off, pos[2] + body->radius },
                                    (vec3){ 0, -1, 0 }, &dist, &contact);
    if (ch->collision)
        goto got_it;

    ch->collision = __phys_ray_cast(phys, e, (vec3){ pos[0], pos[1] - body->ray_off, pos[2] - body->radius},
                                    (vec3){ 0, -1, 0 }, &dist, &contact);
    if (ch->collision)
        goto got_it;

    return ret;

got_it:
    if (grounded && (dist > ray_len)) {
        /*
         * correct for temporary raising above ground due to uneven
         * terrain / stairs: move down
         */
        goto move;
    } else if (dist < ray_len) {
        /*
         * correct for temporarily going below ground due to dynamics
         * being faster than the frame rate: move up
         */
        goto move;
    } else if (dist > ray_len) {
        /*
         * above ground, airborne, do nothing
         */
        return false;
    } else {
        /*
         * on the ground, stick, don't correct height
         */
        goto stick;
    }

move:
    entity3d_move(e, (vec3){ 0, ray_len - dist, 0 });
stick:
    phys_body_stick(body, &contact);

    return true;
}

static void __phys_step(struct phys *phys, double dt)
{
    struct phys_body *pb, *itpb;
    DECLARE_LIST(pen);

    dSpaceCollide2((dGeomID)phys->ground_space, (dGeomID)phys->character_space,
                   &pen, near_callback);
    dSpaceCollide(phys->character_space, &pen, near_callback);

    list_for_each_entry_iter(pb, itpb, &pen, pen_entry) {
        const dReal     *pos = dBodyGetPosition(pb->body);
        vec3            off = { pos[0], pos[1], pos[2] };

        if (pb->pen_depth > 0 && vec3_len(pb->pen_norm) > 0) {
            vec3_sub(off, off, pb->pen_norm);
            dBodySetPosition(pb->body, off[0], off[1], off[2]);
        }
        list_del(&pb->pen_entry);
        pb->pen_depth = 0;
        pb->pen_norm[0] = pb->pen_norm[1] = pb->pen_norm[2] = 0.0;
    }

    /* ODE manual warns against this */
    dWorldQuickStep(phys->world, dt);
    dJointGroupEmpty(phys->contact);
}

void phys_step(struct phys *phys, double dt)
{
    const dReal fixed_dt = 1.0 / 120.0;

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
    const dReal *pos;
    const dReal *vel;

    if (!e->phys_body || !phys_body_has_body(e->phys_body))
        return 0;

    pos = dGeomGetPosition(e->phys_body->geom);
    e->phys_body->updated++;
    entity3d_position(e, (vec3){ pos[0], pos[1] - e->phys_body->yoffset, pos[2] });
    vel = dBodyGetLinearVel(e->phys_body->body);
    if (!e->priv) {
        quat rot;
        phys_body_rotation(e->phys_body, rot);
        transform_set_quat(&e->xform, rot);
    }

    return dCalcVectorLength3(vel) > 1e-3 ? 1 : 0;
}

static dGeomID phys_geom_capsule_new(struct phys *phys, struct phys_body *body, entity3d *e,
                                     double mass, double geom_radius, double geom_offset)
{
    float r = 0.0, length = 0.0, off = 0.0, X, Y, Z;
    int direction;
    dGeomID g;

    X = entity3d_aabb_X(e);
    Y = entity3d_aabb_Y(e);
    Z = entity3d_aabb_Z(e);
    /*
     * capsule seems to be always be vertical (direction 2 -- Y positive),
     * so we need to use offset rotation for horizontal cases
     */
    direction = xmax3(X, Y, Z) + 1;
    switch (direction) {
        case 1:
            // direction = 2; /* XXX */
            // abort();
            // break;
        case 2:
            /*
             * Upright
             */
            r      = geom_radius ? geom_radius : min3(X, Y, Z) / 2;
            length = max(Y / 2 - r * 2, 0);
            off    = geom_offset ? geom_offset : Y / 2;
            body->ray_off = r + length / 2;
            break;
        case 3:
            direction = 3;

            /*
             * Puppy
             */
            r      = geom_radius ? geom_radius : X / 2;
            length = Z - r * 2;
            off    = geom_offset ? geom_offset : (Y - r * 2) / 2;
            body->ray_off = r;
            break;
    }

    /*
     * XXX: The above logic is busted: ray length ends up zero.
     */
    // dbg("CAPSULE('%s') dir=%d r=%f length=%f yoff=%f ray_off=%f ray_len=%f\n", entity_name(e), direction, r, length,
    //     off, body->ray_off, off - body->ray_off);

    if (length)
        CHECK(g = dCreateCapsule(phys->space, r, length));
    else
        CHECK(g = dCreateSphere(phys->space, r));
    //dGeomSetData(g, e);
    body->radius = r;

    body->yoffset = off;
    if (phys_body_has_body(body)) {
        dMassSetZero(&body->mass);
        if (length)
            dMassSetCapsuleTotal(&body->mass, mass, direction, r, length);
        else
            dMassSetSphereTotal(&body->mass, mass, r);
        dBodySetMass(body->body, &body->mass);
    }

    return g;
}

static dGeomID phys_geom_trimesh_new(struct phys *phys, struct phys_body *body,
                                     entity3d *e, double mass)
{
    dTriMeshDataID meshdata = dGeomTriMeshDataCreate();
    model3d *m = e->txmodel->model;
    unsigned short *idx = m->collision_idx;
    size_t idxsz = m->collision_idxsz;
    size_t vxsz = m->collision_vxsz;
    float *vx = m->collision_vx;
    dGeomID trimesh = NULL;
    dReal *tvx = NULL;
    dTriIndex *tidx;
    int i;

    idxsz /= sizeof(unsigned short);
    tidx = mem_alloc(sizeof(*tidx), .nr = idxsz, .fatal_fail = 1); /* XXX: refcounting, or tied to model? */
    for (i = 0; i < idxsz; i += 3) {
        /* swap i+1 and i+2 on either side to switch winding */
        tidx[i + 0] = idx[i + 0];
        tidx[i + 1] = idx[i + 1];
        tidx[i + 2] = idx[i + 2];
    }

    vxsz /= sizeof(float);
    tvx = mem_alloc(sizeof(*tvx), .nr = vxsz, .fatal_fail = 1);
    for (i = 0; i < vxsz; i += 3) {
        /* apply rotation and scale */
        mat4x4 trans;
        vec4 pos = { vx[i + 0], vx[i + 1], vx[i + 2], 1 };
        vec4 res;
        mat4x4_identity(trans);
        /* Not baking rotation into the mesh any more, using phys_body_rotate_mat4x4() instead */
        mat4x4_scale_aniso(trans, trans, e->scale, e->scale, e->scale);
        mat4x4_mul_vec4(res, trans, pos);
        tvx[i + 0] = res[0];
        tvx[i + 1] = res[1];
        tvx[i + 2] = res[2];
    }

#ifdef dDOUBLE
    dGeomTriMeshDataBuildDouble(meshdata, tvx, 3 * sizeof(dReal), vxsz / 3, tidx, idxsz, 3 * sizeof(dTriIndex));
#else
    dGeomTriMeshDataBuildSingle1(meshdata, tvx, 3 * sizeof(float), vxsz / 3, tidx, idxsz, 3 * sizeof(dTriIndex), NULL);
#endif
    //dGeomTriMeshDataBuildSimple(meshdata, tvx, vxsz, tidx, idxsz);
    dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_FACE_ANGLES), NULL);
    //dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_CONCAVE_EDGES), NULL);
    trimesh = dCreateTriMesh(phys->space, meshdata, NULL, NULL, NULL);
    if (!trimesh) {
        dGeomTriMeshDataDestroy(meshdata);
        mem_free(tvx);
        mem_free(tidx);
        return NULL;
    }

    body->trimesh_vx = tvx;
    body->trimesh_idx = tidx;

    if (phys_body_has_body(body)) {
        body->geom = trimesh;
        if (phys_body_has_body(body)) {
            dMassSetTrimeshTotal(&body->mass, mass, body->geom);
            dGeomSetPosition(body->geom, -body->mass.c[0], -body->mass.c[1], -body->mass.c[2]);
            dMassTranslate(&body->mass, -body->mass.c[0], -body->mass.c[1], -body->mass.c[2]);
            dBodySetMass(body->body, &body->mass);
        }
    }

    return trimesh;
}

struct phys_body *phys_body_new(struct phys *phys, entity3d *entity, geom_class class,
                                double geom_radius, double geom_offset, phys_type type, double mass)
{
    bool has_body = type == PHYS_BODY;
    struct phys_body *body;
    dMatrix3 rot;
    dMass m;

    body = mem_alloc(sizeof(*body), .zero = 1, .fatal_fail = 1);
    list_init(&body->pen_entry);
    body->phys = phys;

    if (has_body)
        body->body = dBodyCreate(phys->world);

    if (class == GEOM_TRIMESH) {
        body->geom = phys_geom_trimesh_new(phys, body, entity, mass);
    } else if (class == GEOM_SPHERE) {
        dMassSetZero(&m);
        dMassSetSphereTotal(&m, mass, 0.1);
    } else if (class == GEOM_CAPSULE) {
        body->geom = phys_geom_capsule_new(phys, body, entity, mass, geom_radius * entity->scale,
                                           geom_offset * entity->scale);
    }

    if (!body->geom) {
        if (has_body)
            dBodyDestroy(body->body);
        mem_free(body);

        return NULL;
    }

    body->class = class;

    dRSetIdentity(rot);
    phys_body_set_position(body, transform_pos(&entity->xform, NULL));
    if (has_body) {
        dBodySetRotation(body->body, rot);
        dGeomSetBody(body->geom, body->body);
        dBodySetData(body->body, entity);
        if (class == GEOM_CAPSULE) {
            // Capsule geometry assumes that Z goes upwards,
            // so cylinder's axis is parallel to Z axis.
            // We need to rotate the local geometry so that
            // cylinder's axis is parallel to Y axis.
            dMatrix3 R;
            dRFromAxisAndAngle(R, 1.0, 1.0, 1.0, -M_PI * 2.0 / 3.0);
            dGeomSetOffsetRotation(body->geom, R);
        }
        dSpaceRemove(phys->space, body->geom);
        dSpaceAdd(phys->character_space, body->geom);
    } else {
        if (class == GEOM_CAPSULE) {
            // A similar orientation fix is needed for geometries
            // that don't have a body; in this case,
            // we can just set its rotation, since it is not
            // going to change.
            dRFromAxisAndAngle(rot, 1.0, 1.0, 1.0, -M_PI * 2.0 / 3.0);
        }

        dGeomSetRotation(body->geom, rot);
        dSpaceRemove(phys->space, body->geom);
        dSpaceAdd(phys->ground_space, body->geom);
    }
    dGeomSetData(body->geom, entity);

    if (has_body) {
        /* XXX: not all bodies need the motor */
        body->lmotor = dJointCreateLMotor(phys->world, 0);
        dJointSetLMotorNumAxes(body->lmotor, 3);
        dJointSetLMotorAxis(body->lmotor, 0, 0, 1, 0, 0);
        dJointSetLMotorAxis(body->lmotor, 1, 0, 0, 1, 0);
        dJointSetLMotorAxis(body->lmotor, 2, 0, 0, 0, 1);

        /* lmotor's force constraint: fmax = mass * accel */
        dReal fmax = body->mass.mass * /* max speed */10.0 / /* acceleration time*/0.1;
        dJointSetLMotorParam(body->lmotor, dParamFMax1, fmax);
        dJointSetLMotorParam(body->lmotor, dParamFMax2, fmax);
        dJointSetLMotorParam(body->lmotor, dParamFMax3, fmax);
        phys_body_attach_motor(body, true);
    }

    return body;
}

void phys_body_done(struct phys_body *body)
{
    dTriMeshDataID meshdata = NULL;
    if (body->class == GEOM_TRIMESH) {
        meshdata = dGeomTriMeshGetTriMeshDataID(body->geom);
        mem_free(body->trimesh_vx);
        mem_free(body->trimesh_idx);
    }
    if (body->geom)
        dGeomDestroy(body->geom);
    if (meshdata)
        dGeomTriMeshDataDestroy(meshdata);
    if (body->body)
        dBodyDestroy(body->body);
    body->geom = NULL;
    body->body = NULL;
    mem_free(body);
}

static void ode_error(int errnum, const char *msg, va_list ap)
{
    vlogg(ERR, "ODE", -1, "", msg, ap);
    vlogg(ERR, "ODE", -1, "\n", msg, ap);
}

static void ode_debug(int errnum, const char *msg, va_list ap)
{
    vlogg(DBG, "ODE", -1, "", msg, ap);
    vlogg(DBG, "ODE", -1, "\n", msg, ap);
    abort();
}

static void ode_message(int errnum, const char *msg, va_list ap)
{
    vlogg(NORMAL, "ODE", -1, "\n", msg, ap);
}

void phys_set_ground_contact(struct phys *phys, ground_contact_fn ground_contact)
{
    phys->ground_contact = ground_contact;
}

static void *ode_alloc(dsizeint size)
{
    return mem_alloc(size);
}

static void *ode_realloc(void *ptr, dsizeint oldsize, dsizeint newsize)
{
    return mem_realloc(ptr, newsize, .old_size = oldsize);
}

static void ode_free(void *ptr, dsizeint size)
{
    mem_free(ptr, .size = size);
}

struct phys *phys_init(struct clap_context *ctx)
{
    struct phys *phys;

    phys = mem_alloc(sizeof(*phys));
    if (!phys)
        return NULL;

    phys->clap_ctx = ctx;
    dInitODE2(0);
    dSetAllocHandler(ode_alloc);
    dSetReallocHandler(ode_realloc);
    dSetFreeHandler(ode_free);
    dSetErrorHandler(ode_error);
    dSetDebugHandler(ode_debug);
    dSetMessageHandler(ode_message);
    phys->world = dWorldCreate();
    phys->space = dHashSpaceCreate(0);
    phys->collision = dHashSpaceCreate(phys->space);
    phys->character_space = dHashSpaceCreate(phys->space);
    phys->ground_space = dHashSpaceCreate(phys->space);
    phys->contact = dJointGroupCreate(0);
    phys->time_acc = 0.0;
    phys->draw_capsules = false;
    phys->draw_contacts = false;
    phys->draw_velocities = false;
    dWorldSetGravity(phys->world, 0, -9.8, 0);
    // dWorldSetCFM(phys->world, 1e-5);
    // dWorldSetERP(phys->world, 0.8);
    //dWorldSetContactSurfaceLayer(phys->world, 0.001);
    dWorldSetLinearDamping(phys->world, 0.001);

    return phys;
}

void phys_done(struct phys *phys)
{
    dSpaceDestroy(phys->ground_space);
    dSpaceDestroy(phys->character_space);
    dSpaceDestroy(phys->collision);
    dSpaceDestroy(phys->space);
    dWorldDestroy(phys->world);
    dJointGroupDestroy(phys->contact);
    dCloseODE();
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

    const dReal *vel = dBodyGetLinearVel(body->body);
    if (likely(dLENGTHSQUARED(vel) < 1e-3))     return;

    dVector3 dvel = { vel[0], vel[1], vel[2] };
    dNormalize3(dvel);

    const dReal *spos = dBodyGetPosition(body->body);
    dVector3 dpos;

    dAddVectors3(dpos, spos, dvel);

    message_send(body->phys->clap_ctx, &(struct message) {
        .type           = MT_DEBUG_DRAW,
        .debug_draw     = (struct message_debug_draw) {
            .color      = { 0.0, 1.0, 0.0, 1.0 },
            .thickness  = 2.0,
            .shape      = DEBUG_DRAW_LINE,
            .v0         = { spos[0], spos[1], spos[2] },
            .v1         = { dpos[0], dpos[1], dpos[2] },
        }
    });
}

void phys_debug_draw(struct scene *scene, struct phys_body *body)
{
    phys_debug_draw_velocity(body);

    if (likely(!body->phys->draw_capsules))
        return;

    const dReal *pos = dGeomGetPosition(body->geom);
    dReal r, len = 0;
    int class = dGeomGetClass(body->geom);

    if (class == dCapsuleClass)
        dGeomCapsuleGetParams(body->geom, &r, &len);
    else if (class == dSphereClass)
        r = dGeomSphereGetRadius(body->geom);
    else
        return;

    struct message dm   = {
        .type           = MT_DEBUG_DRAW,
        .debug_draw     = (struct message_debug_draw){
            .color      = { 1.0, 0.0, 0.0, 1.0 },
            .thickness  = 4.0,
            .shape      = DEBUG_DRAW_LINE,
        }
    };
    vec3_dup(dm.debug_draw.v0, (vec3){ pos[0] - r, pos[1]  - len / 2 - r, pos[2] - r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos[0] + r, pos[1]  + len / 2 + r, pos[2] + r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos[0] + r, pos[1] - len / 2 - r, pos[2] + r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos[0] - r, pos[1] + len / 2 + r, pos[2] - r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos[0] - r, pos[1] - len / 2 - r, pos[2] + r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos[0] + r, pos[1] + len / 2 + r, pos[2] - r });
    message_send(body->phys->clap_ctx, &dm);

    vec3_dup(dm.debug_draw.v0, (vec3){ pos[0] + r, pos[1] - len / 2 - r, pos[2] - r });
    vec3_dup(dm.debug_draw.v1, (vec3){ pos[0] - r, pos[1] + len / 2 + r, pos[2] + r });
    message_send(body->phys->clap_ctx, &dm);
}
