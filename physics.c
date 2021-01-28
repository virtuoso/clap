#include <ode/ode.h>
#include "common.h"
#include "model.h"
#include "physics.h"

static struct phys _phys;
struct phys *phys = &_phys; /* XXX */

/*
 * Separate thread? Emscripten won't be happy.
 */
#define MAX_CONTACTS 16

static void near_callback(void *data, dGeomID o1, dGeomID o2)
{
    bool ground = o1 == phys->ground || o2 == phys->ground;
    dContact contact[MAX_CONTACTS];
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);
    struct entity3d *e1 = dGeomGetData(o1);
    struct entity3d *e2 = dGeomGetData(o2);
    dReal bounce = 0.001, bounce_vel = 0.0;
    dJointID j;
    int i, nc;

    if (b1 && b2 && dAreConnectedExcluding(b1, b2, dJointTypeContact))
        return;

    if (e1 && e1->phys_body && e1->phys_body->bounce) {
        bounce = e1->phys_body->bounce;
        bounce_vel = e1->phys_body->bounce_vel;
    }

    if (e2 && e2->phys_body && e2->phys_body->bounce) {
        bounce = e2->phys_body->bounce;
        bounce_vel = e2->phys_body->bounce_vel;
    }
    //bounce = 0, bounce_vel = 0;

    for (i = 0; i < MAX_CONTACTS; i++) {
        contact[i].surface.mode = bounce != 0 ? dContactBounce | dContactSoftCFM : 0;
        contact[i].surface.mu   = /*bounce != 0 ? dInfinity : */0;
        contact[i].surface.mu2  = 0;
        contact[i].surface.bounce = bounce;
        contact[i].surface.bounce_vel = bounce_vel;
        contact[i].surface.soft_cfm = bounce != 0 ? 0.01 : 0;
    }

    //msg("collision testing: %s vs %s\n (%d)\n", e1 ? entity_name(e1) : "", e2 ? entity_name(e2) : "", ground);
    nc = dCollide(o1, o2, MAX_CONTACTS, &contact[0].geom, sizeof(dContact));
    if (nc) {
        //phys_body_update(b1);
        //phys_body_update(b2);
        //dbg("collision '%s', '%s'\n", e1 ? entity_name(e1) : "", e2 ? entity_name(e2) : "");
        for (i = 0; i < nc; i++) {
            j = dJointCreateContact(phys->world, phys->contact, &contact[i]);
            dJointAttach(j, b1, b2);
        }

        if (ground) {
            struct entity3d *e    = e1 ? e1 : e2;
            const char      *name = entity_name(e);
            dBodyID         b    = e->phys_body->body;

            //dbg("ground collision: %s\n", name);

            if (strstr(name, "tree") && dBodyIsEnabled(b)) {
                phys_body_update(e);
                //entity3d_move(e, 0, -0.2, 0);
                dBodyDisable(b);
                //dbg("ground collision of '%s' at %f,%f (+ %f),%f\n", name, e->dx, e->dy, e->phys_body->zoffset, e->dz);
                phys->ground_contact(NULL, e->dx, e->dy + 2.0, e->dz);
            }
        }
    }
}

void phys_body_update(struct entity3d *e)
{
    const dReal *pos;

    if (!e->phys_body)
        return;

    pos = dBodyGetPosition(e->phys_body->body);
    e->dx = pos[0];
    e->dy = pos[2] - e->phys_body->zoffset;
    e->dz = pos[1];
}

dGeomID phys_geom_new(struct phys *phys, float *vx, size_t vxsz, float *norm, unsigned short *idx, size_t idxsz)
{
    dGeomID trimesh = NULL;
    dTriMeshDataID meshdata = dGeomTriMeshDataCreate();
    float *tvx = NULL;
    float *tnorm = NULL;
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
        tidx[i + 0] = idx[i + 0];
        tidx[i + 1] = idx[i + 2];
        tidx[i + 2] = idx[i + 1];

        /*tidx[i + 3] = idx[i + 6];
        tidx[i + 4] = idx[i + 7];
        tidx[i + 5] = idx[i + 8];

        tidx[i + 6] = idx[i + 3];
        tidx[i + 7] = idx[i + 4];
        tidx[i + 8] = idx[i + 5];*/
    }

    vxsz /= sizeof(GLfloat);
    CHECK(tvx = calloc(vxsz, sizeof(*tvx)));
    for (i = 0; i < vxsz; i += 3) {
        tvx[i]     = vx[i];
        tvx[i + 1] = vx[i + 2];
        tvx[i + 2] = vx[i + 1];
    }
    if (norm) {
        CHECK(tnorm = calloc(vxsz, sizeof(*tnorm)));
        for (i = 0; i < vxsz; i += 3) {
            tnorm[i]     = norm[i];
            tnorm[i + 1] = norm[i + 2];
            tnorm[i + 2] = norm[i + 1];
        }
    }
//#ifdef dDOUBLE
//    dGeomTriMeshDataBuildDouble1(meshdata, tvx, 3 * sizeof(float), vxsz, tidx, idxsz, 3 * sizeof(dTriIndex), norm);
//#else
    dGeomTriMeshDataBuildSingle1(meshdata, tvx, 3 * sizeof(float), vxsz, tidx, idxsz, 3 * sizeof(dTriIndex), tnorm);
//#endif
    //dGeomTriMeshDataBuildSimple(meshdata, tvx, vxsz, tidx, idxsz);
    dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_FACE_ANGLES), NULL);
    //dGeomTriMeshDataPreprocess2(meshdata, (1U << dTRIDATAPREPROCESS_BUILD_CONCAVE_EDGES), NULL);
    trimesh = dCreateTriMesh(phys->space, meshdata, NULL, NULL, NULL);
    /*free(tidx);
    free(tvx);
    free(tnorm);*/

    return trimesh;
}

struct phys_body *
phys_body_new(struct phys *phys, struct entity3d *entity, enum geom_type type, dReal mass, dGeomID geom, float x, float y, float z)
{
    struct phys_body *body;
    dMass m;

    CHECK(body = calloc(1, sizeof(*body)));
    body->geom = geom;//dCreateSphere(phys->space, 0.1);
    body->body = dBodyCreate(phys->world);

    if (type == GEOM_TRIMESH) {
        dMassSetTrimesh(&m, mass, body->geom);
        dGeomSetPosition(body->geom, -m.c[0], -m.c[1], -m.c[2]);
        dMassTranslate(&m, -m.c[0], -m.c[1], -m.c[2]);
    } else {
        dMassSetZero(&m);
        dMassSetSphereTotal(&m, mass, 0.1);
    }
    dBodySetMass(body->body, &m);
    dBodySetPosition(body->body, x, z, y);
    dGeomSetBody(body->geom, body->body);
    dGeomSetData(body->geom, entity);

    return body;
}

void phys_body_done(struct phys_body *body)
{
    //dGeomDestroy(body->geom);
    //dBodyDestroy(body->body);
    body->geom = NULL;
    body->body = NULL;
    free(body);
}

void phys_step(void)
{
    dSpaceCollide(phys->space, 0, near_callback);
    dWorldQuickStep(phys->world, 0.01);
    dJointGroupEmpty(phys->contact);
}

int phys_init(void)
{
    dInitODE2(0);
    phys->world = dWorldCreate();
    phys->space = dHashSpaceCreate(0);
    phys->contact = dJointGroupCreate(0);
    dWorldSetGravity(phys->world, 0, 0, -9.8);
    //dWorldSetCFM(phys->world, 1e-5);
    //phys->ground = dCreatePlane(phys->space, 0, 0, 1, 0);

    return 0;
}

void phys_done(void)
{
    dSpaceDestroy(phys->space);
    dWorldDestroy(phys->world);
    dCloseODE();
}
