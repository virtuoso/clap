#ifndef __CLAP_PHYSICS_H__
#define __CLAP_PHYSICS_H__

#ifdef __EMSCRIPTEN__
#define dSINGLE
#else
#define dDOUBLE
#endif
#include <ode/ode.h>
#include "linmath.h"

enum {
    PHYS_BODY = 0,
    PHYS_GEOM,
};

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
};

struct phys {
    dWorldID    world;
    dSpaceID    space;
    dSpaceID    collision;
    dGeomID     ground;
    dJointGroupID contact;
    void        (*ground_contact)(void *priv, float x, float y, float z);
};

extern struct phys *phys;

struct entity3d;

void phys_step(void);
int  phys_init(void);
void phys_done(void);
static inline bool phys_body_has_body(struct phys_body *body) { return !!body->body; }
struct entity3d *phys_body_entity(struct phys_body *body);
const dReal *phys_body_position(struct phys_body *body);
const dReal *phys_body_rotation(struct phys_body *body);
dGeomID phys_geom_capsule_new(struct phys *phys, struct phys_body *body, struct entity3d *e, double mass);
dGeomID phys_geom_trimesh_new(struct phys *phys, struct phys_body *body, struct entity3d *e, double mass);
struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, int class, int type, double mass);
int phys_body_update(struct entity3d *e);
void phys_body_done(struct phys_body *body);
bool phys_body_ground_collide(struct phys_body *body);
bool phys_body_is_grounded(struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
