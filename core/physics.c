// SPDX-License-Identifier: Apache-2.0
#include <ode/ode.h>
#include "common.h"
#include "character.h"
#include "linmath.h"
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

struct entity3d *phys_body_entity(struct phys_body *body)
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
    dQuaternion _rot;

    dGeomGetQuaternion(body->geom, _rot);

    rot[0] = _rot[0];
    rot[1] = _rot[1];
    rot[2] = _rot[2];
    rot[3] = _rot[3];
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
static bool entity_and_other_by_class(dContactGeom *geom, int class, struct entity3d **match,
                                      struct entity3d **other)
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

void phys_body_set_position(struct phys_body *body, vec3 pos)
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

    struct entity3d *e = phys_body_entity(body);
    if (e && e->priv) {
        struct character *c = e->priv;
        if (!vec3_len(vel))
            c->stopped = true;
        else
            c->stopped = false;
    }
}

void phys_body_stop(struct phys_body *body)
{
    if (!phys_body_has_body(body))
        return;

    phys_body_set_motor_velocity(body, true, (vec3){ 0, 0, 0 });
    dBodySetMaxAngularSpeed(body->body, 0);
    dBodySetLinearDampingThreshold(body->body, 0.001);
}

static void phys_body_stick(struct phys_body *body, dContact *contact)
{
    struct entity3d *e = phys_body_entity(body);
    struct phys *phys = body->phys;
    struct character *c = e->priv;
    dJointID j;

    /* should also warn */
    if (!phys_body_has_body(body))
        return;

    if (c) {
        c->stuck = true;
        c->normal[0] = contact->geom.normal[0];
        c->normal[1] = contact->geom.normal[1];
        c->normal[2] = contact->geom.normal[2];
    }

    j = dJointCreateContact(phys->world, phys->contact, contact);
    dJointAttach(j, body->body, NULL);
}

static void phys_contact_surface(struct entity3d *e1, struct entity3d *e2, dContact *contact, int nc)
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
static unused const char *class_str(int class)
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
static void entity_pen_push(struct entity3d *e, dContact *contact, struct list *pen)
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
            struct entity3d *e1 = dGeomGetData(g1);
            struct entity3d *e2 = dGeomGetData(g2);
            struct phys *phys = e1->phys_body->phys;

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

struct entity3d *phys_ray_cast2(struct phys *phys, struct entity3d *e, vec3 start,
                                vec3 dir, double *pdist)
{
    struct entity3d *target = NULL;
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
    for (i = 0; i < c.nc; i++) {
        if (entity_and_other_by_class(&c.contact[i].geom, dRayClass, NULL, &target)) {
            /* skip self intersections */
            if (check_self && e == target)
                continue;

            if (depth > c.contact[i].geom.depth) {
                depth = c.contact[i].geom.depth;
                min_i = i;
            }
        }
    }

    if (min_i < 0) {
        target = NULL;
        goto out;
    }

    *pdist = c.contact[min_i].geom.depth;
out:
    dGeomDestroy(ray);

    return target;
}

struct entity3d *phys_ray_cast(struct entity3d *e, vec3 start, vec3 dir, double *pdist)
{
    if (!e->phys_body)
        return NULL;

    return phys_ray_cast2(e->phys_body->phys, e, start, dir, pdist);
}

void phys_ground_entity(struct phys *phys, struct entity3d *e)
{
    struct entity3d *collision;
    float *start = e->pos;
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
    struct entity3d *e = phys_body_entity(body);
    struct phys *phys = body->phys;
    dReal epsilon = 1e-3;
    dReal ray_len = body->yoffset - body->ray_off + epsilon;
    dVector3 dir = { 0, -ray_len, 0 };
    struct character *ch = e->priv;
    struct contact c = {};
    dGeomID ray;
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
        if (upness > 0.95)
            entity3d_move(e, (vec3){ 0, ray_len + c.contact->geom.depth, 0 });

        phys_body_stop(body);
        ret = true;
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
    ray = dCreateRay(phys->space, ray_len * 2);
    dGeomRaySetBackfaceCull(ray, 1);
    dGeomRaySetClosestHit(ray, 1);
    dGeomRaySetFirstContact(ray, 0);
    dGeomRaySet(ray, pos[0], pos[1] - body->ray_off, pos[2], dir[0], dir[1], dir[2]);
    phys_contact_surface(NULL, NULL, c.contact, array_size(c.contact));
    c.nc = 0;
    dSpaceCollide2(ray, (dGeomID)phys->ground_space, &c, got_contact);
    if (c.nc && !ch->collision)
        entity_and_other_by_class(&c.contact[0].geom, dRayClass, NULL, &ch->collision);

    dGeomDestroy(ray);
    if (c.nc)
        goto got_it;

    if (!ret)
        ch->stuck = false;

    return ret;

got_it:
    if (grounded && (c.contact[0].geom.depth > ray_len))
        goto stick;
    else if (ray_len - c.contact[0].geom.depth > epsilon)
        goto stick;
    else if (ray_len < c.contact[0].geom.depth)
        return false;

stick:
    entity3d_move(e, (vec3){ 0, ray_len - c.contact[0].geom.depth, 0 });
    phys_body_stick(body, &c.contact[0]);

    return true;
}

void phys_step(struct phys *phys, unsigned long frame_count)
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

    /* XXX: quick step fails in quickstep.cpp:3267 */
    dWorldQuickStep(phys->world, 0.01 * frame_count);
    dJointGroupEmpty(phys->contact);
}

int phys_body_update(struct entity3d *e)
{
    const dReal *pos;
    const dReal *vel;

    if (!e->phys_body || !phys_body_has_body(e->phys_body))
        return 0;

    pos = dGeomGetPosition(e->phys_body->geom);
    e->phys_body->updated++;
    entity3d_position(e, (vec3){ pos[0], pos[1] - e->phys_body->yoffset, pos[2] });
    vel = dBodyGetLinearVel(e->phys_body->body);

    return dCalcVectorLength3(vel) > 1e-3 ? 1 : 0;
}

static dGeomID phys_geom_capsule_new(struct phys *phys, struct phys_body *body, struct entity3d *e,
                                     double mass, double geom_radius, double geom_offset)
{
    float r = 0.0, length = 0.0, off = 0.0, X, Y, Z;
    int direction;
    dGeomID g;
    dMass m;

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

    body->yoffset = off;
    if (phys_body_has_body(body)) {
        dMassSetZero(&body->mass);
        if (length)
            dMassSetCapsuleTotal(&m, mass, direction, r, length);
        else
            dMassSetSphereTotal(&m, mass, r);
        dBodySetMass(body->body, &m);
    }

    return g;
}

static dGeomID phys_geom_trimesh_new(struct phys *phys, struct phys_body *body,
                                     struct entity3d *e, double mass)
{
    dTriMeshDataID meshdata = dGeomTriMeshDataCreate();
    struct model3d *m = e->txmodel->model;
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
        mat4x4_rotate_X(trans, trans, e->rx);
        mat4x4_rotate_Y(trans, trans, e->ry);
        mat4x4_rotate_Z(trans, trans, e->rz);
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

    /*
     * XXX: terrain.c corner case, calls here directly with body==NULL
     * XXX: make it create PHYS_GEOM "body" instead.
     */
    if (body) {
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

struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, geom_class class,
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
    phys_body_set_position(body, entity->pos);
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
        body->lmotor = dJointCreateLMotor(phys->world, 0);
        dJointSetLMotorNumAxes(body->lmotor, 3);
        dJointSetLMotorAxis(body->lmotor, 0, 0, 1, 0, 0);
        dJointSetLMotorAxis(body->lmotor, 1, 0, 0, 1, 0);
        dJointSetLMotorAxis(body->lmotor, 2, 0, 0, 0, 1);
        dJointSetLMotorParam(body->lmotor, dParamFMax1, 50);
        dJointSetLMotorParam(body->lmotor, dParamFMax2, 5);
        dJointSetLMotorParam(body->lmotor, dParamFMax3, 50);
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

struct phys *phys_init(void)
{
    struct phys *phys;

    phys = mem_alloc(sizeof(*phys));
    if (!phys)
        return NULL;

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
    dWorldSetGravity(phys->world, 0, -9.8, 0);
    dWorldSetMaxAngularSpeed(phys->world, 0);
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

void phys_rotation_to_mat4x4(const dReal *rot, const dReal *pos, mat4x4 *m)
{
    float *mx = (float *)*m;
    mx[0] = rot[0];
    mx[1] = rot[1];
    mx[2] = rot[2];
    mx[3] = 0;
    mx[4] = rot[4];
    mx[5] = rot[5];
    mx[6] = rot[6];
    mx[7] = 0;
    mx[8] = rot[8];
    mx[9] = rot[9];
    mx[10] = rot[10];
    mx[11] = 0;
    mx[12] = pos ? pos[0] : 0;
    mx[13] = pos ? pos[1] : 0;
    mx[14] = pos ? pos[2] : 0;
    mx[15] = 1;
}

void phys_debug_draw(struct scene *scene, struct phys_body *body)
{
    vec3 start, end;
    const dReal *pos = dGeomGetPosition(body->geom);
    dReal r, len = 0;
    const dReal *rot;
    mat4x4 _rot, _rot_tmp;
    int class = dGeomGetClass(body->geom);

    if (class == dCapsuleClass)
        dGeomCapsuleGetParams(body->geom, &r, &len);
    else if (class == dSphereClass)
        r = dGeomSphereGetRadius(body->geom);
    else
        return;

    rot = dGeomGetRotation(body->geom);
    phys_rotation_to_mat4x4(rot, NULL, &_rot_tmp);
    mat4x4_invert(_rot, _rot_tmp);
    _rot[3][0] = pos[0];
    _rot[3][1] = pos[1];
    _rot[3][2] = pos[2];

    start[0] = -r;
    start[1] = -r;
    start[2] = -len / 2 - r;
    end[0] = r;
    end[1] = r;
    end[2] = len / 2 + r;
    debug_draw_line(scene, start, end, &_rot);
    start[0] = r;
    start[1] = r;
    end[0] = -r;
    end[1] = -r;
    debug_draw_line(scene, start, end, &_rot);
    start[0] = -r;
    start[1] = r;
    end[0] = r;
    end[1] = -r;
    debug_draw_line(scene, start, end, &_rot);
    start[0] = r;
    start[1] = -r;
    end[0] = -r;
    end[1] = r;
    debug_draw_line(scene, start, end, &_rot);
    start[0] = r;
    start[1] = -r;
    end[0] = -r;
    end[1] = r;
    debug_draw_line(scene, start, end, &_rot);
}
