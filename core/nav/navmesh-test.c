// SPDX-License-Identifier: Apache-2.0
/*
 * Navmesh subsystem tests, built as the standalone nav_tests binary: it
 * links only the navmesh and the shared test harness (test_main() lives in
 * core/test-harness.c, declared in core/test.h), not the whole engine test
 * blob.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "linmath.h"
#include "memory.h"
#include "navmesh.h"
#include "util.h"
#include "test.h"

/*
 * The navmesh test scene: a 12x12 grid floor with a lava pool over
 * [1,3]x[1,3] and a wall along x = 6 with a doorway at z in [7, 11]; a
 * walkable ramp off the x = 12 edge, a too-steep strip north of it and
 * a disconnected island at x in [20, 24].
 */
struct nm_soup {
    float           *vx;
    unsigned short  *idx;
    size_t          nr_vx, nr_idx;
};

static void soup_quad(struct nm_soup *s, const vec3 a, const vec3 b, const vec3 c, const vec3 d)
{
    const float *corners[] = { a, b, c, d };
    unsigned short base = (unsigned short)s->nr_vx;

    for (int i = 0; i < 4; i++) {
        memcpy(&s->vx[s->nr_vx * 3], corners[i], sizeof(vec3));
        s->nr_vx++;
    }

    /* (a, c, b) and (a, d, c) face +y when abcd winds x then z */
    s->idx[s->nr_idx++] = base;
    s->idx[s->nr_idx++] = base + 2;
    s->idx[s->nr_idx++] = base + 1;
    s->idx[s->nr_idx++] = base;
    s->idx[s->nr_idx++] = base + 3;
    s->idx[s->nr_idx++] = base + 2;
}

/* a unit floor cell over [x,x+1]x[z,z+1], y0 at x rising to y1 at x+1 */
static void soup_cell(struct nm_soup *s, float x, float z, float y0, float y1)
{
    soup_quad(s,
              (vec3){ x,        y0, z },
              (vec3){ x + 1.0f, y1, z },
              (vec3){ x + 1.0f, y1, z + 1.0f },
              (vec3){ x,        y0, z + 1.0f });
}

static navmesh *navmesh_test_build(void)
{
    enum { MAX_QUADS = 256 };
    struct nm_soup s = {
        .vx  = mem_alloc(sizeof(float), .nr = MAX_QUADS * 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = MAX_QUADS * 6, .fatal_fail = 1),
    };
    struct nm_soup lava = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    navmesh *nm = nullptr;

    cresp(navmesh) res = navmesh_new(.agent_radius = 0.4f, .agent_height = 1.8f);
    if (IS_CERR(res))   goto out;

    for (int x = 0; x < 12; x++)
        for (int z = 0; z < 12; z++)    soup_cell(&s, x, z, 0.0f, 0.0f);

    /* the ramp: 0.25 rise per unit, walkable */
    for (int x = 12; x < 16; x++)
        for (int z = 4; z < 8; z++) soup_cell(&s, x, z, 0.25f * (x - 12), 0.25f * (x - 11));

    /* too steep to walk */
    for (int x = 12; x < 14; x++)
        for (int z = 8; z < 12; z++)    soup_cell(&s, x, z, 1.2f * (x - 12), 1.2f * (x - 11));

    /* the island: walkable, unreachable */
    for (int x = 20; x < 24; x++)
        for (int z = 0; z < 4; z++) soup_cell(&s, x, z, 0.0f, 0.0f);

    /* the wall, slope-classified into an obstruction */
    soup_quad(&s, (vec3){ 6, 0, 0 }, (vec3){ 6, 2.5f, 0 },
                  (vec3){ 6, 2.5f, 7 }, (vec3){ 6, 0, 7 });
    soup_quad(&s, (vec3){ 6, 0, 11 }, (vec3){ 6, 2.5f, 11 },
                  (vec3){ 6, 2.5f, 12 }, (vec3){ 6, 0, 12 });

    soup_quad(&lava, (vec3){ 1, 0.02f, 1 }, (vec3){ 3, 0.02f, 1 },
                     (vec3){ 3, 0.02f, 3 }, (vec3){ 1, 0.02f, 3 });

    cerr err = navmesh_add(res.val, .vx = s.vx, .vxsz = s.nr_vx * 3 * sizeof(float),
                           .idx = s.idx, .idxsz = s.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))   goto out_free;

    err = navmesh_add(res.val, .vx = lava.vx, .vxsz = lava.nr_vx * 3 * sizeof(float),
                      .idx = lava.idx, .idxsz = lava.nr_idx * sizeof(unsigned short),
                      .obstacle = true);
    if (IS_CERR(err))   goto out_free;

    err = navmesh_bake(res.val, (vec3){ 4.5f, 0.0f, 4.5f });
    if (IS_CERR(err))   goto out_free;

    nm = res.val;
    res.val = nullptr;

out_free:
    if (res.val)    navmesh_free(res.val);
out:
    mem_free(s.vx);
    mem_free(s.idx);
    mem_free(lava.vx);
    mem_free(lava.idx);

    return nm;
}

static int navmesh_test_bake(void)
{
    navmesh *nm = navmesh_test_build();

    if (!nm)                                    return EXIT_FAILURE;

    /*
     * 352 floor triangles went in (grid, ramp, island); the island,
     * the lava cover, the wall bands and the steep strip's margin must
     * come out, but most of the field stays
     */
    size_t nr = navmesh_nr_tris(nm);
    int ret = nr > 150 && nr < 330 ? EXIT_SUCCESS : EXIT_FAILURE;

    navmesh_free(nm);
    return ret;
}

static int navmesh_test_locate(void)
{
    navmesh *nm = navmesh_test_build();
    int ret = EXIT_FAILURE;

    if (!nm)                                    return EXIT_FAILURE;

    /* open floor; the point is interior to one triangle */
    cres(int) tri = navmesh_locate(nm, (vec3){ 3.4f, 0.0f, 6.2f }, 0.5f, -1);
    if (IS_CERR(tri))                                           goto out;
    if (fabsf(navmesh_height(nm, tri.val, 3.4f, 6.2f)) > 1e-3f) goto out;

    /* the same spot, hinted from the next cell over */
    cres(int) near = navmesh_locate(nm, (vec3){ 2.5f, 0.0f, 6.2f }, 0.5f, -1);
    if (IS_CERR(near))                          goto out;
    cres(int) hinted = navmesh_locate(nm, (vec3){ 3.4f, 0.0f, 6.2f }, 0.5f, near.val);
    if (IS_CERR(hinted) || hinted.val != tri.val)   goto out;

    /* the ramp is walkable and reports its slope height */
    cres(int) ramp = navmesh_locate(nm, (vec3){ 14.5f, 0.625f, 6.5f }, 0.5f, -1);
    if (IS_CERR(ramp))                                                      goto out;
    if (fabsf(navmesh_height(nm, ramp.val, 14.5f, 6.5f) - 0.625f) > 1e-3f)  goto out;

    /* the steep strip, the lava cover, the island and the walled band are not */
    cres(int) bad = navmesh_locate(nm, (vec3){ 13.0f, 1.2f, 10.0f }, 3.0f, -1);
    if (!IS_CERR(bad))                          goto out;
    bad = navmesh_locate(nm, (vec3){ 2.0f, 0.0f, 2.0f }, 0.5f, -1);
    if (!IS_CERR(bad))                          goto out;
    bad = navmesh_locate(nm, (vec3){ 22.0f, 0.0f, 2.0f }, 0.5f, -1);
    if (!IS_CERR(bad))                          goto out;
    bad = navmesh_locate(nm, (vec3){ 5.8f, 0.0f, 3.5f }, 0.5f, -1);
    if (!IS_CERR(bad))                          goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    return ret;
}

static int navmesh_test_random(void)
{
    navmesh *nm = navmesh_test_build();
    int ret = EXIT_FAILURE;
    unsigned int lcg = 1;

    if (!nm)                                    return EXIT_FAILURE;

    for (int i = 0; i < 200; i++) {
        float r[3];
        vec3 pt;

        for (int c = 0; c < 3; c++) {
            lcg = lcg * 1103515245u + 12345u;
            r[c] = (float)(lcg >> 8 & 0xffffff) / (float)0x1000000;
        }

        int tri = CRES_RET(navmesh_random_point(nm, r[0], r[1], r[2], pt), goto out);

        /* on the kept component, never in lava, never on the island */
        cres(int) at = navmesh_locate(nm, pt, 0.1f, tri);
        if (IS_CERR(at))                                                    goto out;
        if (pt[0] > 1.0f && pt[0] < 3.0f && pt[2] > 1.0f && pt[2] < 3.0f)   goto out;
        if (pt[0] > 19.0f)                                                  goto out;
    }

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    return ret;
}

static int navmesh_test_path(void)
{
    navmesh *nm = navmesh_test_build();
    int ret = EXIT_FAILURE;
    vec3 wp[16];

    if (!nm)                                    return EXIT_FAILURE;

    /* within one triangle: straight line */
    cres(int) nr = navmesh_path(nm, (vec3){ 4.2f, 0, 4.2f }, (vec3){ 4.6f, 0, 4.6f }, wp, 16);
    if (IS_CERR(nr) || nr.val != 2)             goto out;

    /* across the wall: through the doorway, never through the wall */
    vec3 from = { 2.5f, 0.0f, 9.0f }, to = { 10.5f, 0.0f, 2.5f };

    nr = navmesh_path(nm, from, to, wp, 16);
    if (IS_CERR(nr) || nr.val < 3)              goto out;

    vec3 d;

    vec3_sub(d, wp[0], from);
    if (vec3_len(d) > 1e-3f)                    goto out;
    vec3_sub(d, wp[nr.val - 1], to);
    if (vec3_len(d) > 1e-3f)                    goto out;

    for (int i = 0; i + 1 < nr.val; i++) {
        /* waypoints stay on the mesh */
        cres(int) at = navmesh_locate(nm, wp[i], 0.2f, -1);

        if (IS_CERR(at))                        goto out;

        /* segments crossing the wall line do it inside the doorway */
        float a = wp[i][0] - 6.0f, b = wp[i + 1][0] - 6.0f;

        if (a * b < 0.0f) {
            float zc = wp[i][2] + (wp[i + 1][2] - wp[i][2]) * (-a / (b - a));

            if (zc < 7.4f || zc > 10.6f)        goto out;
        }
    }

    /* a two-slot buffer truncates honestly */
    nr = navmesh_path(nm, from, to, wp, 2);
    if (IS_CERR(nr) || nr.val != 2)             goto out;
    vec3_sub(d, wp[1], to);
    if (vec3_len(d) < 0.5f)                     goto out;

    /* the island and points off the mesh are unreachable */
    nr = navmesh_path(nm, from, (vec3){ 22.0f, 0.0f, 2.0f }, wp, 16);
    if (!IS_CERR(nr))                           goto out;
    nr = navmesh_path(nm, from, (vec3){ -5.0f, 0.0f, -5.0f }, wp, 16);
    if (!IS_CERR(nr))                           goto out;

    /* up the ramp: waypoints climb */
    nr = navmesh_path(nm, (vec3){ 10.5f, 0.0f, 6.0f }, (vec3){ 15.5f, 0.875f, 6.0f }, wp, 16);
    if (IS_CERR(nr) || nr.val < 2)                  goto out;
    if (fabsf(wp[nr.val - 1][1] - 0.875f) > 1e-3f)  goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    return ret;
}

static int navmesh_test_tjunction(void)
{
    /*
     * One big 2x2 cell on the left, four 1x1 cells on the right: the
     * border at x = 2 is a T-junction (the big edge meets two small
     * ones at a midpoint vertex), so exact edge matching can't connect
     * them; border stitching must
     */
    struct nm_soup s = {
        .vx  = mem_alloc(sizeof(float), .nr = 8 * 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 8 * 6, .fatal_fail = 1),
    };
    int ret = EXIT_FAILURE;
    navmesh *nm = nullptr;

    cresp(navmesh) res = navmesh_new(.agent_radius = 0.2f, .agent_height = 1.0f);
    if (IS_CERR(res))   goto out;
    nm = res.val;

    soup_quad(&s, (vec3){ 0, 0, 0 }, (vec3){ 2, 0, 0 },
                  (vec3){ 2, 0, 2 }, (vec3){ 0, 0, 2 });
    for (int x = 2; x < 4; x++)
        for (int z = 0; z < 2; z++) soup_cell(&s, x, z, 0.0f, 0.0f);

    cerr err = navmesh_add(nm, .vx = s.vx, .vxsz = s.nr_vx * 3 * sizeof(float),
                           .idx = s.idx, .idxsz = s.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_bake(nm, (vec3){ 1.0f, 0.0f, 1.0f });
    if (IS_CERR(err))                           goto out;

    /* every triangle survives, connected across the junction */
    if (navmesh_nr_tris(nm) != 10)              goto out;

    cres(int) tri = navmesh_locate(nm, (vec3){ 3.5f, 0.0f, 1.5f }, 0.5f, -1);
    if (IS_CERR(tri))                           goto out;

    vec3 wp[8];
    cres(int) nr = navmesh_path(nm, (vec3){ 0.5f, 0.0f, 1.0f },
                                (vec3){ 3.5f, 0.0f, 1.5f }, wp, 8);
    if (IS_CERR(nr) || nr.val < 2)              goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    mem_free(s.vx);
    mem_free(s.idx);
    return ret;
}

static int navmesh_test_carve(void)
{
    /*
     * One big 20x20 floor quad with an obstacle over its middle. At full
     * size the clearance cull can only keep or drop the whole quad;
     * subdivision lets it carve a hole under the obstacle, and a path
     * across must route around that hole.
     */
    struct nm_soup floor = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    struct nm_soup obs = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    int ret = EXIT_FAILURE;
    navmesh *nm = nullptr;

    cresp(navmesh) res = navmesh_new(.agent_radius = 0.4f, .agent_height = 1.8f,
                                     .subdivide = 1.0f);
    if (IS_CERR(res))   goto out;
    nm = res.val;

    soup_quad(&floor, (vec3){ 0, 0, 0 }, (vec3){ 20, 0, 0 },
                      (vec3){ 20, 0, 20 }, (vec3){ 0, 0, 20 });
    /* an obstacle hovering a bit above the floor, over (8,8)-(12,12) */
    soup_quad(&obs, (vec3){ 8, 0.5f, 8 }, (vec3){ 12, 0.5f, 8 },
                    (vec3){ 12, 0.5f, 12 }, (vec3){ 8, 0.5f, 12 });

    cerr err = navmesh_add(nm, .vx = floor.vx, .vxsz = floor.nr_vx * 3 * sizeof(float),
                           .idx = floor.idx, .idxsz = floor.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_add(nm, .vx = obs.vx, .vxsz = obs.nr_vx * 3 * sizeof(float),
                      .idx = obs.idx, .idxsz = obs.nr_idx * sizeof(unsigned short),
                      .obstacle = true);
    if (IS_CERR(err))                           goto out;
    err = navmesh_bake_largest(nm);
    if (IS_CERR(err))                           goto out;

    /* the middle is carved away, the corners survive */
    cres(int) hole = navmesh_locate(nm, (vec3){ 10, 0, 10 }, 0.5f, -1);
    if (!IS_CERR(hole))                         goto out;
    cres(int) corner = navmesh_locate(nm, (vec3){ 2, 0, 2 }, 0.5f, -1);
    if (IS_CERR(corner))                        goto out;

    /* a crossing that would pass through the hole must bend around it */
    vec3 wp[24];
    cres(int) nr = navmesh_path(nm, (vec3){ 2, 0, 10 }, (vec3){ 18, 0, 10 }, wp, 24);
    if (IS_CERR(nr) || nr.val <= 2)             goto out;

    /* no waypoint sits inside the carved footprint */
    for (int i = 0; i < nr.val; i++)
        if (wp[i][0] > 8.0f && wp[i][0] < 12.0f && wp[i][2] > 8.0f && wp[i][2] < 12.0f) goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    mem_free(floor.vx);
    mem_free(floor.idx);
    mem_free(obs.vx);
    mem_free(obs.idx);
    return ret;
}

static int navmesh_test_step(void)
{
    /*
     * A raised platform sitting on a ground plane, its top a step_height
     * above it: the two share no edge (the platform stacks on the
     * ground), so only edge-to-surface step links can join them. A path
     * from the ground onto the platform must exist and climb.
     */
    struct nm_soup ground = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    struct nm_soup plat = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    int ret = EXIT_FAILURE;
    navmesh *nm = nullptr;

    cresp(navmesh) res = navmesh_new(.agent_radius = 0.4f, .agent_height = 1.8f,
                                     .step_height = 0.5f, .subdivide = 1.0f);
    if (IS_CERR(res))   goto out;
    nm = res.val;

    soup_quad(&ground, (vec3){ 0, 0, 0 }, (vec3){ 20, 0, 0 },
                       (vec3){ 20, 0, 20 }, (vec3){ 0, 0, 20 });
    /* an 8x8 platform 0.4 up, resting over the middle of the ground */
    soup_quad(&plat, (vec3){ 6, 0.4f, 6 }, (vec3){ 14, 0.4f, 6 },
                     (vec3){ 14, 0.4f, 14 }, (vec3){ 6, 0.4f, 14 });

    cerr err = navmesh_add(nm, .vx = ground.vx, .vxsz = ground.nr_vx * 3 * sizeof(float),
                           .idx = ground.idx, .idxsz = ground.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_add(nm, .vx = plat.vx, .vxsz = plat.nr_vx * 3 * sizeof(float),
                      .idx = plat.idx, .idxsz = plat.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_bake_largest(nm);
    if (IS_CERR(err))                           goto out;

    /* the platform top is walkable and reports its raised height */
    cres(int) top = navmesh_locate(nm, (vec3){ 10, 0.4f, 10 }, 0.2f, -1);
    if (IS_CERR(top))                                                       goto out;
    if (fabsf(navmesh_height(nm, top.val, 10.0f, 10.0f) - 0.4f) > 0.05f)    goto out;

    /* a path from the ground onto the platform climbs to the top */
    vec3 wp[24];
    cres(int) nr = navmesh_path(nm, (vec3){ 2, 0, 2 }, (vec3){ 10, 0.4f, 10 }, wp, 24);
    if (IS_CERR(nr) || nr.val < 2)                  goto out;
    if (fabsf(wp[nr.val - 1][1] - 0.4f) > 0.05f)    goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    mem_free(ground.vx);
    mem_free(ground.idx);
    mem_free(plat.vx);
    mem_free(plat.idx);
    return ret;
}

static int navmesh_test_step_negative(void)
{
    /*
     * The step test, translated to straddle the origin so the grid cell
     * coordinates go negative. nm_step_surface_links() must size its cell
     * array the same way it fills it for negative floors, or it overruns.
     */
    struct nm_soup ground = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    struct nm_soup plat = {
        .vx  = mem_alloc(sizeof(float), .nr = 12, .fatal_fail = 1),
        .idx = mem_alloc(sizeof(unsigned short), .nr = 6, .fatal_fail = 1),
    };
    int ret = EXIT_FAILURE;
    navmesh *nm = nullptr;

    cresp(navmesh) res = navmesh_new(.agent_radius = 0.4f, .agent_height = 1.8f,
                                     .step_height = 0.5f, .subdivide = 1.0f);
    if (IS_CERR(res))   goto out;
    nm = res.val;

    /* the step scene shifted by (-30, -30) in XZ: all cells are negative */
    soup_quad(&ground, (vec3){ -30, 0, -30 }, (vec3){ -10, 0, -30 },
                       (vec3){ -10, 0, -10 }, (vec3){ -30, 0, -10 });
    soup_quad(&plat, (vec3){ -24, 0.4f, -24 }, (vec3){ -16, 0.4f, -24 },
                     (vec3){ -16, 0.4f, -16 }, (vec3){ -24, 0.4f, -16 });

    cerr err = navmesh_add(nm, .vx = ground.vx, .vxsz = ground.nr_vx * 3 * sizeof(float),
                           .idx = ground.idx, .idxsz = ground.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_add(nm, .vx = plat.vx, .vxsz = plat.nr_vx * 3 * sizeof(float),
                      .idx = plat.idx, .idxsz = plat.nr_idx * sizeof(unsigned short));
    if (IS_CERR(err))                           goto out;
    err = navmesh_bake_largest(nm);
    if (IS_CERR(err))                           goto out;

    /* a path from the ground onto the platform climbs to the top */
    vec3 wp[24];
    cres(int) nr = navmesh_path(nm, (vec3){ -28, 0, -28 }, (vec3){ -20, 0.4f, -20 }, wp, 24);
    if (IS_CERR(nr) || nr.val < 2)                  goto out;
    if (fabsf(wp[nr.val - 1][1] - 0.4f) > 0.05f)    goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    mem_free(ground.vx);
    mem_free(ground.idx);
    mem_free(plat.vx);
    mem_free(plat.idx);
    return ret;
}

static int navmesh_test_invalid(void)
{
    navmesh *nm = navmesh_test_build();
    int ret = EXIT_FAILURE;
    float vx[9] = { 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    unsigned short idx[3] = { 0, 1, 2 };
    vec3 wp[4];

    if (!nm)                                    return EXIT_FAILURE;

    /* no adding or rebaking once baked */
    cerr err = navmesh_add(nm, .vx = vx, .vxsz = sizeof(vx),
                           .idx = idx, .idxsz = sizeof(idx));
    if (!IS_CERR(err))                          goto out;
    err = navmesh_bake(nm, (vec3){ 0, 0, 0 });
    if (!IS_CERR(err))                          goto out;

    /* a path needs somewhere to put itself */
    cres(int) nr = navmesh_path(nm, (vec3){ 4, 0, 4 }, (vec3){ 5, 0, 5 }, wp, 1);
    if (!IS_CERR(nr))                           goto out;

    /* out-of-range indices are rejected */
    cresp(navmesh) fresh = navmesh_new();
    if (IS_CERR(fresh))                         goto out;

    unsigned short bad_idx[3] = { 0, 1, 7 };

    err = navmesh_add(fresh.val, .vx = vx, .vxsz = sizeof(vx),
                      .idx = bad_idx, .idxsz = sizeof(bad_idx));
    navmesh_free(fresh.val);
    if (!IS_CERR(err))                          goto out;

    ret = EXIT_SUCCESS;
out:
    navmesh_free(nm);
    return ret;
}

static const struct test tests[] = {
    { .name = "navmesh bake",       .test = navmesh_test_bake },
    { .name = "navmesh locate",     .test = navmesh_test_locate },
    { .name = "navmesh random",     .test = navmesh_test_random },
    { .name = "navmesh path",       .test = navmesh_test_path },
    { .name = "navmesh t-junction", .test = navmesh_test_tjunction },
    { .name = "navmesh carve",      .test = navmesh_test_carve },
    { .name = "navmesh step",       .test = navmesh_test_step },
    { .name = "navmesh step -ve",   .test = navmesh_test_step_negative },
    { .name = "navmesh invalid",    .test = navmesh_test_invalid },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
