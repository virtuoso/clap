// SPDX-License-Identifier: Apache-2.0
/*
 * Walkable-surface navigation mesh built from collision triangle soup.
 *
 * Sources are split by slope into floor candidates and obstructions at
 * add time; baking welds the floors into a connected mesh, culls the
 * triangles whose clearance column is cut by an obstruction, keeps the
 * connected component around a seed point and precomputes the area
 * distribution for uniform random sampling. Pathfinding is A* over the
 * triangle adjacency graph with funnel straightening.
 *
 * Adjacency is two-phase: exactly shared welded edges first, then the
 * remaining border edges are stitched across collinear overlaps.
 * Authored meshes are full of T-junctions (a big triangle's edge
 * meeting a row of smaller ones), which exact matching alone would
 * shred into disconnected patches. Every crossing carries its portal
 * interval explicitly, and a single edge may link several neighbors.
 *
 * Everything is world space; the mesh is 2.5D: triangles may stack
 * vertically (bridges, platforms), (x, z) queries disambiguate by
 * height.
 */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "common.h"
#include "memory.h"
#include "util.h"
#include "navmesh.h"
#include "navmesh-internal.h"

#define NAVMESH_MAX_SLOPE       0.698f  /* 40 degrees */
#define NAVMESH_WELD            0.01f
#define NAVMESH_STEP_HEIGHT     0.25f
#define NAVMESH_AGENT_HEIGHT    2.0f
#define NAVMESH_AGENT_RADIUS    0.5f

/* barycentric slack for point-in-triangle tests */
#define NM_BARY_EPS             1e-4
/* stitched portals shorter than this are not worth crossing */
#define NM_MIN_PORTAL           0.05f

/*
 * nav_link, nav_tri, nm_ob, nm_gent, nm_hnode and struct navmesh live in
 * navmesh-internal.h, shared with nav/debug.c
 */

/***
 * open-addressing map: 63-bit keys, int64 values, sized up front
 ***/

#define NM_EMPTY (~0ull)

struct nm_map {
    uint64_t    *keys;
    int64_t     *vals;
    size_t      cap;    /* power of two, >= 2 * items */
};

static void nm_map_init(struct nm_map *m, size_t items)
{
    size_t cap = 16;

    while (cap < items * 2) cap <<= 1;

    m->cap = cap;
    m->keys = mem_alloc(sizeof(*m->keys), .nr = cap, .fatal_fail = 1);
    m->vals = mem_alloc(sizeof(*m->vals), .nr = cap, .fatal_fail = 1);
    memset(m->keys, 0xff, cap * sizeof(*m->keys));
}

static void nm_map_done(struct nm_map *m)
{
    mem_free(m->keys);
    mem_free(m->vals);
    m->keys = nullptr;
    m->vals = nullptr;
}

static size_t nm_map_start(struct nm_map *m, uint64_t key)
{
    uint64_t h = key * 0x9e3779b97f4a7c15ull;

    return (h ^ (h >> 32)) & (m->cap - 1);
}

/* find or claim a slot; a fresh slot's value is -1 */
static int64_t *nm_map_slot(struct nm_map *m, uint64_t key)
{
    size_t i = nm_map_start(m, key);

    while (m->keys[i] != key) {
        if (m->keys[i] == NM_EMPTY) {
            m->keys[i] = key;
            m->vals[i] = -1;
            break;
        }
        i = (i + 1) & (m->cap - 1);
    }

    return &m->vals[i];
}

/* lookup without claiming; -1 if absent */
static int64_t nm_map_get(struct nm_map *m, uint64_t key)
{
    size_t i = nm_map_start(m, key);

    while (m->keys[i] != key) {
        if (m->keys[i] == NM_EMPTY) return -1;
        i = (i + 1) & (m->cap - 1);
    }

    return m->vals[i];
}

/* pack signed cell coordinates into map keys, 21 bits each */
#define NM_CELL_BIAS (1 << 20)

static uint64_t nm_cell3(int cx, int cy, int cz)
{
    return ((uint64_t)((cx + NM_CELL_BIAS) & 0x1fffff) << 42) |
           ((uint64_t)((cy + NM_CELL_BIAS) & 0x1fffff) << 21) |
            (uint64_t)((cz + NM_CELL_BIAS) & 0x1fffff);
}

static uint64_t nm_cell2(int cx, int cz)
{
    return ((uint64_t)((cx + NM_CELL_BIAS) & 0x1fffff) << 21) |
            (uint64_t)((cz + NM_CELL_BIAS) & 0x1fffff);
}

/***
 * construction
 ***/

cresp(navmesh) _navmesh_new(const navmesh_params *params)
{
    navmesh *nm = mem_alloc(sizeof(*nm), .zero = 1);

    if (!nm)    return cresp_error(navmesh, CERR_NOMEM);

    nm->params = *params;
    nm->params.max_slope    = nm->params.max_slope    ? : NAVMESH_MAX_SLOPE;
    nm->params.weld         = nm->params.weld         ? : NAVMESH_WELD;
    nm->params.step_height  = nm->params.step_height  ? : NAVMESH_STEP_HEIGHT;
    nm->params.agent_height = nm->params.agent_height ? : NAVMESH_AGENT_HEIGHT;
    nm->params.agent_radius = nm->params.agent_radius ? : NAVMESH_AGENT_RADIUS;

    darray_init(nm->floor_soup);
    darray_init(nm->obstacles);
#ifndef CONFIG_FINAL
    list_init(&nm->roamers);
    darray_init(nm->debug_entities);
#endif /* !CONFIG_FINAL */

    return cresp_val(navmesh, nm);
}

void navmesh_free(navmesh *nm)
{
    if (!nm)    return;

    darray_clearout(nm->floor_soup);
    darray_clearout(nm->obstacles);
#ifndef CONFIG_FINAL
    /* the overlay entities themselves belong to the scene's model queue */
    darray_clearout(nm->debug_entities);
#endif /* !CONFIG_FINAL */
    mem_free(nm->verts);
    mem_free(nm->tris);
    mem_free(nm->links);
    mem_free(nm->cdf);
    mem_free(nm->loc_ents);
    mem_free(nm->loc_heads);
    mem_free(nm->pf_g);
    mem_free(nm->pf_parent);
    mem_free(nm->pf_parent_link);
    mem_free(nm->pf_seen);
    mem_free(nm->pf_closed);
    mem_free(nm->pf_heap);
    mem_free(nm);
}

cerr _navmesh_add(navmesh *nm, const navmesh_source *source)
{
    if (nm->baked)  return CERR_INVALID_OPERATION;
    if (!source->vx || !source->idx ||
        source->vxsz % (3 * sizeof(float)) || !source->vxsz ||
        source->idxsz % (3 * sizeof(unsigned short)) || !source->idxsz)
        return CERR_INVALID_ARGUMENTS;

    size_t nr_vx = source->vxsz / (3 * sizeof(float));
    size_t nr_idx = source->idxsz / sizeof(unsigned short);
    float scale = source->scale ? : 1.0f;
    float min_ny = cosf(nm->params.max_slope);

    for (size_t i = 0; i < nr_idx; i++)
        if (source->idx[i] >= nr_vx)    return CERR_INVALID_ARGUMENTS;

    for (size_t i = 0; i < nr_idx; i += 3) {
        vec3 w[3];

        /* world = rot * (scale * v) + pos, same as the physics trimesh */
        for (int c = 0; c < 3; c++) {
            vec3_scale(w[c], &source->vx[source->idx[i + c] * 3], scale);
            if (source->rot)    quat_mul_vec3(w[c], (float *)source->rot, w[c]);
            if (source->pos)    vec3_add(w[c], w[c], (float *)source->pos);
        }

        vec3 ab, ac, n;
        vec3_sub(ab, w[1], w[0]);
        vec3_sub(ac, w[2], w[0]);
        vec3_mul_cross(n, ab, ac);

        float len = vec3_len(n);
        if (len < 1e-9f)    continue;

        if (!source->obstacle && n[1] / len >= min_ny) {
            size_t at = darray_count(nm->floor_soup);

            if (!darray_resize(nm->floor_soup, at + 9)) return CERR_NOMEM;
            memcpy(&nm->floor_soup.x[at], w, sizeof(w));
        } else {
            struct nm_ob *ob = darray_add(nm->obstacles);

            if (!ob)    return CERR_NOMEM;
            memcpy(ob->v, w, sizeof(w));
            for (int c = 0; c < 3; c++) {
                ob->mn[c] = fminf(w[0][c], fminf(w[1][c], w[2][c]));
                ob->mx[c] = fmaxf(w[0][c], fmaxf(w[1][c], w[2][c]));
            }
            ob->deny = source->obstacle;
            ob->src = nm->nr_sources;
        }
    }

    nm->nr_sources++;

    return CERR_OK;
}

/***
 * baking
 ***/

/* find a matching vertex in the weld grid or add a new one */
static int nm_weld_vert(struct nm_map *cells, vec3 **verts, int **vnext,
                        size_t *nr_verts, const float *p, float weld)
{
    int cx = (int)floorf(p[0] / weld);
    int cy = (int)floorf(p[1] / weld);
    int cz = (int)floorf(p[2] / weld);
    float best_d = weld * weld;
    int best = -1;

    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int64_t i = nm_map_get(cells, nm_cell3(cx + dx, cy + dy, cz + dz));

                for (; i >= 0; i = (*vnext)[i]) {
                    vec3 d;

                    vec3_sub(d, (*verts)[i], (float *)p);
                    float dist = vec3_mul_inner(d, d);
                    if (dist <= best_d) {
                        best_d = dist;
                        best = i;
                    }
                }
            }

    if (best >= 0)  return best;

    int idx = (*nr_verts)++;
    vec3_dup((*verts)[idx], (float *)p);

    int64_t *head = nm_map_slot(cells, nm_cell3(cx, cy, cz));
    (*vnext)[idx] = *head;
    *head = idx;

    return idx;
}

static double nm_cross2(const float *a, const float *b, const float *c)
{
    return ((double)b[0] - a[0]) * ((double)c[2] - a[2]) -
           ((double)c[0] - a[0]) * ((double)b[2] - a[2]);
}

static void nm_tri_aabb(navmesh *nm, const nav_tri *t, struct nm_ob *box)
{
    for (int c = 0; c < 3; c++) {
        box->mn[c] = fminf(nm->verts[t->v[0]][c],
                           fminf(nm->verts[t->v[1]][c], nm->verts[t->v[2]][c]));
        box->mx[c] = fmaxf(nm->verts[t->v[0]][c],
                           fmaxf(nm->verts[t->v[1]][c], nm->verts[t->v[2]][c]));
    }
}

/* the locate-grid cells [*c0, *c1] that the world interval [lo, hi] overlaps */
static void nm_loc_span(float lo, float hi, float org, float cell, int n, int *c0, int *c1)
{
    int a = (int)floorf((lo - org) / cell);
    int b = (int)floorf((hi - org) / cell);

    *c0 = max(0, min(n - 1, a));
    *c1 = max(0, min(n - 1, b));
}

static void nm_centroid(navmesh *nm, int tri, vec3 c)
{
    nav_tri *t = &nm->tris[tri];

    for (int i = 0; i < 3; i++) c[i] = (nm->verts[t->v[0]][i] + nm->verts[t->v[1]][i] + nm->verts[t->v[2]][i]) / 3.0f;
}

/* triangle area from three welded-vertex indices */
static float nm_tri_area3(vec3 *verts, const int v[3])
{
    vec3 ab, ac, n;

    vec3_sub(ab, verts[v[1]], verts[v[0]]);
    vec3_sub(ac, verts[v[2]], verts[v[0]]);
    vec3_mul_cross(n, ab, ac);

    return vec3_len(n) * 0.5f;
}

/* barycentric height on a triangle; sets *inside for the (x, z) test */
static float nm_tri_height_at(navmesh *nm, const nav_tri *t, float x, float z, bool *inside)
{
    float *a = nm->verts[t->v[0]], *b = nm->verts[t->v[1]], *c = nm->verts[t->v[2]];
    vec3 p = { x, 0.0f, z };
    double d = nm_cross2(a, b, c);

    if (fabs(d) < 1e-9) {
        if (inside) *inside = false;
        return a[1];
    }

    double la = nm_cross2(p, b, c) / d;
    double lb = nm_cross2(a, p, c) / d;
    double lc = nm_cross2(a, b, p) / d;

    if (inside) *inside = la >= -NM_BARY_EPS && lb >= -NM_BARY_EPS && lc >= -NM_BARY_EPS;

    return (float)(la * a[1] + lb * b[1] + lc * c[1]);
}

/* clip a polygon to one side of an axis plane, interpolating all components */
static int nm_clip_axis(const vec3 *in, int nr, vec3 *out, int axis, float bound, bool below)
{
    int nr_out = 0;

    for (int i = 0; i < nr; i++) {
        const float *a = in[i], *b = in[(i + 1) % nr];
        float da = below ? bound - a[axis] : a[axis] - bound;
        float db = below ? bound - b[axis] : b[axis] - bound;

        if (da >= 0.0f) vec3_dup(out[nr_out++], (float *)a);
        if ((da >= 0.0f) != (db >= 0.0f)) {
            float s = da / (da - db);

            for (int c = 0; c < 3; c++) out[nr_out][c] = a[c] + (b[c] - a[c]) * s;
            nr_out++;
        }
    }

    return nr_out;
}

/*
 * The obstruction triangle's height range within the query XZ box:
 * low-poly art means a single wall or cliff triangle can span half the
 * map, its global AABB with it. Only the piece inside the column may
 * block.
 */
static bool nm_ob_y_range(const struct nm_ob *ob, const float qmn[2], const float qmx[2],
                          float *ymn, float *ymx)
{
    vec3 a[8], b[8];
    int nr;

    nr = nm_clip_axis(ob->v, 3, a, 0, qmn[0], false);
    nr = nm_clip_axis(a, nr, b, 0, qmx[0], true);
    nr = nm_clip_axis(b, nr, a, 2, qmn[1], false);
    nr = nm_clip_axis(a, nr, b, 2, qmx[1], true);
    if (!nr)    return false;

    *ymn = *ymx = b[0][1];
    for (int i = 1; i < nr; i++) {
        *ymn = fminf(*ymn, b[i][1]);
        *ymx = fmaxf(*ymx, b[i][1]);
    }

    return true;
}

/*
 * Does anything cut into the clearance column above floor triangle @t?
 * Hard obstructions block between step_height and agent_height above
 * the surface; other floors only when they hang less than agent_height
 * overhead (a walkable slope isn't a roof over its own foot).
 * Return: the blocking source, -1 for a floor overhead, INT_MIN if clear
 */
static int nm_tri_blocked(navmesh *nm, int t, const nav_tri *tris,
                          const struct nm_ob *soft, size_t nr_soft,
                          struct nm_map *grid, const struct nm_gent *gents, float cell)
{
    const navmesh_params *p = &nm->params;
    size_t nr_hard = darray_count(nm->obstacles);

    /*
     * The clearance is tested as a column around the floor triangle's
     * centroid: floor triangles come as big as the art is low-poly,
     * and a wall near one corner must not condemn the whole thing.
     * Paths keep off the edges anyway (portal shrinking).
     */
    float cx = (nm->verts[tris[t].v[0]][0] + nm->verts[tris[t].v[1]][0] +
                nm->verts[tris[t].v[2]][0]) / 3.0f;
    float cz = (nm->verts[tris[t].v[0]][2] + nm->verts[tris[t].v[1]][2] +
                nm->verts[tris[t].v[2]][2]) / 3.0f;
    float yf = nm_tri_height_at(nm, &tris[t], cx, cz, nullptr);
    float qmn[2] = { cx - p->agent_radius, cz - p->agent_radius };
    float qmx[2] = { cx + p->agent_radius, cz + p->agent_radius };

    for (int gz = (int)floorf(qmn[1] / cell); gz <= (int)floorf(qmx[1] / cell); gz++)
        for (int gx = (int)floorf(qmn[0] / cell); gx <= (int)floorf(qmx[0] / cell); gx++) {
            int64_t i = nm_map_get(grid, nm_cell2(gx, gz));

            for (; i >= 0; i = gents[i].next) {
                int obi = gents[i].idx;
                const struct nm_ob *ob = obi < (int)nr_hard ?
                                         &nm->obstacles.x[obi] : &soft[obi - nr_hard];

                if (ob->mn[0] > qmx[0] || ob->mx[0] < qmn[0] ||
                    ob->mn[2] > qmx[1] || ob->mx[2] < qmn[1])
                    continue;

                if (obi < (int)nr_hard) {
                    /*
                     * Slope-derived obstructions must rise a step above
                     * the floor surface: noisy terrain bumps stay
                     * walkable, walls and cliffs still block. Hazards
                     * (deny) reach a step below the floor, too. Only
                     * the piece of the triangle inside the column
                     * counts, not its global extent.
                     */
                    float lo = ob->deny ? yf - p->step_height :
                                          yf + p->step_height;
                    float ymn, ymx;

                    if (ob->mx[1] >= lo &&
                        ob->mn[1] <= yf + p->agent_height &&
                        nm_ob_y_range(ob, qmn, qmx, &ymn, &ymx) &&
                        ymx >= lo &&
                        ymn <= yf + p->agent_height)
                        return ob->src;
                } else {
                    size_t bi = obi - nr_hard;

                    if (bi == (size_t)t)    continue;

                    /*
                     * A roof must actually cover the floor: its plane
                     * over the floor's centroid, a step to agent_height
                     * up. AABB slop here would let uphill neighbors on
                     * rolling terrain read as roofs over the downhill
                     * ones.
                     */
                    bool covers;
                    float yb = nm_tri_height_at(nm, &tris[bi], cx, cz, &covers);

                    if (!covers)    continue;

                    if (yb >= yf + p->step_height &&
                        yb <= yf + p->agent_height)
                        return -1;
                }
            }
        }

    return INT_MIN;
}

/* a triangle crossing collected while linking */
struct nm_rec {
    int     from;
    int     to;
    vec3    pa;
    vec3    pb;
};

/* an unpaired triangle edge, a stitching candidate */
struct nm_bedge {
    int     tri;
    vec3    a;
    vec3    b;
};

static void nm_rec_add(struct darray *recs, int from, int to,
                       const float *pa, const float *pb)
{
    struct nm_rec *r = _darray_add(recs);

    if (!r) return;
    r->from = from;
    r->to = to;
    vec3_dup(r->pa, (float *)pa);
    vec3_dup(r->pb, (float *)pb);
}

/*
 * Stitch border edges across collinear XZ overlaps: T-junctions between
 * tessellation densities (a long edge meeting a row of short ones) and
 * small vertical steps (a stair riser, a ramp meeting the ground) where
 * the two surfaces differ by up to step_height. Working in XZ with a
 * height tolerance is what lets an agent climb; a strict 3D match would
 * only ever join coplanar patches. The portal is the overlap interval.
 */
static size_t nm_stitch_borders(navmesh *nm, const struct nm_bedge *bedges, size_t nr_bedges,
                                struct darray *recs)
{
    float tol = max(2.0f * nm->params.weld, 0.02f);
    float cell = 2.0f;
    size_t stitched = 0;

    /*
     * Grid the border edges by their 3D AABB. Walls stack hundreds of
     * borders onto the same (x, z) column, so a 2D grid would square
     * that density; the query still reaches one height cell either way
     * to catch step partners that straddle a cell boundary.
     */
    size_t nr_cells = 0;

    for (size_t i = 0; i < nr_bedges; i++) {
        const struct nm_bedge *e = &bedges[i];
        int cx0 = (int)floorf(fminf(e->a[0], e->b[0]) / cell);
        int cx1 = (int)floorf(fmaxf(e->a[0], e->b[0]) / cell);
        int cy0 = (int)floorf(fminf(e->a[1], e->b[1]) / cell);
        int cy1 = (int)floorf(fmaxf(e->a[1], e->b[1]) / cell);
        int cz0 = (int)floorf(fminf(e->a[2], e->b[2]) / cell);
        int cz1 = (int)floorf(fmaxf(e->a[2], e->b[2]) / cell);

        nr_cells += (size_t)(cx1 - cx0 + 1) * (size_t)(cy1 - cy0 + 1) *
                    (size_t)(cz1 - cz0 + 1);
    }

    struct nm_map grid;
    struct nm_gent *gents = mem_alloc(sizeof(*gents), .nr = nr_cells, .fatal_fail = 1);
    /* stamp[j] = the last i that considered j: O(1) pair dedup */
    int *stamp = mem_alloc(sizeof(*stamp), .nr = nr_bedges, .fatal_fail = 1);
    size_t gent = 0;

    for (size_t i = 0; i < nr_bedges; i++)  stamp[i] = -1;

    nm_map_init(&grid, nr_cells);

    for (size_t i = 0; i < nr_bedges; i++) {
        const struct nm_bedge *e = &bedges[i];
        int cx0 = (int)floorf(fminf(e->a[0], e->b[0]) / cell);
        int cx1 = (int)floorf(fmaxf(e->a[0], e->b[0]) / cell);
        int cy0 = (int)floorf(fminf(e->a[1], e->b[1]) / cell);
        int cy1 = (int)floorf(fmaxf(e->a[1], e->b[1]) / cell);
        int cz0 = (int)floorf(fminf(e->a[2], e->b[2]) / cell);
        int cz1 = (int)floorf(fmaxf(e->a[2], e->b[2]) / cell);

        for (int cz = cz0; cz <= cz1; cz++)
            for (int cy = cy0; cy <= cy1; cy++)
                for (int cx = cx0; cx <= cx1; cx++) {
                    int64_t *head = nm_map_slot(&grid, nm_cell3(cx, cy, cz));

                    gents[gent] = (struct nm_gent){ .idx = (int)i, .next = (int)*head };
                    *head = (int64_t)gent++;
                }
    }

    float step = nm->params.step_height;

    for (size_t i = 0; i < nr_bedges; i++) {
        const struct nm_bedge *e1 = &bedges[i];
        /* work in XZ: a small vertical step still stitches (stairs, ramps) */
        float d1x = e1->b[0] - e1->a[0], d1z = e1->b[2] - e1->a[2];
        float len1 = hypotf(d1x, d1z);

        if (len1 < NM_MIN_PORTAL)   continue;
        d1x /= len1;
        d1z /= len1;

        int cx0 = (int)floorf(fminf(e1->a[0], e1->b[0]) / cell);
        int cx1 = (int)floorf(fmaxf(e1->a[0], e1->b[0]) / cell);
        int cy0 = (int)floorf(fminf(e1->a[1], e1->b[1]) / cell);
        int cy1 = (int)floorf(fmaxf(e1->a[1], e1->b[1]) / cell);
        int cz0 = (int)floorf(fminf(e1->a[2], e1->b[2]) / cell);
        int cz1 = (int)floorf(fmaxf(e1->a[2], e1->b[2]) / cell);

        /* a step partner can sit in the neighbouring height cell */
        for (int cz = cz0; cz <= cz1; cz++)
            for (int cy = cy0 - 1; cy <= cy1 + 1; cy++)
                for (int cx = cx0; cx <= cx1; cx++)
                    for (int64_t g = nm_map_get(&grid, nm_cell3(cx, cy, cz)); g >= 0;
                         g = gents[g].next) {
                    size_t j = (size_t)gents[g].idx;

                    if (j <= i) continue;

                    const struct nm_bedge *e2 = &bedges[j];

                    if (e2->tri == e1->tri) continue;

                    /* an edge pair can meet in several cells: once is enough */
                    if (stamp[j] == (int)i) continue;
                    stamp[j] = (int)i;

                    float d2x = e2->b[0] - e2->a[0], d2z = e2->b[2] - e2->a[2];
                    float len2 = hypotf(d2x, d2z);

                    if (len2 < NM_MIN_PORTAL)   continue;

                    /* parallel in XZ? cross of (unit d1, d2) = len2 * sin(angle) */
                    if (fabsf(d1x * d2z - d1z * d2x) > 0.05f * len2)    continue;

                    /* e2's endpoints project onto e1's XZ line, close to it */
                    float ax = e2->a[0] - e1->a[0], az = e2->a[2] - e1->a[2];
                    float bx = e2->b[0] - e1->a[0], bz = e2->b[2] - e1->a[2];
                    float sa = ax * d1x + az * d1z;
                    float sb = bx * d1x + bz * d1z;

                    if (fabsf(ax * -d1z + az * d1x) > tol ||
                        fabsf(bx * -d1z + bz * d1x) > tol)
                        continue;

                    /* the overlapping interval along e1 */
                    float lo = fmaxf(0.0f, fminf(sa, sb));
                    float hi = fminf(len1, fmaxf(sa, sb));

                    if (hi - lo < NM_MIN_PORTAL)    continue;

                    /* the vertical gap at the overlap must be a climbable step */
                    float smid = 0.5f * (lo + hi);
                    float y1 = e1->a[1] + (e1->b[1] - e1->a[1]) * (smid / len1);
                    float mx = e1->a[0] + d1x * smid, mz = e1->a[2] + d1z * smid;
                    float s2 = clampf(((mx - e2->a[0]) * d2x + (mz - e2->a[2]) * d2z) / len2,
                                      0.0f, len2);
                    float y2 = e2->a[1] + (e2->b[1] - e2->a[1]) * (s2 / len2);

                    if (fabsf(y1 - y2) > step)  continue;

                    vec3 pa = { e1->a[0] + d1x * lo, fminf(y1, y2), e1->a[2] + d1z * lo };
                    vec3 pb = { e1->a[0] + d1x * hi, fminf(y1, y2), e1->a[2] + d1z * hi };

                    nm_rec_add(recs, e1->tri, e2->tri, pa, pb);
                    nm_rec_add(recs, e2->tri, e1->tri, pa, pb);
                    stitched++;
                }
    }

    nm_map_done(&grid);
    mem_free(stamp);
    mem_free(gents);

    return stitched;
}

/*
 * Link surfaces that stack rather than abut: a slope or platform resting
 * on a ground plane shares no edge with it, so nothing above joins them.
 * For each border edge, probe just past it, off the surface; if a
 * walkable triangle sits there within step_height (the ground under the
 * slope's lip, the tread the stairs land on), link across. This is the
 * step-off/step-onto an agent does at a surface boundary.
 */
static size_t nm_step_surface_links(navmesh *nm, const nav_tri *tris, size_t nr_tris,
                                    const struct nm_bedge *bedges, size_t nr_bedges,
                                    struct darray *recs)
{
    float cell = max(4.0f * nm->params.agent_radius, 2.0f);
    float probe = nm->params.agent_radius + 0.1f;
    float step = nm->params.step_height;
    size_t linked = 0;

    /* grid the walkable triangles by their XZ footprint */
    size_t nr_cells = 0;

    for (size_t t = 0; t < nr_tris; t++) {
        struct nm_ob box;

        nm_tri_aabb(nm, &tris[t], &box);
        /*
         * mirror the fill loop's signed int floors: negative cells would
         * otherwise convert out of range and mis-size the array
         */
        nr_cells += (size_t)((int)floorf(box.mx[0] / cell) - (int)floorf(box.mn[0] / cell) + 1) *
                    (size_t)((int)floorf(box.mx[2] / cell) - (int)floorf(box.mn[2] / cell) + 1);
    }

    struct nm_map grid;
    struct nm_gent *gents = mem_alloc(sizeof(*gents), .nr = nr_cells ? : 1, .fatal_fail = 1);
    size_t gent = 0;

    nm_map_init(&grid, nr_cells ? : 1);

    for (size_t t = 0; t < nr_tris; t++) {
        struct nm_ob box;

        nm_tri_aabb(nm, &tris[t], &box);
        for (int cz = (int)floorf(box.mn[2] / cell); cz <= (int)floorf(box.mx[2] / cell); cz++)
            for (int cx = (int)floorf(box.mn[0] / cell); cx <= (int)floorf(box.mx[0] / cell); cx++) {
                int64_t *head = nm_map_slot(&grid, nm_cell2(cx, cz));

                gents[gent] = (struct nm_gent){ .idx = (int)t, .next = (int)*head };
                *head = (int64_t)gent++;
            }
    }

    for (size_t i = 0; i < nr_bedges; i++) {
        const struct nm_bedge *e = &bedges[i];
        const nav_tri *t = &tris[e->tri];
        vec3 cen, mid, out;

        for (int k = 0; k < 3; k++) {
            cen[k] = (nm->verts[t->v[0]][k] + nm->verts[t->v[1]][k] + nm->verts[t->v[2]][k]) / 3.0f;
            mid[k] = 0.5f * (e->a[k] + e->b[k]);
        }

        /* outward: off the surface, away from its centroid, in XZ */
        out[0] = mid[0] - cen[0];
        out[2] = mid[2] - cen[2];
        float olen = hypotf(out[0], out[2]);
        if (olen < 1e-4f)   continue;

        float px = mid[0] + out[0] / olen * probe;
        float pz = mid[2] + out[2] / olen * probe;
        int gx = (int)floorf(px / cell), gz = (int)floorf(pz / cell);
        int best = -1;
        float best_dy = step;

        for (int64_t g = nm_map_get(&grid, nm_cell2(gx, gz)); g >= 0; g = gents[g].next) {
            int n = gents[g].idx;

            if (n == e->tri)    continue;

            bool inside;
            float y = nm_tri_height_at(nm, &tris[n], px, pz, &inside);

            if (inside && fabsf(y - mid[1]) < best_dy) {
                best_dy = fabsf(y - mid[1]);
                best = n;
            }
        }

        if (best >= 0) {
            nm_rec_add(recs, e->tri, best, e->a, e->b);
            nm_rec_add(recs, best, e->tri, e->a, e->b);
            linked++;
        }
    }

    nm_map_done(&grid);
    mem_free(gents);

    return linked;
}

/* emit one floor triangle (three world positions) into the soup */
static cerr nm_soup_tri(struct darray *soup, const vec3 a, const vec3 b, const vec3 c)
{
    size_t at = soup->nr_el;
    float *arr = _darray_resize(soup, at + 9);

    if (!arr)   return CERR_NOMEM;

    vec3_dup(&arr[at + 0], (float *)a);
    vec3_dup(&arr[at + 3], (float *)b);
    vec3_dup(&arr[at + 6], (float *)c);

    return CERR_OK;
}

/*
 * Split every floor triangle until no edge exceeds max_edge. Low-poly
 * ground comes as a few enormous quads; the clearance cull works per
 * triangle around its centroid and can only keep or drop a whole
 * triangle, so without this an obstacle sitting in the middle of a
 * giant quad carves nothing. Subdividing first gives the cull
 * something to bite.
 *
 * Longest-edge bisection, not a barycentric grid: a grid slices every
 * axis into the longest edge's count of pieces, so a long, thin
 * triangle (half of a stair tread: meters wide, a going deep) is
 * minced across its short axis into slivers finer than the weld
 * tolerance, and the welder then collapses them into holes and
 * severed seams. Bisection only ever cuts the long axis, and two
 * neighbours bisect a shared edge at the same midpoint, so the cut
 * welds back together; where their split orders disagree, the border
 * stitch reconnects the T-junction.
 */
#define NM_SUBDIV_DEPTH 32

static cerr nm_subdiv_tri(struct darray *fine, const float *a, const float *b,
                          const float *c, float max_edge, int depth)
{
    vec3 d[3], mid;
    float l2[3];

    vec3_sub(d[0], (float *)b, (float *)a);
    vec3_sub(d[1], (float *)c, (float *)b);
    vec3_sub(d[2], (float *)a, (float *)c);
    for (int e = 0; e < 3; e++) l2[e] = vec3_mul_inner(d[e], d[e]);

    int e = l2[1] > l2[0] ? (l2[2] > l2[1] ? 2 : 1) : (l2[2] > l2[0] ? 2 : 0);

    if (!depth || l2[e] <= max_edge * max_edge) return nm_soup_tri(fine, a, b, c);

    const float *p = e == 0 ? a : e == 1 ? b : c;
    const float *q = e == 0 ? b : e == 1 ? c : a;
    const float *o = e == 0 ? c : e == 1 ? a : b;

    /*
     * The midpoint from the endpoints, not the edge vector: addition
     * commutes, so the neighbour walking the shared edge the other way
     * computes the bitwise-same point and welding rejoins the cut.
     */
    for (int k = 0; k < 3; k++) mid[k] = (p[k] + q[k]) * 0.5f;

    CERR_RET_CERR(nm_subdiv_tri(fine, p, mid, o, max_edge, depth - 1));
    return nm_subdiv_tri(fine, mid, q, o, max_edge, depth - 1);
}

static cerr nm_subdivide(navmesh *nm)
{
    size_t nr_floor = darray_count(nm->floor_soup) / 9;
    float max_edge = nm->params.subdivide;
    darray(float, fine);

    darray_init(fine);

    for (size_t i = 0; i < nr_floor; i++) {
        const float *a = &nm->floor_soup.x[i * 9 + 0];
        const float *b = &nm->floor_soup.x[i * 9 + 3];
        const float *c = &nm->floor_soup.x[i * 9 + 6];

        CERR_RET(nm_subdiv_tri(&fine.da, a, b, c, max_edge, NM_SUBDIV_DEPTH),
                 { darray_clearout(fine); return __cerr; });
    }

    darray_clearout(nm->floor_soup);
    nm->floor_soup.da = fine.da;

    trace("subdivided floor: %zu -> %zu triangles at %.2f m\n",
          nr_floor, darray_count(nm->floor_soup) / 9, max_edge);

    return CERR_OK;
}

/* seed != nullptr: keep the seed's component; nullptr: keep the largest */
static cerr nm_bake(navmesh *nm, const float *seed)
{
    if (nm->baked)  return CERR_INVALID_OPERATION;

    if (nm->params.subdivide > 0.0f && darray_count(nm->floor_soup))    CERR_RET_CERR(nm_subdivide(nm));

    size_t nr_floor = darray_count(nm->floor_soup) / 9;
    size_t nr_hard = darray_count(nm->obstacles);

    if (!nr_floor)  return CERR_NOT_FOUND;

    /* weld floor corners into shared vertices */
    vec3 *verts = mem_alloc(sizeof(vec3), .nr = nr_floor * 3, .fatal_fail = 1);
    int *vnext = mem_alloc(sizeof(int), .nr = nr_floor * 3, .fatal_fail = 1);
    nav_tri *tris = mem_alloc(sizeof(nav_tri), .nr = nr_floor, .fatal_fail = 1);
    size_t nr_verts = 0, nr_tris = 0;
    struct nm_map cells;

    nm_map_init(&cells, nr_floor * 3);

    for (size_t i = 0; i < nr_floor; i++) {
        nav_tri *t = &tris[nr_tris];

        for (int c = 0; c < 3; c++)
            t->v[c] = nm_weld_vert(&cells, &verts, &vnext, &nr_verts,
                                   &nm->floor_soup.x[i * 9 + c * 3], nm->params.weld);
        t->link_first = 0;
        t->link_nr = 0;

        /* welding can collapse slivers; XZ-degenerate can't be walked */
        if (t->v[0] == t->v[1] || t->v[1] == t->v[2] || t->v[2] == t->v[0])         continue;
        if (fabs(nm_cross2(verts[t->v[0]], verts[t->v[1]], verts[t->v[2]])) < 1e-7) continue;

        nr_tris++;
    }

    nm_map_done(&cells);
    mem_free(vnext);

    if (!nr_tris) {
        mem_free(verts);
        mem_free(tris);
        return CERR_NOT_FOUND;
    }

    nm->verts = verts;
    nm->nr_verts = nr_verts;

    /* adjacency, first pass: exactly shared welded edges */
    struct nm_map edges;
    darray(struct nm_rec, recs);
    uint8_t *paired = mem_alloc(sizeof(*paired), .nr = nr_tris, .zero = 1, .fatal_fail = 1);

    darray_init(recs);
    nm_map_init(&edges, nr_tris * 3);

    for (size_t t = 0; t < nr_tris; t++)
        for (int e = 0; e < 3; e++) {
            uint64_t lo = min(tris[t].v[e], tris[t].v[(e + 1) % 3]);
            uint64_t hi = max(tris[t].v[e], tris[t].v[(e + 1) % 3]);
            int64_t *slot = nm_map_slot(&edges, lo << 32 | hi);

            if (*slot == -1) {
                *slot = (int64_t)(t << 2 | e);
            } else if (*slot >= 0) {
                size_t ot = (size_t)*slot >> 2;
                int oe = (int)(*slot & 3);
                float *pa = verts[tris[t].v[e]];
                float *pb = verts[tris[t].v[(e + 1) % 3]];

                nm_rec_add(&recs.da, (int)t, (int)ot, pa, pb);
                nm_rec_add(&recs.da, (int)ot, (int)t, pa, pb);
                paired[t] |= 1 << e;
                paired[ot] |= 1 << oe;
                *slot = -2; /* consumed; further tris on this edge are ignored */
            }
            /* a third triangle on this edge (*slot == -2): ignored */
        }

    nm_map_done(&edges);

    /* second pass: stitch the leftover borders across collinear overlaps */
    struct nm_bedge *bedges = mem_alloc(sizeof(*bedges), .nr = nr_tris * 3, .fatal_fail = 1);
    size_t nr_bedges = 0;

    for (size_t t = 0; t < nr_tris; t++)
        for (int e = 0; e < 3; e++) {
            if (paired[t] & (1 << e))   continue;

            struct nm_bedge *be = &bedges[nr_bedges++];

            be->tri = (int)t;
            vec3_dup(be->a, verts[tris[t].v[e]]);
            vec3_dup(be->b, verts[tris[t].v[(e + 1) % 3]]);
        }

    mem_free(paired);

    nm_stitch_borders(nm, bedges, nr_bedges, &recs.da);
    nm_step_surface_links(nm, tris, nr_tris, bedges, nr_bedges, &recs.da);

    mem_free(bedges);

    /* bucket the crossings by their triangle */
    size_t nr_recs = darray_count(recs);
    struct nav_link *all_links = mem_alloc(sizeof(*all_links), .nr = nr_recs ? : 1,
                                           .fatal_fail = 1);

    for (size_t i = 0; i < nr_recs; i++)    tris[recs.x[i].from].link_nr++;

    int run = 0;

    for (size_t t = 0; t < nr_tris; t++) {
        tris[t].link_first = run;
        run += tris[t].link_nr;
        tris[t].link_nr = 0;
    }

    for (size_t i = 0; i < nr_recs; i++) {
        nav_tri *t = &tris[recs.x[i].from];
        struct nav_link *l = &all_links[t->link_first + t->link_nr++];

        l->to = recs.x[i].to;
        vec3_dup(l->pa, recs.x[i].pa);
        vec3_dup(l->pb, recs.x[i].pb);
    }

    darray_clearout(recs);


    /* cull floors whose clearance column is obstructed */
    struct nm_ob *soft = mem_alloc(sizeof(*soft), .nr = nr_tris, .fatal_fail = 1);
    float cell = max(4.0f * nm->params.agent_radius, 2.0f);
    size_t nr_gents = 0;

    for (size_t t = 0; t < nr_tris; t++)    nm_tri_aabb(nm, &tris[t], &soft[t]);

    for (size_t i = 0; i < nr_hard + nr_tris; i++) {
        const struct nm_ob *ob = i < nr_hard ? &nm->obstacles.x[i] : &soft[i - nr_hard];
        int cx0 = (int)floorf(ob->mn[0] / cell), cx1 = (int)floorf(ob->mx[0] / cell);
        int cz0 = (int)floorf(ob->mn[2] / cell), cz1 = (int)floorf(ob->mx[2] / cell);

        nr_gents += (size_t)(cx1 - cx0 + 1) * (size_t)(cz1 - cz0 + 1);
    }

    struct nm_map grid;
    struct nm_gent *gents = mem_alloc(sizeof(*gents), .nr = nr_gents, .fatal_fail = 1);
    size_t gent = 0;

    nm_map_init(&grid, nr_gents);

    for (size_t i = 0; i < nr_hard + nr_tris; i++) {
        const struct nm_ob *ob = i < nr_hard ? &nm->obstacles.x[i] : &soft[i - nr_hard];

        for (int cz = (int)floorf(ob->mn[2] / cell); cz <= (int)floorf(ob->mx[2] / cell); cz++)
            for (int cx = (int)floorf(ob->mn[0] / cell); cx <= (int)floorf(ob->mx[0] / cell); cx++) {
                int64_t *head = nm_map_slot(&grid, nm_cell2(cx, cz));

                gents[gent] = (struct nm_gent){ .idx = (int)i, .next = (int)*head };
                *head = (int64_t)gent++;
            }
    }

    bool *keep = mem_alloc(sizeof(*keep), .nr = nr_tris, .fatal_fail = 1);
    size_t nr_blocked = 0;

    for (size_t t = 0; t < nr_tris; t++) {
        keep[t] = nm_tri_blocked(nm, (int)t, tris, soft, nr_tris, &grid, gents, cell) == INT_MIN;
        nr_blocked += !keep[t];
    }

    nm_map_done(&grid);
    mem_free(gents);
    mem_free(soft);

    /*
     * Pick the surviving component: the one under the seed point, or
     * the largest anywhere when the caller passes no seed (a scene
     * whose walkable ground is scattered and whose best floor the
     * caller can't name in advance).
     */
    int *comp = mem_alloc(sizeof(*comp), .nr = nr_tris, .fatal_fail = 1);
    int *bfs = mem_alloc(sizeof(*bfs), .nr = nr_tris, .fatal_fail = 1);
    int nr_comp = 0, biggest = -1;
    float biggest_area = -1.0f;

    for (size_t t = 0; t < nr_tris; t++)    comp[t] = -1;

    /*
     * Rank components by walkable AREA, not triangle count: authored
     * ground is low-poly, so the main floor can be a handful of huge
     * quads while a decorative nook is a dense mesh of tiny facets.
     * Picking by count would hand back the nook.
     */
    for (size_t t = 0; t < nr_tris; t++) {
        if (!keep[t] || comp[t] >= 0)   continue;

        size_t h = 0, tl = 0;
        float area = 0.0f;

        comp[t] = nr_comp;
        bfs[tl++] = (int)t;
        while (h < tl) {
            int c = bfs[h++];

            area += nm_tri_area3(verts, tris[c].v);
            for (int l = 0; l < tris[c].link_nr; l++) {
                int n = all_links[tris[c].link_first + l].to;

                if (keep[n] && comp[n] < 0) {
                    comp[n] = nr_comp;
                    bfs[tl++] = n;
                }
            }
        }

        if (area > biggest_area) {
            biggest_area = area;
            biggest = (int)t;
        }
        nr_comp++;
    }

    mem_free(bfs);

    int seed_tri = -1;

    if (seed) {
        float best_dy = FLT_MAX, best_dist = FLT_MAX;

        for (size_t t = 0; t < nr_tris; t++) {
            if (!keep[t])   continue;

            bool inside;
            float y = nm_tri_height_at(nm, &tris[t], seed[0], seed[2], &inside);

            if (inside && fabsf(y - seed[1]) < best_dy) {
                best_dy = fabsf(y - seed[1]);
                seed_tri = (int)t;
            }
        }

        if (seed_tri < 0)
            for (size_t t = 0; t < nr_tris; t++) {
                if (!keep[t])   continue;

                vec3 c, d;

                for (int i = 0; i < 3; i++)
                    c[i] = (verts[tris[t].v[0]][i] + verts[tris[t].v[1]][i] +
                            verts[tris[t].v[2]][i]) / 3.0f;
                vec3_sub(d, c, (float *)seed);
                d[1] = 0.0f;

                float dist = vec3_mul_inner(d, d);
                if (dist < best_dist) {
                    best_dist = dist;
                    seed_tri = (int)t;
                }
            }
    } else {
        seed_tri = biggest;
    }

    mem_free(comp);

    if (seed_tri < 0) {
        mem_free(keep);
        mem_free(tris);
        mem_free(all_links);
        mem_free(nm->verts);
        nm->verts = nullptr;
        return CERR_NOT_FOUND;
    }

    /* BFS the walkable component */
    int *queue = mem_alloc(sizeof(*queue), .nr = nr_tris, .fatal_fail = 1);
    int *remap = mem_alloc(sizeof(*remap), .nr = nr_tris, .fatal_fail = 1);
    int *inv = mem_alloc(sizeof(*inv), .nr = nr_tris, .fatal_fail = 1);
    size_t head = 0, tail = 0, nr_kept = 0;

    for (size_t t = 0; t < nr_tris; t++)    remap[t] = -1;

    remap[seed_tri] = 0;
    inv[0] = seed_tri;
    nr_kept = 1;
    queue[tail++] = seed_tri;

    while (head < tail) {
        int t = queue[head++];

        for (int l = 0; l < tris[t].link_nr; l++) {
            int n = all_links[tris[t].link_first + l].to;

            if (!keep[n] || remap[n] >= 0)  continue;
            remap[n] = (int)nr_kept;
            inv[nr_kept++] = n;
            queue[tail++] = n;
        }
    }

    mem_free(keep);
    mem_free(queue);

    /* compact triangles and their crossings */
    size_t nr_links = 0;

    for (size_t ni = 0; ni < nr_kept; ni++)
        for (int l = 0; l < tris[inv[ni]].link_nr; l++)
            nr_links += remap[all_links[tris[inv[ni]].link_first + l].to] >= 0;

    nm->tris = mem_alloc(sizeof(nav_tri), .nr = nr_kept, .fatal_fail = 1);
    nm->links = mem_alloc(sizeof(struct nav_link), .nr = nr_links ? : 1, .fatal_fail = 1);
    nm->cdf = mem_alloc(sizeof(float), .nr = nr_kept, .fatal_fail = 1);
    nm->nr_tris = nr_kept;
    nm->nr_links = nr_links;

    size_t at = 0;

    for (size_t ni = 0; ni < nr_kept; ni++) {
        nav_tri *src = &tris[inv[ni]];
        nav_tri *dst = &nm->tris[ni];

        for (int c = 0; c < 3; c++) dst->v[c] = src->v[c];
        dst->link_first = (int)at;

        for (int l = 0; l < src->link_nr; l++) {
            struct nav_link *link = &all_links[src->link_first + l];

            if (remap[link->to] < 0)    continue;

            nm->links[at] = *link;
            nm->links[at].to = remap[link->to];
            at++;
        }
        dst->link_nr = (int)(at - dst->link_first);
    }

    mem_free(remap);
    mem_free(inv);
    mem_free(tris);
    mem_free(all_links);

    /* area distribution for uniform sampling */
    float total = 0.0f;

    for (size_t t = 0; t < nr_kept; t++) {
        vec3 ab, ac, n;

        vec3_sub(ab, nm->verts[nm->tris[t].v[1]], nm->verts[nm->tris[t].v[0]]);
        vec3_sub(ac, nm->verts[nm->tris[t].v[2]], nm->verts[nm->tris[t].v[0]]);
        vec3_mul_cross(n, ab, ac);
        total += vec3_len(n) * 0.5f;
        nm->cdf[t] = total;
    }

    nm->total_area = total;

    /*
     * The locate grid: XZ cells chaining every triangle whose AABB
     * overlaps them, so navmesh_locate() off the hint ring walks one
     * cell instead of the whole mesh.
     */
    struct nm_ob box;
    float mnx = FLT_MAX, mnz = FLT_MAX, mxx = -FLT_MAX, mxz = -FLT_MAX;

    for (size_t t = 0; t < nr_kept; t++) {
        nm_tri_aabb(nm, &nm->tris[t], &box);
        mnx = fminf(mnx, box.mn[0]);
        mnz = fminf(mnz, box.mn[2]);
        mxx = fmaxf(mxx, box.mx[0]);
        mxz = fmaxf(mxz, box.mx[2]);
    }

    /*
     * The obstruction grid's cell default, doubled until the table
     * stays proportional to the mesh: a vast sparse world must not
     * conjure a vast dense grid.
     */
    float lcell = max(4.0f * nm->params.agent_radius, 2.0f);
    int nx = max(1, (int)ceilf((mxx - mnx) / lcell));
    int nz = max(1, (int)ceilf((mxz - mnz) / lcell));

    while ((size_t)nx * nz > 4 * nr_kept + 4096) {
        lcell *= 2.0f;
        nx = max(1, (int)ceilf((mxx - mnx) / lcell));
        nz = max(1, (int)ceilf((mxz - mnz) / lcell));
    }

    size_t nr_ents = 0;
    int cx0, cx1, cz0, cz1;

    for (size_t t = 0; t < nr_kept; t++) {
        nm_tri_aabb(nm, &nm->tris[t], &box);
        nm_loc_span(box.mn[0], box.mx[0], mnx, lcell, nx, &cx0, &cx1);
        nm_loc_span(box.mn[2], box.mx[2], mnz, lcell, nz, &cz0, &cz1);
        nr_ents += (size_t)(cx1 - cx0 + 1) * (size_t)(cz1 - cz0 + 1);
    }

    nm->loc_heads = mem_alloc(sizeof(*nm->loc_heads), .nr = (size_t)nx * nz,
                              .fatal_fail = 1);
    nm->loc_ents = mem_alloc(sizeof(*nm->loc_ents), .nr = nr_ents, .fatal_fail = 1);
    memset(nm->loc_heads, 0xff, (size_t)nx * nz * sizeof(*nm->loc_heads));

    size_t ent = 0;

    for (size_t t = 0; t < nr_kept; t++) {
        nm_tri_aabb(nm, &nm->tris[t], &box);
        nm_loc_span(box.mn[0], box.mx[0], mnx, lcell, nx, &cx0, &cx1);
        nm_loc_span(box.mn[2], box.mx[2], mnz, lcell, nz, &cz0, &cz1);

        for (int cz = cz0; cz <= cz1; cz++)
            for (int cx = cx0; cx <= cx1; cx++) {
                int *head = &nm->loc_heads[cz * nx + cx];

                nm->loc_ents[ent] = (struct nm_gent){ .idx = (int)t, .next = *head };
                *head = (int)ent++;
            }
    }

    nm->loc_cell = lcell;
    nm->loc_nx = nx;
    nm->loc_nz = nz;
    nm->loc_x0 = mnx;
    nm->loc_z0 = mnz;

    /* the A* scratch: nothing to clear per query, pf_gen stamps do it */
    nm->pf_g = mem_alloc(sizeof(*nm->pf_g), .nr = nr_kept, .fatal_fail = 1);
    nm->pf_parent = mem_alloc(sizeof(*nm->pf_parent), .nr = nr_kept, .fatal_fail = 1);
    nm->pf_parent_link = mem_alloc(sizeof(*nm->pf_parent_link), .nr = nr_kept,
                                   .fatal_fail = 1);
    nm->pf_seen = mem_alloc(sizeof(*nm->pf_seen), .nr = nr_kept, .zero = 1,
                            .fatal_fail = 1);
    nm->pf_closed = mem_alloc(sizeof(*nm->pf_closed), .nr = nr_kept, .zero = 1,
                              .fatal_fail = 1);
    nm->pf_heap = mem_alloc(sizeof(*nm->pf_heap), .nr = nr_links + 8, .fatal_fail = 1);

    trace("navmesh: %zu floor triangles in, %zu obstructed, %zu kept of the "
          "biggest of %d walkable regions, %.1f m2\n",
          nr_tris, nr_blocked, nr_kept, nr_comp, total);

    darray_clearout(nm->floor_soup);
    darray_clearout(nm->obstacles);
    nm->baked = true;

    return CERR_OK;
}

cerr navmesh_bake(navmesh *nm, const vec3 seed)
{
    return nm_bake(nm, seed);
}

cerr navmesh_bake_largest(navmesh *nm)
{
    return nm_bake(nm, nullptr);
}

/***
 * queries
 ***/

size_t navmesh_nr_tris(navmesh *nm)
{
    return nm->baked ? nm->nr_tris : 0;
}

cerr navmesh_tri(navmesh *nm, int tri, vec3 c[3])
{
    if (!nm->baked || tri < 0 || (size_t)tri >= nm->nr_tris)    return CERR_INVALID_ARGUMENTS;

    for (int i = 0; i < 3; i++) vec3_dup(c[i], nm->verts[nm->tris[tri].v[i]]);

    return CERR_OK;
}

float navmesh_height(navmesh *nm, int tri, float x, float z)
{
    if (!nm->baked || tri < 0 || (size_t)tri >= nm->nr_tris)    return 0.0f;

    return nm_tri_height_at(nm, &nm->tris[tri], x, z, nullptr);
}

cres(int) navmesh_raycast(navmesh *nm, const vec3 origin, const vec3 dir, vec3 hit)
{
    if (!nm->baked) return cres_error(int, CERR_INVALID_OPERATION);

    /* Moller-Trumbore against every walkable triangle, nearest wins */
    float best = FLT_MAX;
    int best_tri = -1;

    for (size_t t = 0; t < nm->nr_tris; t++) {
        float *a = nm->verts[nm->tris[t].v[0]];
        float *b = nm->verts[nm->tris[t].v[1]];
        float *c = nm->verts[nm->tris[t].v[2]];
        vec3 e1, e2, h, s, q;

        vec3_sub(e1, b, a);
        vec3_sub(e2, c, a);
        vec3_mul_cross(h, (float *)dir, e2);

        float det = vec3_mul_inner(e1, h);
        if (fabsf(det) < 1e-8f) continue;

        float inv = 1.0f / det;

        vec3_sub(s, (float *)origin, a);
        float u = inv * vec3_mul_inner(s, h);
        if (u < 0.0f || u > 1.0f)   continue;

        vec3_mul_cross(q, s, e1);
        float v = inv * vec3_mul_inner((float *)dir, q);
        if (v < 0.0f || u + v > 1.0f)   continue;

        float dist = inv * vec3_mul_inner(e2, q);
        if (dist > 1e-4f && dist < best) {
            best = dist;
            best_tri = (int)t;
        }
    }

    if (best_tri < 0)   return cres_error(int, CERR_NOT_FOUND);

    for (int i = 0; i < 3; i++) hit[i] = origin[i] + dir[i] * best;

    return cres_val(int, best_tri);
}

/* the best (x, z)-containing candidate so far, by vertical distance */
struct nm_pick {
    int     tri;
    float   dy;
};

static void nm_locate_try(navmesh *nm, int t, const vec3 p, float max_dy, struct nm_pick *pick)
{
    bool inside;
    float y = nm_tri_height_at(nm, &nm->tris[t], p[0], p[2], &inside);

    if (!inside)    return;

    float dy = fabsf(y - p[1]);
    if (dy <= max_dy && dy < pick->dy) {
        pick->dy = dy;
        pick->tri = t;
    }
}

#define NM_RING 24

cres(int) navmesh_locate(navmesh *nm, const vec3 p, float max_dy, int hint)
{
    if (!nm->baked) return cres_error(int, CERR_INVALID_OPERATION);

    if (max_dy <= 0.0f) max_dy = FLT_MAX;

    struct nm_pick pick = { .tri = -1, .dy = FLT_MAX };

    if (hint >= 0 && (size_t)hint < nm->nr_tris) {
        int ring[NM_RING];
        int nr_ring = 0, nr_first = 0;

        ring[nr_ring++] = hint;
        for (int l = 0; l < nm->tris[hint].link_nr && nr_ring < NM_RING; l++)
            ring[nr_ring++] = nm->links[nm->tris[hint].link_first + l].to;
        nr_first = nr_ring;

        for (int i = 1; i < nr_first && nr_ring < NM_RING; i++) {
            nav_tri *t = &nm->tris[ring[i]];

            for (int l = 0; l < t->link_nr && nr_ring < NM_RING; l++) {
                int n = nm->links[t->link_first + l].to;

                if (n != hint)  ring[nr_ring++] = n;
            }
        }

        for (int i = 0; i < nr_ring; i++)   nm_locate_try(nm, ring[i], p, max_dy, &pick);

        if (pick.tri >= 0)  return cres_val(int, pick.tri);
    }

    /*
     * The hint ring missed (or there was no hint): walk the point's
     * grid cell. Every triangle containing (x, z) has its AABB over
     * this cell, so the chain sees all the candidates the full scan
     * would.
     */
    int cx = (int)floorf((p[0] - nm->loc_x0) / nm->loc_cell);
    int cz = (int)floorf((p[2] - nm->loc_z0) / nm->loc_cell);

    cx = max(0, min(nm->loc_nx - 1, cx));
    cz = max(0, min(nm->loc_nz - 1, cz));

    for (int e = nm->loc_heads[cz * nm->loc_nx + cx]; e >= 0; e = nm->loc_ents[e].next)
        nm_locate_try(nm, nm->loc_ents[e].idx, p, max_dy, &pick);

    if (pick.tri < 0)   return cres_error(int, CERR_NOT_FOUND);

    return cres_val(int, pick.tri);
}

cres(int) navmesh_random_point(navmesh *nm, float r1, float r2, float r3, vec3 pt)
{
    if (!nm->baked || !nm->nr_tris) return cres_error(int, CERR_INVALID_OPERATION);

    float r = clampf(r1, 0.0f, 1.0f) * nm->total_area;
    size_t lo = 0, hi = nm->nr_tris - 1;

    while (lo < hi) {
        size_t mid = (lo + hi) / 2;

        if (nm->cdf[mid] < r)
            lo = mid + 1;
        else
            hi = mid;
    }

    /* uniform barycentric point */
    float s = sqrtf(clampf(r2, 0.0f, 1.0f));
    float la = 1.0f - s;
    float lb = s * (1.0f - clampf(r3, 0.0f, 1.0f));
    float lc = s * clampf(r3, 0.0f, 1.0f);
    nav_tri *t = &nm->tris[lo];

    for (int c = 0; c < 3; c++)
        pt[c] = la * nm->verts[t->v[0]][c] +
                lb * nm->verts[t->v[1]][c] +
                lc * nm->verts[t->v[2]][c];

    return cres_val(int, (int)lo);
}

/***
 * pathfinding: A* over triangle adjacency, then funnel straightening
 ***/

struct nm_heap {
    struct nm_hnode *nodes;
    size_t          nr;
    size_t          cap;
};

static void nm_heap_push(struct nm_heap *h, float f, int tri)
{
    if (h->nr >= h->cap)
        return; /* degrades the path, never the correctness */

    size_t i = h->nr++;

    h->nodes[i] = (struct nm_hnode){ .f = f, .tri = tri };
    while (i && h->nodes[(i - 1) / 2].f > h->nodes[i].f) {
        struct nm_hnode tmp = h->nodes[(i - 1) / 2];

        h->nodes[(i - 1) / 2] = h->nodes[i];
        h->nodes[i] = tmp;
        i = (i - 1) / 2;
    }
}

static int nm_heap_pop(struct nm_heap *h)
{
    if (!h->nr) return -1;

    int top = h->nodes[0].tri;

    h->nodes[0] = h->nodes[--h->nr];

    size_t i = 0;
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, smallest = i;

        if (l < h->nr && h->nodes[l].f < h->nodes[smallest].f)  smallest = l;
        if (r < h->nr && h->nodes[r].f < h->nodes[smallest].f)  smallest = r;
        if (smallest == i)                                      break;

        struct nm_hnode tmp = h->nodes[i];

        h->nodes[i] = h->nodes[smallest];
        h->nodes[smallest] = tmp;
        i = smallest;
    }

    return top;
}

/* emit a waypoint unless the buffer is full; repeats collapse */
static bool nm_emit(vec3 *waypoints, int nr_waypoints, int *nr, const vec3 p)
{
    if (*nr > 0) {
        float dx = waypoints[*nr - 1][0] - p[0];
        float dz = waypoints[*nr - 1][2] - p[2];

        if (dx * dx + dz * dz < 1e-10f) return true;
    }

    if (*nr >= nr_waypoints)    return false;

    vec3_dup(waypoints[(*nr)++], (float *)p);
    return true;
}

cres(int) navmesh_path(navmesh *nm, const vec3 from, const vec3 to, float radius,
                       vec3 *waypoints, int nr_waypoints)
{
    if (!nm->baked)                     return cres_error(int, CERR_INVALID_OPERATION);
    if (!waypoints || nr_waypoints < 2) return cres_error(int, CERR_INVALID_ARGUMENTS);

    if (radius <= 0.0f) radius = nm->params.agent_radius;

    cres(int) from_res = navmesh_locate(nm, from, 0.0f, -1);
    cres(int) to_res = navmesh_locate(nm, to, 0.0f, -1);

    if (IS_CERR(from_res) || IS_CERR(to_res))   return cres_error(int, CERR_NOT_FOUND);

    int from_tri = from_res.val, to_tri = to_res.val;
    vec3 start = { from[0], navmesh_height(nm, from_tri, from[0], from[2]), from[2] };
    vec3 goal = { to[0], navmesh_height(nm, to_tri, to[0], to[2]), to[2] };
    int nr = 0;

    if (from_tri == to_tri) {
        nm_emit(waypoints, nr_waypoints, &nr, start);
        nm_emit(waypoints, nr_waypoints, &nr, goal);
        return cres_val(int, nr);
    }

    /*
     * A wrapped stamp generation would alias stale entries; one clear
     * per 4 billion queries buys zero-cost per-call initialization.
     */
    if (!++nm->pf_gen) {
        memset(nm->pf_seen, 0, nm->nr_tris * sizeof(*nm->pf_seen));
        memset(nm->pf_closed, 0, nm->nr_tris * sizeof(*nm->pf_closed));
        nm->pf_gen = 1;
    }

    uint32_t gen = nm->pf_gen;
    float *g = nm->pf_g;
    int *parent = nm->pf_parent;
    int *parent_link = nm->pf_parent_link;
    struct nm_heap heap = {
        .nodes = nm->pf_heap,
        .cap = nm->nr_links + 8,
    };

    g[from_tri] = 0.0f;
    parent[from_tri] = -1;
    nm->pf_seen[from_tri] = gen;
    nm_heap_push(&heap, 0.0f, from_tri);

    while (heap.nr) {
        int cur = nm_heap_pop(&heap);

        if (nm->pf_closed[cur] == gen)  continue;
        nm->pf_closed[cur] = gen;

        if (cur == to_tri)  break;

        vec3 cc;

        nm_centroid(nm, cur, cc);

        for (int l = 0; l < nm->tris[cur].link_nr; l++) {
            int li = nm->tris[cur].link_first + l;
            int n = nm->links[li].to;

            if (nm->pf_closed[n] == gen)    continue;

            vec3 nc, d;

            nm_centroid(nm, n, nc);
            vec3_sub(d, nc, cc);

            float ng = g[cur] + vec3_len(d);
            if (nm->pf_seen[n] == gen && ng >= g[n])    continue;

            g[n] = ng;
            parent[n] = cur;
            parent_link[n] = li;
            nm->pf_seen[n] = gen;

            vec3_sub(d, goal, nc);
            nm_heap_push(&heap, ng + vec3_len(d), n);
        }
    }

    if (nm->pf_closed[to_tri] != gen)   return cres_error(int, CERR_NOT_FOUND);

    /* walk the parents back into a triangle chain */
    size_t chain_len = 0;

    for (int t = to_tri; t >= 0; t = parent[t]) chain_len++;

    int *chain = mem_alloc(sizeof(*chain), .nr = chain_len, .fatal_fail = 1);
    size_t ci = chain_len;

    for (int t = to_tri; t >= 0; t = parent[t]) chain[--ci] = t;

    /*
     * Portals between consecutive chain triangles, shrunk by the agent
     * radius so corners clear the walls; left/right by the travel
     * direction across the portal.
     */
    size_t nr_portals = chain_len - 1;
    vec3 *pleft = mem_alloc(sizeof(vec3), .nr = nr_portals + 1, .fatal_fail = 1);
    vec3 *pright = mem_alloc(sizeof(vec3), .nr = nr_portals + 1, .fatal_fail = 1);

    for (size_t i = 0; i < nr_portals; i++) {
        int cur = chain[i], next = chain[i + 1];
        struct nav_link *link = &nm->links[parent_link[next]];
        float *pa = link->pa;
        float *pb = link->pb;
        vec3 ca, cb, dir, mid;

        nm_centroid(nm, cur, ca);
        nm_centroid(nm, next, cb);
        vec3_sub(dir, cb, ca);
        vec3_add(mid, pa, pb);
        vec3_scale(mid, mid, 0.5f);

        /* cross2(dir, p - mid) > 0: p is on the left of the travel direction */
        float side = dir[0] * (pa[2] - mid[2]) - dir[2] * (pa[0] - mid[0]);
        float *left = side > 0.0f ? pa : pb;
        float *right = side > 0.0f ? pb : pa;

        vec3 lr;

        vec3_sub(lr, right, left);
        float len = sqrtf(lr[0] * lr[0] + lr[2] * lr[2]);
        float s = len > 1e-6f ? fminf(0.4f, radius / len) : 0.0f;

        for (int c = 0; c < 3; c++) {
            pleft[i][c] = left[c] + lr[c] * s;
            pright[i][c] = right[c] - lr[c] * s;
        }
    }

    /* the goal is the last, zero-width portal */
    vec3_dup(pleft[nr_portals], goal);
    vec3_dup(pright[nr_portals], goal);
    nr_portals++;

    /*
     * Funnel: walk the portals keeping the narrowing wedge (apex, left,
     * right); a new portal side sweeping past the opposite side turns
     * that side's point into a path corner and restarts the wedge
     * there. Sides here follow nm_cross2(): "c left of a->b" > 0.
     */
    vec3 apex, lp, rp;
    size_t li = 0, ri = 0;
    bool full = false;

    vec3_dup(apex, start);
    vec3_dup(lp, pleft[0]);
    vec3_dup(rp, pright[0]);

    nm_emit(waypoints, nr_waypoints, &nr, apex);

    for (size_t i = 1; i < nr_portals && !full; i++) {
        float *nl = pleft[i], *nr_ = pright[i];

        /* tighten the right side */
        if (nm_cross2(apex, rp, nr_) >= 0.0) {
            if (nm_cross2(apex, lp, nr_) <= 0.0) {
                vec3_dup(rp, nr_);
                ri = i;
            } else {
                /* right swept past left: corner at the left point */
                full = !nm_emit(waypoints, nr_waypoints, &nr, lp);
                vec3_dup(apex, lp);
                vec3_dup(rp, apex);
                ri = li;
                i = li;
                continue;
            }
        }

        /* tighten the left side */
        if (nm_cross2(apex, lp, nl) <= 0.0) {
            if (nm_cross2(apex, rp, nl) >= 0.0) {
                vec3_dup(lp, nl);
                li = i;
            } else {
                /* left swept past right: corner at the right point */
                full = !nm_emit(waypoints, nr_waypoints, &nr, rp);
                vec3_dup(apex, rp);
                vec3_dup(lp, apex);
                li = ri;
                i = ri;
                continue;
            }
        }
    }

    if (!full)  nm_emit(waypoints, nr_waypoints, &nr, goal);

    mem_free(pleft);
    mem_free(pright);
    mem_free(chain);

    return cres_val(int, nr);
}
