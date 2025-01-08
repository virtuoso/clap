/* SPDX-License-Identifier: Apache-2.0 */
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
    dReal       *trimesh_vx;
    dTriIndex   *trimesh_idx;
    int         class;
};

struct phys {
    dWorldID    world;
    dSpaceID    space;
    dSpaceID    character_space;
    dSpaceID    ground_space;
    dSpaceID    collision;
    dJointGroupID contact;
    void        (*ground_contact)(void *priv, float x, float y, float z);
};

extern struct phys *phys;

struct entity3d;

void phys_step(unsigned long frame_count);
int  phys_init(void);
void phys_done(void);
void phys_ground_add(struct entity3d *e);
struct entity3d *phys_ray_cast(struct entity3d *e, vec3 start, vec3 dir, double *pdist);
static inline bool phys_body_has_body(struct phys_body *body) { return !!body->body; }
struct entity3d *phys_body_entity(struct phys_body *body);
const dReal *phys_body_position(struct phys_body *body);
const dReal *phys_body_rotation(struct phys_body *body);
dGeomID phys_geom_capsule_new(struct phys *phys, struct phys_body *body, struct entity3d *e,
                              double mass, double geom_radius, double geom_offset);
dGeomID phys_geom_trimesh_new(struct phys *phys, struct phys_body *body, struct entity3d *e, double mass);
struct phys_body *phys_body_new(struct phys *phys, struct entity3d *entity, int class,
                                double geom_radius, double geom_offset, int type, double mass);
int phys_body_update(struct entity3d *e);
void phys_body_done(struct phys_body *body);
void phys_body_attach_motor(struct phys_body *body, bool attach);
void phys_body_set_velocity_vec(struct phys_body *body, vec3 vel);
void phys_body_set_motor_velocity_vec(struct phys_body *body, bool body_also, vec3 vel);
static inline void phys_body_set_motor_velocity(struct phys_body *body, bool body_also, float x, float y, float z)
{
    vec3 vel = { x, y, z };
    phys_body_set_motor_velocity_vec(body, body_also, vel);
}
void phys_body_stop(struct phys_body *body);
bool phys_body_ground_collide(struct phys_body *body, bool grounded);
void phys_ground_entity(struct entity3d *e);

struct scene;
void phys_debug_draw(struct scene *scene, struct phys_body *body);

#endif /* __CLAP_PHYSICS_H__ */
