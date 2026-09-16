/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NAV_NAVMESH_INTERNAL_H__
#define __CLAP_NAV_NAVMESH_INTERNAL_H__

#include "util.h"       /* darray */
#include "linmath.h"    /* vec3 */
#include "navmesh.h"    /* navmesh_params */

/*
 * The navmesh internals, shared between navmesh.c (build/query) and
 * nav/debug.c (the walkable-surface overlay, which needs the debug_entity
 * slot). Nothing outside core/nav/ includes this: the navmesh is opaque.
 */

struct entity3d;

/* a crossing into a neighbor triangle: the portal interval */
struct nav_link {
    int     to;
    vec3    pa;
    vec3    pb;
};

/* one entry of a grid cell's chain */
struct nm_gent {
    int     idx;
    int     next;
};

typedef struct nav_tri {
    int     v[3];
    int     link_first;
    int     link_nr;
} nav_tri;

/* an obstruction triangle and its world AABB */
struct nm_ob {
    vec3    v[3];
    float   mn[3];
    float   mx[3];
    int     src;    /* which navmesh_add() batch it came from */
    /*
     * Explicitly marked obstacles (hazards) deny the floor they cover
     * at any height; slope-derived ones only above step_height, so
     * steps and gentle clutter stay walkable.
     */
    bool    deny;
};

struct navmesh {
    navmesh_params  params;

    /* build-time soup, freed by navmesh_bake() */
    darray(float, floor_soup);          /* 9 floats per floor triangle */
    darray(struct nm_ob, obstacles);    /* hard obstructions */

    /* baked */
    vec3            *verts;
    nav_tri         *tris;
    struct nav_link *links;
    float           *cdf;               /* cumulative triangle area */
    size_t          nr_verts;
    size_t          nr_tris;
    size_t          nr_links;
    float           total_area;
    int             nr_sources;

    /* the XZ locate grid: each cell chains the triangles whose AABB overlaps it */
    struct nm_gent  *loc_ents;
    int             *loc_heads;
    int             loc_nx, loc_nz;
    float           loc_cell;
    float           loc_x0, loc_z0;
#ifdef CLAP_NAVMESH_DEBUG
    struct entity3d *debug_entity;      /* walkable-surface wireframe overlay */
#endif /* CLAP_NAVMESH_DEBUG */
    bool            baked;
};

#endif /* __CLAP_NAV_NAVMESH_INTERNAL_H__ */
