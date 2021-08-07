#include <ode/ode.h>
#include "common.h"
#include "character.h"
#include "linmath.h"
#include "model.h"
#include "physics.h"

static struct phys _phys;
struct phys *phys = &_phys; /* XXX */
static DECLARE_LIST(phys_bodies);

struct entity3d *phys_body_entity(struct phys_body *body)
{
    return dGeomGetData(body->geom);
}

const dReal *phys_body_position(struct phys_body *body)
{
    if (phys_body_has_body(body))
        return dBodyGetPosition(body->body);
    return dGeomGetPosition(body->geom);
}

const dReal *phys_body_rotation(struct phys_body *body)
{
    if (phys_body_has_body(body))
        return dBodyGetRotation(body->body);
    return dGeomGetRotation(body->geom);
}

/*
 * Separate thread? Emscripten won't be happy.
 */
#define MAX_CONTACTS 16

static bool geom_and_other_by_class(dGeomID o1, dGeomID o2, int class, dGeomID *match, dGeomID *other)
{
    *match = *other = NULL;

    if (dGeomGetClass(o1) == class) {
        *match = o1;
        *other = o2;
        return true;
    } else if (dGeomGetClass(o2) == class) {
        *match = o2;
        *other = o1;
        return true;
    }

    return false;
}

static bool entity_and_other_by_class(dGeomID o1, dGeomID o2, int class, struct entity3d **match,
                                      struct entity3d **other)
{
    dGeomID _match, _other;

    *match = *other = NULL;
    if (geom_and_other_by_class(o1, o2, class, &_match, &_other)) {
        *match = dGeomGetData(_match);
        *other = dGeomGetData(_other);
        return true;
    }

    return false;
}

void phys_body_stick(struct phys_body *body, dContact *contact)
{
    struct entity3d *e = phys_body_entity(body);
    struct character *c = e->priv;
    dMatrix3 R;
    dJointID j;

    /* should also warn */
    if (!phys_body_has_body(body))
        return;

    if (c) {
        c->normal[0] = contact->geom.normal[0];
        c->normal[1] = contact->geom.normal[1];
        c->normal[2] = contact->geom.normal[2];
    }
    /* XXX: can't set rotation here if called from near_callback */
    // dRSetIdentity(R);
    // dRFromEulerAngles(R, e->rx, e->ry, e->rz);
    // dBodySetRotation(e->phys_body->body, R);

    j = dJointCreateContact(phys->world, phys->contact, contact);
    dJointAttach(j, body->body, NULL);

    if (dJointGetBody(body->lmotor, 0))
        return;
    // if (body->lmotor)
    //     dJointDestroy(body->lmotor);
    // body->lmotor = dJointCreateContact(phys->world, phys->contact, contact);
    dJointAttach(body->lmotor, body->body, NULL);
    dJointSetLMotorParam(body->lmotor, dParamVel1, 0);
    dJointSetLMotorParam(body->lmotor, dParamVel2, 0);
    dJointSetLMotorParam(body->lmotor, dParamVel3, 0);
}

static void phys_contact_surface(struct entity3d *e1, struct entity3d *e2, dContact *contact, int nc)
{
    int i;

    for (i = 0; i < nc; i++) {
        contact[i].surface.mode = dContactBounce | dContactSoftCFM | dContactSoftERP;
        contact[i].surface.mu = /*bounce != 0 ? dInfinity : */50;
        contact[i].surface.mu2 = 0;
        contact[i].surface.bounce = 0.01;
        contact[i].surface.bounce_vel = 10.0;
        contact[i].surface.soft_cfm = 0.01;
        contact[i].surface.soft_erp = 1e-3;
    }
}

static const char *class_str(int class)
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

static void near_callback(void *data, dGeomID o1, dGeomID o2)
{
    bool ground = o1 == phys->ground || o2 == phys->ground;
    dContact contact[MAX_CONTACTS];
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);
    dReal bounce = 0.0, bounce_vel = 0.0;
    struct list *pen = data;
    dJointID j;
    int i, nc;

    //if (b1 && b2 && dAreConnectedExcluding(b1, b2, dJointTypeContact))
    //    return;

    /*if (e1 && e1->phys_body && e1->phys_body->bounce) {
        bounce = e1->phys_body->bounce;
        bounce_vel = e1->phys_body->bounce_vel;
    }

    if (e2 && e2->phys_body && e2->phys_body->bounce) {
        bounce = e2->phys_body->bounce;
        bounce_vel = e2->phys_body->bounce_vel;
    }*/
    //bounce = 0, bounce_vel = 0;

    phys_contact_surface(NULL, NULL, contact, MAX_CONTACTS);
    // for (i = 0; i < MAX_CONTACTS; i++) {
    //     contact[i].surface.mode = dContactBounce | dContactSoftCFM | dContactSoftERP;
    //     contact[i].surface.mu = /*bounce != 0 ? dInfinity : */50;
    //     contact[i].surface.mu2 = 0;
    //     contact[i].surface.bounce = 0.01;
    //     contact[i].surface.bounce_vel = 10.0;
    //     contact[i].surface.soft_cfm = 0.01;
    //     contact[i].surface.soft_erp = 1e-3;
    // }

    nc = dCollide(o1, o2, MAX_CONTACTS, &contact[0].geom, sizeof(dContact));
    if (nc) {
        for (i = 0; i < nc; i++) {
            dGeomID g1 = contact[i].geom.g1;
            dGeomID g2 = contact[i].geom.g2;
            struct entity3d *e1 = dGeomGetData(g1);
            struct entity3d *e2 = dGeomGetData(g2);
            struct entity3d *e_ground, *e_ray, *e_other;

            if (entity_and_other_by_class(g1, g2, dRayClass, &e_ray, &e_other)) {
                dbg_once("RAY collision of '%s'/'%s' at depth %f\n",
                         entity_name(e1), entity_name(e2), contact[i].geom.depth);
            }
            b1 = dGeomGetBody(g1);
            b2 = dGeomGetBody(g2);
            phys_body_update(e1);
            phys_body_update(e2);
            dbg_once("collision %s '%s', %s '%s' at %f,%f,%f\n", class_str(dGeomGetClass(g1)), e1 ? entity_name(e1) : "",
                class_str(dGeomGetClass(g2)), e2 ? entity_name(e2) : "",
                contact[i].geom.pos[0], contact[i].geom.pos[1], contact[i].geom.pos[2]);
            j = dJointCreateContact(phys->world, phys->contact, &contact[i]);
            dJointAttach(j, b1, b2);
            if (!phys_body_has_body(e1->phys_body)) {
                e_ground = e1;
                e_other = e2;
                ground = true;
            } else if (!phys_body_has_body(e2->phys_body)) {
                e_ground = e2;
                e_other = e1;
                ground  = true;
            } else {
                ground   = false;
            }

            if (ground) {
                const char *name = entity_name(e_other);
                vec3 norm = { contact[i].geom.normal[0], contact[i].geom.normal[1], contact[i].geom.normal[2] };

                if (!e_other->phys_body)
                    return;

                //dbg("ground collision: %s\n", name);

                // dbg("ground collision of '%s' at %f,%f (+ %f),%f\n", name,
                //     e_other->dx, e_other->dy, e_other->phys_body->yoffset, e_other->dz);
                // if (strstr(name, "tree") && dBodyIsEnabled(b)) {
                //     phys_body_update(e_other);
                //     //entity3d_move(e_other, 0, -0.2, 0);
                //     dBodyDisable(b);
                //     phys->ground_contact(e_other, e_other->dx, e_other->dy + e_other->phys_body->yoffset, e_other->dz);
                // } else if (!strcmp(name, "puppy")) {
                //     struct character *ch = e_other->priv;
                //     //dBodyAddRelForce(b, 0, 0, 10.0);
                //     ch->moved++;
                // }
                //dBodyDisable(b);
                phys_body_stick(e_other->phys_body, &contact[i]);
                //entity3d_stick(e_other, &contact[i]);
                e_other->phys_body->pen_depth += contact[i].geom.depth;
                vec3_scale(norm, norm, contact[i].geom.depth);
                vec3_add(e_other->phys_body->pen_norm, e_other->phys_body->pen_norm, norm);
                // e_other->phys_body->pen_norm[0] = contact[i].geom.normal[0];
                // e_other->phys_body->pen_norm[1] = contact[i].geom.normal[1];
                // e_other->phys_body->pen_norm[2] = contact[i].geom.normal[2];
                list_del(&e_other->phys_body->pen_entry);
                list_append(pen, &e_other->phys_body->pen_entry);
            }
        }
    }
}

bool phys_body_is_grounded(struct phys_body *body)
{
    struct entity3d *e = phys_body_entity(body);
    dVector3 dir = { 0, -1, 0 };
    dContact contact;
    dGeomID ray;
    dReal *pos;
    int nc;

    if (!phys_body_has_body(body))
        return true;

    phys_contact_surface(e, dGeomGetData(phys->ground), &contact, 1);
    nc = dCollide(body->geom, phys->ground, 1, &contact.geom, sizeof(dContact));
    if (nc)
        return true;

    pos = phys_body_position(body);
    ray = dCreateRay(phys->space, body->yoffset - body->ray_off);
    dGeomRaySet(ray, pos[0], pos[1] - body->ray_off, pos[2], dir[0], dir[1], dir[2]);
    //dGeomSetBody(ray, body->body);
    phys_contact_surface(e, dGeomGetData(phys->ground), &contact, 1);
    nc = dCollide(ray, phys->ground, 1, &contact.geom, sizeof(dContact));
    dGeomDestroy(ray);

    return !!nc;
}

bool phys_body_ground_collide(struct phys_body *body)
{
    struct entity3d *e = phys_body_entity(body);
    dReal ray_len = body->yoffset - body->ray_off;
    dVector3 dir = { 0, -1, 0 };
    //struct character *ch = e->priv;
    dContact contact;
    dGeomID ray;
    dReal *pos;
    int nc;

    if (!phys_body_has_body(body))
        return true;

    /* XXX */
    contact.surface.mode = dContactBounce | dContactSoftCFM | dContactSoftERP;
    contact.surface.mu = /*bounce != 0 ? dInfinity : */dInfinity;
    contact.surface.mu2 = 0;
    contact.surface.bounce = 0.01;
    contact.surface.bounce_vel = 10.0;
    contact.surface.soft_cfm = 0.01;
    contact.surface.soft_erp = 1e-3;

    /*
     * XXX: phys->ground, actually, is one mesh, but we may have multiple geoms
     *   1: maybe phys->ground should be a space instead? phys->collisions?
     *      then, we can use dSpaceCollide()
     */
    nc = dCollide(body->geom, phys->ground, 1, &contact.geom, sizeof(dContact));
    if (nc) {
        //dbg("body '%s' penetrates ground\n", entity_name(e));
        entity3d_move(e, 0, /*body->yoffset + */contact.geom.depth, 0);
        phys_body_stick(body, &contact);
        //return true;
    }

    pos = phys_body_position(body);
    ray = dCreateRay(phys->space, ray_len);
    dGeomRaySet(ray, pos[0], pos[1] - body->ray_off, pos[2], dir[0], dir[1], dir[2]);
    //dGeomSetBody(ray, body->body);
    nc = dCollide(ray, phys->ground, 1, &contact.geom, sizeof(dContact));
    dGeomDestroy(ray);
    if (!nc)
        return false;

    
    if (ray_len - contact.geom.depth > 1e-5) {
        // dbg("RAY '%s' collides with %s at %f/%f normal %f,%f,%f\n", entity_name(e), class_str(dGeomGetClass(contact.geom.g2)),
        //     contact.geom.depth, ray_len,
        //     contact.geom.normal[0], contact.geom.normal[1], contact.geom.normal[2]);
        entity3d_move(e, 0, ray_len - contact.geom.depth, 0);
    }
    phys_body_stick(e->phys_body, &contact);

    return true;
}

void phys_step(void)
{
    struct phys_body *pb, *itpb;
    DECLARE_LIST(pen);

    /* this is instead called in character update */
    // list_for_each_entry(pb, &phys_bodies, entry) {
    //     if (phys_body_ground_collide(pb)) {
    //     }
    // }

    dSpaceCollide(phys->space, &pen, near_callback);

    list_for_each_entry_iter(pb, itpb, &pen, pen_entry) {
        const dReal     *pos = phys_body_position(pb);
        struct entity3d *e = phys_body_entity(pb);
        struct character *c  = e->priv;
        vec3             off = { pos[0], pos[1], pos[2] };
        dMatrix3 R;

        /*if (pb->lmotor) {
            dJointDestroy(pb->lmotor);
            pb->lmotor = NULL;
        }*/

        /* XXX */
        if (c) {
            c->ragdoll = 0;
        }
        //dbg("clearing velocities from '%s'\n", entity_name(e));
        // dBodySetLinearVel(pb->body, 0, 0, 0);
        // dBodySetAngularVel(pb->body, 0, 0, 0);
        dRSetIdentity(R);
        dRFromEulerAngles(R, e->rx, e->ry, e->rz);
        if (phys_body_has_body(pb))
            dBodySetRotation(pb->body, R);
        else
            dGeomSetRotation(pb->geom, R);
        // dBodyDisable(pb->body);
        /*if (pb->pen_depth > 0.5 && vec3_len(pb->pen_norm) > 0) {
            //dbg("moving '%s' by %f,%f,%f\n", entity_name(e), pb->pen_norm[0], pb->pen_norm[1], pb->pen_norm[2]);
            vec3_sub(off, off, pb->pen_norm);
            dBodySetPosition(pb->body, off[0], off[1], off[2]);
        }*/
        list_del(&pb->pen_entry);
        pb->pen_depth = 0;
        pb->pen_norm[0] = pb->pen_norm[1] = pb->pen_norm[2] = 0.0;
    }

    dWorldQuickStep(phys->world, 0.01);
    dJointGroupEmpty(phys->contact);
}

static void dGetEulerAngleFromRot(const dMatrix3 mRot, dReal *rX, dReal *rY, dReal *rZ)
{
    *rY = asin(mRot[0 * 4 + 2]);
    if (*rY < M_PI / 2) {
        if (*rY > -M_PI / 2) {
            *rX = atan2(-mRot[1 * 4 + 2], mRot[2 * 4 + 2]);
            *rZ = atan2(-mRot[0 * 4 + 1], mRot[0 * 4 + 0]);
        } else {
            // not unique
            *rX = -atan2(mRot[1 * 4 + 0], mRot[1 * 4 + 1]);
            *rZ = REAL(0.0);
        }
    } else {
        // not unique
        *rX = atan2(mRot[1 * 4 + 0], mRot[1 * 4 + 1]);
        *rZ = REAL(0.0);
    }
}

int phys_body_update(struct entity3d *e)
{
    const dReal *pos;
    const dReal *rot;
    const dReal *vel;
    dReal       rx, ry, rz;

    if (!e->phys_body || !phys_body_has_body(e->phys_body))
        return 0;

    pos = phys_body_position(e->phys_body);
    e->dx = pos[0];
    e->dy = pos[1] - e->phys_body->yoffset;
    e->dz = pos[2];
    vel = dBodyGetLinearVel(e->phys_body->body);

    return dCalcVectorLength3(vel) > 0.02 ? 1 : 0;
}

dGeomID phys_geom_capsule_new(struct phys *phys, struct phys_body *body, struct entity3d *e, double mass)
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
            break;
        case 2:
            /*
             * Upright
             */
            r      = min3(X, Y, Z) / 2;
            length = max(Y / 2 - r * 2, 0);
            off    = (Y - length) / 2;
            body->ray_off = r + length / 2;
            break;
        case 3:
            direction = 3;

            /*
             * Puppy
             */
            r      = X / 2;
            length = Z - r * 2;
            off    = (Y - r * 2) / 2;
            body->ray_off = r;
            break;
    }
    
    /*
     * XXX: The above logic is busted: ray length ends up zero.
     */
    dbg("CAPSULE('%s') dir=%d r=%f length=%f yoff=%f ray_off=%f ray_len=%f\n", entity_name(e), direction, r, length,
        off, body->ray_off, off - body->ray_off);

    if (length)
        CHECK(g = dCreateCapsule(phys->space, r, length));
    else
        CHECK(g = dCreateSphere(phys->space, r));
    //dGeomSetData(g, e);

    body->geom = g;
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

dGeomID phys_geom_trimesh_new(struct phys *phys, struct phys_body *body, struct entity3d *e, double mass)
{
    dTriMeshDataID meshdata = dGeomTriMeshDataCreate();
    unsigned short *idx = e->collision_idx;
    size_t idxsz = e->collision_idxsz;
    size_t vxsz = e->collision_vxsz;
    float *vx = e->collision_vx;
    dGeomID trimesh = NULL;
    dReal *tnorm = NULL;
    dReal *tvx = NULL;
    dTriIndex *tidx;
    int i;

#ifdef dSINGLE
    dbg("dSINGLE; dReal: %d dTriIndex: %d\n", sizeof(dReal), sizeof(dTriIndex));
#else
    dbg("dDOUBLE; dReal: %d dTriIndex: %d\n", sizeof(dReal), sizeof(dTriIndex));
#endif
    idxsz /= sizeof(unsigned short);
    CHECK(tidx = calloc(idxsz, sizeof(*tidx))); /* XXX: refcounting, or tied to model? */
    for (i = 0; i < idxsz; i += 3) {
        /* swap i+1 and i+2 on either side to switch winding */
        tidx[i + 0] = idx[i + 0];
        tidx[i + 1] = idx[i + 1];
        tidx[i + 2] = idx[i + 2];
    }

    vxsz /= sizeof(GLfloat);
    CHECK(tvx = calloc(vxsz, sizeof(*tvx)));
    for (i = 0; i < vxsz; i += 3) {
        tvx[i + 0] = vx[i + 0];
        tvx[i + 1] = vx[i + 1];
        tvx[i + 2] = vx[i + 2];
    }
    /*if (m->norm) {
        CHECK(tnorm = calloc(vxsz, sizeof(*tnorm)));
        for (i = 0; i < vxsz; i += 3) {
            //vec3_norm(&norm[i], &norm[i]);
            tnorm[i + 0] = m->norm[i + 0];
            tnorm[i + 1] = m->norm[i + 1];
            tnorm[i + 2] = m->norm[i + 2];
            dNormalize3(&tnorm[i]);
        }
    }*/
#ifdef dDOUBLE
    dGeomTriMeshDataBuildDouble(meshdata, tvx, 3 * sizeof(dReal), vxsz / 3, tidx, idxsz, 3 * sizeof(dTriIndex));
#else
    dGeomTriMeshDataBuildSingle1(meshdata, tvx, 3 * sizeof(float), vxsz / 3, tidx, idxsz, 3 * sizeof(dTriIndex), NULL);
#endif
    //dGeomTriMeshDataBuildSimple(meshdata, tvx, vxsz, tidx, idxsz);
    dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_FACE_ANGLES), NULL);
    //dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_CONCAVE_EDGES), NULL);
    CHECK(trimesh = dCreateTriMesh(phys->space, meshdata, NULL, NULL, NULL));
    //dGeomSetData(trimesh, e);

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

struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, int class, int type, double mass)
{
    bool has_body = type == PHYS_BODY;
    struct phys_body *body;
    dMatrix3 rot;
    dMass m;

    CHECK(body = calloc(1, sizeof(*body)));
    list_init(&body->pen_entry);
    body->phys = phys;
    
    if (has_body)
        body->body = dBodyCreate(phys->world);

    if (class == dTriMeshClass) {
        phys_geom_trimesh_new(phys, body, entity, mass);
    } else if (class == dSphereClass) {
        dMassSetZero(&m);
        dMassSetSphereTotal(&m, mass, 0.1);
    } else if (class == dCapsuleClass) {
        phys_geom_capsule_new(phys, body, entity, mass);
    }

    dRSetIdentity(rot);
    if (has_body) {
        dBodySetPosition(body->body, entity->dx, entity->dy, entity->dz);
        dBodySetRotation(body->body, rot);
        dGeomSetBody(body->geom, body->body);
        dBodySetData(body->body, entity);
    } else {
        dGeomSetPosition(body->geom, entity->dx, entity->dy, entity->dz);
        dGeomSetRotation(body->geom, rot);
    }
    dGeomSetData(body->geom, entity);
    list_append(&phys_bodies, &body->entry);

    if (has_body) {
        body->lmotor = dJointCreateLMotor(phys->world, 0);
        dJointSetLMotorNumAxes(body->lmotor, 3);
        dJointSetLMotorAxis(body->lmotor, 0, 0, 1, 0, 0);
        dJointSetLMotorAxis(body->lmotor, 1, 0, 0, 1, 0);
        dJointSetLMotorAxis(body->lmotor, 2, 0, 0, 0, 1);
        dJointSetLMotorParam(body->lmotor, dParamFMax1, 10.0);
        dJointSetLMotorParam(body->lmotor, dParamFMax2, 10.0);
        dJointSetLMotorParam(body->lmotor, dParamFMax3, 10.0);
    }

    return body;
}

void phys_body_done(struct phys_body *body)
{
    list_del(&body->entry);
    if (body->geom)
        dGeomDestroy(body->geom);
    if (body->body)
        dBodyDestroy(body->body);
    body->geom = NULL;
    body->body = NULL;
    free(body);
}

int phys_init(void)
{
    dInitODE2(0);
    phys->world = dWorldCreate();
    phys->space = dHashSpaceCreate(0);
    phys->collision = dHashSpaceCreate(phys->space);
    phys->contact = dJointGroupCreate(0);
    dWorldSetGravity(phys->world, 0, -9.8, 0);
    // dWorldSetCFM(phys->world, 1e-5);
    // dWorldSetERP(phys->world, 0.8);
    //dWorldSetContactSurfaceLayer(phys->world, 0.001);
    dWorldSetLinearDamping(phys->world, 0.001);
    //phys->ground = dCreatePlane(phys->space, 0, 0, 1, 0);

    return 0;
}

void phys_done(void)
{
    dSpaceDestroy(phys->collision);
    dSpaceDestroy(phys->space);
    dWorldDestroy(phys->world);
    dCloseODE();
}
