/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_NAV_NAVMESH_INTERNAL_H__
#define __CLAP_NAV_NAVMESH_INTERNAL_H__

#include <stdint.h>
#include "util.h"       /* darray */
#include "linmath.h"    /* vec3 */
#include "navmesh.h"    /* navmesh_params */

/*
 * The navmesh internals, shared within core/nav/: navmesh.c (build/query),
 * nav/debug.c (the walkable-surface overlay and the roamer debug window,
 * which need the debug slots) and nav/roamer.c (the params, for its
 * step-bounded locates, and the roamer registry).
 * Nothing outside core/nav/ includes this: the navmesh is opaque.
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

/* an A* open-heap node, f-cost ordered */
struct nm_hnode {
    float   f;
    int     tri;
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

    /*
     * A* scratch, sized at bake and reused by every navmesh_path()
     * call (queries are single-threaded, like the rest of the scene);
     * pf_seen/pf_closed stamps against pf_gen replace per-call clearing
     */
    uint32_t        pf_gen;
    float           *pf_g;
    int             *pf_parent;
    int             *pf_parent_link;
    uint32_t        *pf_seen;
    uint32_t        *pf_closed;
    struct nm_hnode *pf_heap;
#ifndef CONFIG_FINAL
    /* walkable-surface wireframe overlay, in 16-bit-index-sized chunks */
    darray(struct entity3d *, debug_entities);
    struct list     roamers;            /* roamers on this mesh, for the debug UI */
    bool            all_paths;          /* the debug UI's "show all paths" */
#endif /* !CONFIG_FINAL */
    bool            baked;
};

#endif /* __CLAP_NAV_NAVMESH_INTERNAL_H__ */
