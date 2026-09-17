// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "fracture.h"
#include "memory.h"
#include "mesh.h"
#include "prng.h"
#include "test.h"
#include "util.h"

/*
 * Fracture tests: closed sources come out as closed fragments whose
 * volumes add up, sheets stay sheets, and the parameter checks hold.
 */

/* an axis-aligned closed box as a triangle soup with UVs and normals */
static cresp(mesh) fracture_test_box(const char *name, const vec3 lo, const vec3 hi)
{
    LOCAL_SET(mesh_t, m) = CRES_RET_T(ref_new_checked(mesh, .name = name), mesh);

    CERR_RET_T(mesh_attr_alloc(m, MESH_VX, sizeof(float) * 3, 36), mesh);
    CERR_RET_T(mesh_attr_alloc(m, MESH_TX, sizeof(float) * 2, 36), mesh);
    CERR_RET_T(mesh_attr_alloc(m, MESH_NORM, sizeof(float) * 3, 36), mesh);
    CERR_RET_T(mesh_attr_alloc(m, MESH_IDX, sizeof(unsigned short), 36), mesh);

    size_t nr = 0;

    for (int axis = 0; axis < 3; axis++)
        for (int sign = -1; sign <= 1; sign += 2) {
            int b = (axis + 1) % 3, c = (axis + 2) % 3;
            /* a cycle around the face; the winding is fixed below */
            static const int cyc[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
            vec3 q[4], n = {}, e1, e2, fn;

            n[axis] = sign;
            for (int k = 0; k < 4; k++) {
                q[k][axis] = sign < 0 ? lo[axis] : hi[axis];
                q[k][b] = cyc[k][0] ? hi[b] : lo[b];
                q[k][c] = cyc[k][1] ? hi[c] : lo[c];
            }
            vec3_sub(e1, q[1], q[0]);
            vec3_sub(e2, q[2], q[0]);
            vec3_mul_cross(fn, e1, e2);
            if (vec3_mul_inner(fn, n) < 0.0f) {
                vec3 t;

                vec3_dup(t, q[1]); vec3_dup(q[1], q[3]); vec3_dup(q[3], t);
            }

            static const int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
            for (int t = 0; t < 2; t++)
                for (int k = 0; k < 3; k++, nr++) {
                    const float *p = q[tri[t][k]];

                    vec3_dup(&mesh_vx(m)[nr * 3], p);
                    mesh_tx(m)[nr * 2 + 0] = (p[b] - lo[b]) / (hi[b] - lo[b]);
                    mesh_tx(m)[nr * 2 + 1] = (p[c] - lo[c]) / (hi[c] - lo[c]);
                    vec3_dup(&mesh_norm(m)[nr * 3], n);
                    mesh_idx(m)[nr] = nr;
                }
        }

    for (enum mesh_attrs a = MESH_VX; a <= MESH_NORM; a++)  mesh_attr(m, a)->nr = nr;
    mesh_attr(m, MESH_IDX)->nr = nr;
    mesh_aabb_calc(m);

    return cresp_val(mesh, NOCU(m));
}

/* signed volume by tetrahedra from the origin: positive for outward CCW */
static double fracture_test_volume(struct mesh *m)
{
    double vol = 0.0;

    for (size_t i = 0; i < mesh_nr_idx(m); i += 3) {
        const float *p0 = &mesh_vx(m)[mesh_idx(m)[i + 0] * 3];
        const float *p1 = &mesh_vx(m)[mesh_idx(m)[i + 1] * 3];
        const float *p2 = &mesh_vx(m)[mesh_idx(m)[i + 2] * 3];
        vec3 cross;

        vec3_mul_cross(cross, p1, p2);
        vol += vec3_mul_inner(p0, cross) / 6.0;
    }

    return vol;
}

static double fracture_test_area(struct mesh *m)
{
    double area = 0.0;

    for (size_t i = 0; i < mesh_nr_idx(m); i += 3) {
        const float *p0 = &mesh_vx(m)[mesh_idx(m)[i + 0] * 3];
        const float *p1 = &mesh_vx(m)[mesh_idx(m)[i + 1] * 3];
        const float *p2 = &mesh_vx(m)[mesh_idx(m)[i + 2] * 3];
        vec3 e1, e2, cross;

        vec3_sub(e1, p1, p0);
        vec3_sub(e2, p2, p0);
        vec3_mul_cross(cross, e1, e2);
        area += vec3_len(cross) / 2.0;
    }

    return area;
}

struct fracture_test_edge {
    unsigned int    lo;
    unsigned int    hi;
    int             dir;
};

static int fracture_test_edge_cmp(const void *_a, const void *_b)
{
    const struct fracture_test_edge *a = _a, *b = _b;

    if (a->lo != b->lo)     return a->lo < b->lo ? -1 : 1;
    if (a->hi != b->hi)     return a->hi < b->hi ? -1 : 1;
    return 0;
}

/*
 * A closed manifold: after welding positions, every edge is shared by
 * exactly two triangles running it in opposite directions.
 */
static bool fracture_test_closed(struct mesh *m)
{
    size_t nr_vx = mesh_nr_vx(m), nr_idx = mesh_nr_idx(m);
    LOCAL(void, weld_buf);
    LOCAL(void, edge_buf);
    unsigned int *weld = weld_buf = mem_alloc(sizeof(*weld), .nr = nr_vx);
    struct fracture_test_edge *edges = edge_buf = mem_alloc(sizeof(*edges), .nr = nr_idx);
    size_t nr_pts = 0;

    if (!weld || !edges || nr_idx % 3)  return false;

    for (size_t i = 0; i < nr_vx; i++) {
        const float *p = &mesh_vx(m)[i * 3];
        size_t j;

        for (j = 0; j < i; j++) {
            vec3 d;

            vec3_sub(d, p, &mesh_vx(m)[j * 3]);
            if (vec3_len(d) < 1e-5f)    break;
        }
        weld[i] = j < i ? weld[j] : nr_pts++;
    }

    size_t nr_edges = 0;

    for (size_t i = 0; i < nr_idx; i += 3) {
        unsigned int w[3];

        for (int k = 0; k < 3; k++) w[k] = weld[mesh_idx(m)[i + k]];

        /* a sliver thinner than the weld: nothing left of it to check */
        if (w[0] == w[1] || w[1] == w[2] || w[2] == w[0])   continue;

        for (int k = 0; k < 3; k++, nr_edges++) {
            unsigned int a = w[k], b = w[(k + 1) % 3];

            edges[nr_edges].lo = min(a, b);
            edges[nr_edges].hi = max(a, b);
            edges[nr_edges].dir = a < b ? 1 : -1;
        }
    }

    qsort(edges, nr_edges, sizeof(*edges), fracture_test_edge_cmp);

    for (size_t i = 0; i < nr_edges; i += 2) {
        if (i + 1 >= nr_edges)                                                      return false;
        if (fracture_test_edge_cmp(&edges[i], &edges[i + 1]))                       return false;
        if (edges[i].dir + edges[i + 1].dir)                                        return false;
        if (i + 2 < nr_edges && !fracture_test_edge_cmp(&edges[i], &edges[i + 2]))  return false;
    }

    return true;
}

static bool fracture_test_near(double a, double b, double tol)
{
    return fabs(a - b) <= tol;
}

/* count the vertices whose normal is @n */
static size_t fracture_test_count_normal(struct mesh *m, const vec3 n)
{
    size_t count = 0;

    for (size_t i = 0; i < mesh_nr_norm(m); i++) {
        vec3 d;

        vec3_sub(d, &mesh_norm(m)[i * 3], n);
        if (vec3_len(d) < 1e-5f)    count++;
    }

    return count;
}

static int fracture_halves_check(fracture_fragments *ff)
{
    if (ff->nr != 2)                            return EXIT_FAILURE;

    for (size_t i = 0; i < ff->nr; i++) {
        fracture_fragment *f = &ff->fragments[i];
        float sign = i ? 1.0f : -1.0f;
        /* the cap faces the other half */
        vec3 cap_n = { -sign, 0.0f, 0.0f };

        if (!fracture_test_near(f->volume, 0.5, 1e-5))                          return EXIT_FAILURE;
        if (!fracture_test_near(f->center[0], sign * 0.25, 1e-5))               return EXIT_FAILURE;
        if (!fracture_test_near(f->center[1], 0.0, 1e-5))                       return EXIT_FAILURE;
        if (!fracture_test_near(f->center[2], 0.0, 1e-5))                       return EXIT_FAILURE;
        if (!fracture_test_closed(f->mesh))                                     return EXIT_FAILURE;
        /* recentered: the mesh's own volume and its AABB agree with the above */
        if (!fracture_test_near(fracture_test_volume(f->mesh), 0.5, 1e-5))      return EXIT_FAILURE;
        if (!fracture_test_near(f->mesh->aabb[0][0], -0.25, 1e-5))              return EXIT_FAILURE;
        if (!fracture_test_near(f->mesh->aabb[1][0], 0.25, 1e-5))               return EXIT_FAILURE;
        /* the source attributes are carried, tangents weren't there */
        if (mesh_nr_tx(f->mesh) != mesh_nr_vx(f->mesh))                         return EXIT_FAILURE;
        if (mesh_nr_norm(f->mesh) != mesh_nr_vx(f->mesh))                       return EXIT_FAILURE;
        if (mesh_nr_tangent(f->mesh))                                           return EXIT_FAILURE;
        /* the cap: a square with the four face diagonals' crossings on its
         * sides, 8 ring points, 6 triangles */
        if (fracture_test_count_normal(f->mesh, cap_n) != 18)                   return EXIT_FAILURE;
        if (strcmp(f->mesh->name, "cube"))                                      return EXIT_FAILURE;
    }

    /* merging everything back restores the cube, minus the hidden caps */
    LOCAL_SET(mesh_t, whole) = CRES_RET(fracture_merge(ff, nullptr, "whole"), return EXIT_FAILURE);
    if (!fracture_test_closed(whole))                                   return EXIT_FAILURE;
    if (!fracture_test_near(fracture_test_volume(whole), 1.0, 1e-5))    return EXIT_FAILURE;
    if (mesh_nr_vx(whole) != mesh_nr_vx(ff->fragments[0].mesh) + mesh_nr_vx(ff->fragments[1].mesh) - 36)
        return EXIT_FAILURE;
    if (fracture_test_count_normal(whole, (vec3){ 1.0f, 0.0f, 0.0f }) != 6) return EXIT_FAILURE;
    if (!fracture_test_near(whole->aabb[0][0], -0.5, 1e-5))                 return EXIT_FAILURE;
    if (!fracture_test_near(whole->aabb[1][0], 0.5, 1e-5))                  return EXIT_FAILURE;

    /* a ruin: one half dropped, its cut face stays on the other */
    bool keep[] = { false, true };
    LOCAL_SET(mesh_t, half) = CRES_RET(fracture_merge(ff, keep, "half"), return EXIT_FAILURE);
    if (!fracture_test_closed(half))                                            return EXIT_FAILURE;
    if (!fracture_test_near(fracture_test_volume(half), 0.5, 1e-5))             return EXIT_FAILURE;
    if (!fracture_test_near(half->aabb[0][0], 0.0, 1e-5))                       return EXIT_FAILURE;
    if (fracture_test_count_normal(half, (vec3){ -1.0f, 0.0f, 0.0f }) != 18)    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int fracture_halves_test(void)
{
    LOCAL_SET(mesh_t, cube) = CRES_RET(
        fracture_test_box("cube", (vec3){ -0.5f, -0.5f, -0.5f }, (vec3){ 0.5f, 0.5f, 0.5f }),
        return EXIT_FAILURE
    );

    if (!fracture_test_closed(cube))                                return EXIT_FAILURE;
    if (!fracture_test_near(fracture_test_volume(cube), 1.0, 1e-6)) return EXIT_FAILURE;

    /* two seeds mirrored in X: the bisector is the YZ plane */
    vec3 seeds[] = { { -0.25f, 0.0f, 0.0f }, { 0.25f, 0.0f, 0.0f } };
    fracture_params params = { .seeds = seeds, .nr_seeds = array_size(seeds) };

    /* explicit seeds don't need a random stream */
    cres(fracture_fragments) res = fracture_mesh(cube, &params, nullptr);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    fracture_fragments ff = res.val;
    int ret = fracture_halves_check(&ff);

    fracture_fragments_release(&ff);
    if (ff.nr || ff.fragments)                  return EXIT_FAILURE;

    return ret;
}

/* two disjoint boxes: one cut makes two loops, one cap each */
static int fracture_two_loops_test(void)
{
    LOCAL_SET(mesh_t, left) = CRES_RET(
        fracture_test_box("pair", (vec3){ -2.0f, -0.5f, -0.5f }, (vec3){ -1.0f, 0.5f, 0.5f }),
        return EXIT_FAILURE
    );
    LOCAL_SET(mesh_t, right) = CRES_RET(
        fracture_test_box("right", (vec3){ 1.0f, -0.5f, -0.5f }, (vec3){ 2.0f, 0.5f, 0.5f }),
        return EXIT_FAILURE
    );

    /* room for both: mesh_push_mesh() doesn't grow */
    for (enum mesh_attrs a = MESH_VX; a <= MESH_NORM; a++)
        CERR_RET(mesh_attr_resize(left, a, 72), return EXIT_FAILURE);
    CERR_RET(mesh_attr_resize(left, MESH_IDX, 72), return EXIT_FAILURE);
    for (enum mesh_attrs a = MESH_VX; a <= MESH_NORM; a++)  mesh_attr(left, a)->nr = 36;
    mesh_attr(left, MESH_IDX)->nr = 36;
    mesh_push_mesh(left, right, 0.0f, 0.0f, 0.0f, 1.0f);
    if (mesh_nr_vx(left) != 72 || !fracture_test_closed(left))  return EXIT_FAILURE;

    /* the bisector z = 0 cuts both boxes */
    vec3 seeds[] = { { 0.0f, 0.0f, -0.25f }, { 0.0f, 0.0f, 0.25f } };
    fracture_params params = { .seeds = seeds, .nr_seeds = array_size(seeds) };
    cres(fracture_fragments) res = fracture_mesh(left, &params, nullptr);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    fracture_fragments ff = res.val;
    int ret = EXIT_FAILURE;

    if (ff.nr != 2)                             goto out;

    for (size_t i = 0; i < ff.nr; i++) {
        fracture_fragment *f = &ff.fragments[i];
        vec3 cap_n = { 0.0f, 0.0f, i ? -1.0f : 1.0f };

        if (!fracture_test_near(f->volume, 1.0, 1e-5))                      goto out;
        if (!fracture_test_near(f->center[0], 0.0, 1e-5))                   goto out;
        if (!fracture_test_near(f->center[2], i ? 0.25 : -0.25, 1e-5))      goto out;
        if (!fracture_test_closed(f->mesh))                                 goto out;
        /* two caps of 6 triangles, one per box */
        if (fracture_test_count_normal(f->mesh, cap_n) != 36)               goto out;
    }

    ret = EXIT_SUCCESS;
out:
    fracture_fragments_release(&ff);

    return ret;
}

static int fracture_random_check(struct mesh *cube, fracture_fragments *ff, float *volumes)
{
    double total = 0.0;
    vec3 moment = {};

    if (ff->nr < 2 || ff->nr > 12)              return EXIT_FAILURE;

    for (size_t i = 0; i < ff->nr; i++) {
        fracture_fragment *f = &ff->fragments[i];

        if (!fracture_test_closed(f->mesh))                                         return EXIT_FAILURE;
        if (!fracture_test_near(fracture_test_volume(f->mesh), f->volume, 1e-5))    return EXIT_FAILURE;
        /* recentered: the fragment sits inside the source around its centroid */
        for (int j = 0; j < 3; j++) {
            float ext = cube->aabb[1][j] - cube->aabb[0][j];

            if (f->center[j] < cube->aabb[0][j] || f->center[j] > cube->aabb[1][j]) return EXIT_FAILURE;
            if (f->mesh->aabb[0][j] < -ext || f->mesh->aabb[1][j] > ext)            return EXIT_FAILURE;
            if (f->mesh->aabb[0][j] > 0.0f || f->mesh->aabb[1][j] < 0.0f)           return EXIT_FAILURE;
            moment[j] += f->center[j] * f->volume;
        }
        total += f->volume;
        volumes[i] = f->volume;
    }

    /* the fragments add up to the source (2 x 1 x 4), centroid included */
    if (!fracture_test_near(total, 8.0, 1e-4))                          return EXIT_FAILURE;
    for (int j = 0; j < 3; j++)
        if (!fracture_test_near(moment[j] / total, 0.0, 1e-4))          return EXIT_FAILURE;

    /* and merge back into it */
    LOCAL_SET(mesh_t, whole) = CRES_RET(fracture_merge(ff, nullptr, "whole"), return EXIT_FAILURE);
    if (!fracture_test_closed(whole))                                   return EXIT_FAILURE;
    if (!fracture_test_near(fracture_test_volume(whole), 8.0, 1e-4))    return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static int fracture_random_test(void)
{
    LOCAL_SET(mesh_t, cube) = CRES_RET(
        fracture_test_box("cube", (vec3){ -1.0f, -0.5f, -2.0f }, (vec3){ 1.0f, 0.5f, 2.0f }),
        return EXIT_FAILURE
    );
    fracture_params params = { .nr_pieces = 12, .cap_uv_scale = 0.5f };
    float volumes[2][12];
    size_t nrs[2];

    /* no random stream is no go without explicit seeds */
    cres(fracture_fragments) bad = fracture_mesh(cube, &params, nullptr);
    if (!IS_CERR(bad))                          return EXIT_FAILURE;

    /* the same seed makes the same fragments */
    for (int run = 0; run < 2; run++) {
        LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_XOSHIRO256PP, 7), return EXIT_FAILURE);
        cres(fracture_fragments) res = fracture_mesh(cube, &params, rng);
        if (IS_CERR(res))                       return EXIT_FAILURE;

        fracture_fragments ff = res.val;
        int ret = fracture_random_check(cube, &ff, volumes[run]);

        nrs[run] = ff.nr;
        fracture_fragments_release(&ff);
        if (ret != EXIT_SUCCESS)                return ret;
    }

    if (nrs[0] != nrs[1])                       return EXIT_FAILURE;
    for (size_t i = 0; i < nrs[0]; i++)
        if (volumes[0][i] != volumes[1][i])     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* an open sheet fractures into sheets: no volume, no caps, the area kept */
static int fracture_sheet_test(void)
{
    LOCAL_SET(mesh_t, sheet) = CRES_RET(ref_new_checked(mesh, .name = "sheet"), return EXIT_FAILURE);
    /* off the origin: the cone from the origin to the sheet has a volume */
    static const float vx[] = {
        -1.0f, 1.0f, -1.0f,  1.0f, 1.0f, -1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, 1.0f, -1.0f,  1.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,
    };
    static const float norm[] = {
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
    };
    static const unsigned short idx[] = { 0, 1, 2, 3, 4, 5 };

    CERR_RET(mesh_attr_dup(sheet, MESH_VX, (void *)vx, sizeof(float) * 3, 6), return EXIT_FAILURE);
    CERR_RET(mesh_attr_dup(sheet, MESH_NORM, (void *)norm, sizeof(float) * 3, 6), return EXIT_FAILURE);
    CERR_RET(mesh_attr_dup(sheet, MESH_IDX, (void *)idx, sizeof(unsigned short), 6), return EXIT_FAILURE);

    vec3 seeds[] = { { -0.5f, 1.0f, -0.5f }, { 0.5f, 1.0f, 0.0f }, { -0.3f, 1.0f, 0.7f } };
    fracture_params params = { .seeds = seeds, .nr_seeds = array_size(seeds) };
    cres(fracture_fragments) res = fracture_mesh(sheet, &params, nullptr);
    if (IS_CERR(res))                           return EXIT_FAILURE;

    fracture_fragments ff = res.val;
    double area = 0.0;
    int ret = EXIT_FAILURE;

    if (ff.nr != 3)                             goto out;

    for (size_t i = 0; i < ff.nr; i++) {
        fracture_fragment *f = &ff.fragments[i];

        if (f->volume != 0.0f)                                                                      goto out;
        if (fracture_test_count_normal(f->mesh, (vec3){ 0.0f, 1.0f, 0.0f }) != mesh_nr_vx(f->mesh)) goto out;
        if (mesh_nr_tx(f->mesh))                                                                    goto out;
        /* an open fragment is centered on its AABB */
        if (!fracture_test_near(f->mesh->aabb[0][0] + f->mesh->aabb[1][0], 0.0, 1e-5))  goto out;
        if (!fracture_test_near(f->mesh->aabb[0][2] + f->mesh->aabb[1][2], 0.0, 1e-5))  goto out;
        if (!fracture_test_near(f->center[1], 1.0, 1e-5))                               goto out;
        area += fracture_test_area(f->mesh);
    }
    if (!fracture_test_near(area, 4.0, 1e-4))   goto out;

    ret = EXIT_SUCCESS;
out:
    fracture_fragments_release(&ff);

    return ret;
}

static int fracture_validation_test(void)
{
    LOCAL_SET(mesh_t, cube) = CRES_RET(
        fracture_test_box("cube", (vec3){ 0.0f, 0.0f, 0.0f }, (vec3){ 2.0f, 1.0f, 1.0f }),
        return EXIT_FAILURE
    );
    LOCAL_SET(prng, rng) = CRES_RET(prng_new(PRNG_DEFAULT, 1), return EXIT_FAILURE);
    vec3 seed = { 1.0f, 0.5f, 0.5f };

    cres(fracture_fragments) bad;

    bad = fracture_mesh(nullptr, &(fracture_params){ .nr_pieces = 2 }, rng);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;
    bad = fracture_mesh(cube, nullptr, rng);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;
    bad = fracture_mesh(cube, &(fracture_params){ .nr_pieces = 0 }, rng);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;
    bad = fracture_mesh(cube, &(fracture_params){ .seeds = &seed, .nr_seeds = 0 }, rng);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;

    /* an empty mesh */
    LOCAL_SET(mesh_t, empty) = CRES_RET(ref_new_checked(mesh, .name = "empty"), return EXIT_FAILURE);
    bad = fracture_mesh(empty, &(fracture_params){ .nr_pieces = 2 }, rng);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;

    /* one seed: the cell is the whole space, the fragment is the source */
    cres(fracture_fragments) res = fracture_mesh(cube, &(fracture_params){ .seeds = &seed, .nr_seeds = 1 }, nullptr);
    if (IS_CERR(res))                                   return EXIT_FAILURE;

    fracture_fragments ff = res.val;
    int ret = EXIT_FAILURE;

    if (ff.nr != 1)                                                         goto out;
    if (!fracture_test_near(ff.fragments[0].volume, 2.0, 1e-5))             goto out;
    if (!fracture_test_near(ff.fragments[0].center[0], 1.0, 1e-5))          goto out;
    if (!fracture_test_near(ff.fragments[0].center[1], 0.5, 1e-5))          goto out;
    if (mesh_nr_vx(ff.fragments[0].mesh) != 36)                             goto out;
    if (!fracture_test_closed(ff.fragments[0].mesh))                        goto out;

    /* merging nothing is an error */
    bool keep = false;
    cresp(mesh) none = fracture_merge(&ff, &keep, "none");
    if (!IS_CERR_CODE(none, CERR_INVALID_ARGUMENTS))                        goto out;
    none = fracture_merge(&ff, nullptr, nullptr);
    if (!IS_CERR_CODE(none, CERR_INVALID_ARGUMENTS))                        goto out;

    ret = EXIT_SUCCESS;
out:
    fracture_fragments_release(&ff);

    return ret;
}


static const struct test tests[] = {
    { .name = "fracture halves", .test = fracture_halves_test },
    { .name = "fracture two loops", .test = fracture_two_loops_test },
    { .name = "fracture random", .test = fracture_random_test },
    { .name = "fracture sheet", .test = fracture_sheet_test },
    { .name = "fracture validation", .test = fracture_validation_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
