#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include <ode/ode.h>

enum geom_type {
    GEOM_TRIMESH = 0,
    GEOM_SPHERE,
};

struct phys_body {
    dGeomID     geom;
    dBodyID     body;
    dReal       bounce;
    dReal       bounce_vel;
    float       zoffset;
};

struct phys {
    dWorldID    world;
    dSpaceID    space;
    dGeomID     ground;
    dJointGroupID contact;
    void        (*ground_contact)(void *priv, float x, float y, float z);
};

extern struct phys *phys;

struct entity3d;

void phys_step(void);
int  phys_init(void);
void phys_done(void);
dGeomID phys_geom_new(struct phys *phys, float *vx, size_t vxsz, float *norm, unsigned short *idx, size_t idxsz);
struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, enum geom_type type, dReal mass, dGeomID geom, float x, float y, float z);
void phys_body_update(struct entity3d *e);
void phys_body_done(struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
