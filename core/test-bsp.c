// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include "bsp.h"
#include "memory.h"
#include "prng.h"
#include "test.h"
#include "util.h"

struct bsp_test_room {
    bsp_leaf    leaf;
    int         id;
};

struct bsp_test_ctx {
    struct bsp_test_room    *rooms[8];
    int                     allocs, frees;
    int                     next_id;
    int                     split_below;
    int                     fail_at;
};

static bsp_leaf *bsp_test_alloc(const float bounds[6], int dims, int depth, void *priv)
{
    struct bsp_test_ctx *ctx = priv;

    if (ctx->fail_at && ctx->allocs == ctx->fail_at - 1)    return NULL;

    struct bsp_test_room *room = mem_alloc(sizeof(*room), .zero = 1);
    if (!room)                                              return NULL;

    ctx->allocs++;
    return &room->leaf;
}

static void bsp_test_free(bsp_leaf *l, void *priv)
{
    struct bsp_test_ctx *ctx = priv;

    ctx->frees++;
    mem_free(container_of(l, struct bsp_test_room, leaf));
}

static bool bsp_test_should_split(const bsp_leaf *l, void *priv)
{
    return l->depth < ((struct bsp_test_ctx *)priv)->split_below;
}

/* alternate X and Z by depth, split down the middle */
static bsp_split bsp_test_split_half(const bsp_leaf *l, prng *r, void *priv)
{
    return (bsp_split) { .axis = l->depth & 1 ? BSP_Z : BSP_X, .where = 0.5f };
}

static void bsp_test_on_leaf(bsp_leaf *l, void *priv)
{
    struct bsp_test_ctx *ctx = priv;
    struct bsp_test_room *room = container_of(l, struct bsp_test_room, leaf);

    room->id = ctx->next_id;
    if (ctx->next_id < (int)array_size(ctx->rooms))     ctx->rooms[ctx->next_id] = room;
    ctx->next_id++;
}

static void bsp_test_count(bsp_leaf *l, void *priv)
{
    (*(int *)priv)++;
}

static int bsp_test_find_id(bsp_tree *t, float x, float y, float z)
{
    cresp(bsp_leaf) res = bsp_find(t, (float [3]){ x, y, z });

    if (IS_CERR(res))   return -1;
    return container_of(res.val, struct bsp_test_room, leaf)->id;
}

static int bsp_test_neigh_id(bsp_tree *t, const bsp_leaf *l, bsp_axis ax, int dir)
{
    cresp(bsp_leaf) res = bsp_neighbor(t, l, ax, dir);

    if (IS_CERR(res))   return -1;
    return container_of(res.val, struct bsp_test_room, leaf)->id;
}

static int bsp_grid_test(void)
{
    struct bsp_test_ctx ctx = { .split_below = 2 };
    bsp_params params = {
        .dims           = 2,
        .extent         = { 8.0f, 0.0f, 8.0f },
        .alloc_leaf     = bsp_test_alloc,
        .free_leaf      = bsp_test_free,
        .should_split   = bsp_test_should_split,
        .split          = bsp_test_split_half,
        .on_leaf        = bsp_test_on_leaf,
        .priv           = &ctx,
    };

    /* a custom splitter doesn't need a random stream */
    LOCAL_SET(bsp_tree, t) = CRES_RET(bsp_build(&params, NULL), return EXIT_FAILURE);

    int count = 0;
    bsp_for_each_leaf(t, bsp_test_count, &count);
    if (count != 4 || ctx.allocs != 4 || ctx.next_id != 4) return EXIT_FAILURE;

    /* in-order leaves: (x, z) quadrants ll, lh, hl, hh, 4x4 each */
    static const float exp_bounds[4][6] = {
        { 0, 0, 0, 4, 0, 4 }, { 0, 0, 4, 4, 0, 8 },
        { 4, 0, 0, 8, 0, 4 }, { 4, 0, 4, 8, 0, 8 },
    };
    for (int i = 0; i < 4; i++) {
        bsp_leaf *l = &ctx.rooms[i]->leaf;

        if (memcmp(l->bounds, exp_bounds[i], sizeof(exp_bounds[i]))) return EXIT_FAILURE;
        if (l->depth != 2 || l->dims != 2)                           return EXIT_FAILURE;
    }

    if (bsp_test_find_id(t, 1, 0, 1) != 0)      return EXIT_FAILURE;
    if (bsp_test_find_id(t, 1, 0, 5) != 1)      return EXIT_FAILURE;
    if (bsp_test_find_id(t, 5, 0, 1) != 2)      return EXIT_FAILURE;
    if (bsp_test_find_id(t, 5, 0, 5) != 3)      return EXIT_FAILURE;
    /* a point on a split plane resolves to the higher side */
    if (bsp_test_find_id(t, 4, 0, 4) != 3)      return EXIT_FAILURE;
    /* the top corner still belongs to the tree */
    if (bsp_test_find_id(t, 8, 0, 8) != 3)      return EXIT_FAILURE;
    /* the unused Y axis is ignored */
    if (bsp_test_find_id(t, 1, 999, 1) != 0)    return EXIT_FAILURE;
    /* outside on active axes, NaN included */
    if (bsp_test_find_id(t, -1, 0, 1) != -1)    return EXIT_FAILURE;
    if (bsp_test_find_id(t, 1, 0, 9) != -1)     return EXIT_FAILURE;
    if (bsp_test_find_id(t, NAN, 0, 1) != -1)   return EXIT_FAILURE;

    const bsp_leaf *l0 = &ctx.rooms[0]->leaf;
    const bsp_leaf *l3 = &ctx.rooms[3]->leaf;
    if (bsp_test_neigh_id(t, l0, BSP_X, 1) != 2)    return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l0, BSP_Z, 1) != 1)    return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l0, BSP_X, -1) != -1)  return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l0, BSP_Z, -1) != -1)  return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l3, BSP_X, -1) != 1)   return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l3, BSP_Z, -1) != 2)   return EXIT_FAILURE;
    /* inactive axis and zero direction are invalid */
    if (bsp_test_neigh_id(t, l0, BSP_Y, 1) != -1)   return EXIT_FAILURE;
    if (bsp_test_neigh_id(t, l0, BSP_X, 0) != -1)   return EXIT_FAILURE;

    bsp_free(NOCU(t));
    if (ctx.frees != ctx.allocs)                return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

struct bsp_bounds_log {
    float   b[16][6];
    int     n;
};

static void bsp_test_log_bounds(bsp_leaf *l, void *priv)
{
    struct bsp_bounds_log *log = priv;

    if (log->n < (int)array_size(log->b))   memcpy(log->b[log->n++], l->bounds, sizeof(l->bounds));
}

static int bsp_random_test(void)
{
    struct bsp_test_ctx ctx = { .split_below = 3 };
    bsp_params params = {
        .dims           = 2,
        .extent         = { 10.0f, 0.0f, 10.0f },
        .alloc_leaf     = bsp_test_alloc,
        .free_leaf      = bsp_test_free,
        .should_split   = bsp_test_should_split,
        .priv           = &ctx,
    };
    struct bsp_bounds_log logs[2] = {};

    /* the default splitter is deterministic for a given seed */
    for (int run = 0; run < 2; run++) {
        LOCAL_SET(prng, r) = CRES_RET(prng_new(PRNG_DEFAULT, 7), return EXIT_FAILURE);
        LOCAL_SET(bsp_tree, t) = CRES_RET(bsp_build(&params, r), return EXIT_FAILURE);

        bsp_for_each_leaf(t, bsp_test_log_bounds, &logs[run]);
        if (logs[run].n != 8)                   return EXIT_FAILURE;
    }
    if (memcmp(&logs[0], &logs[1], sizeof(logs[0])))    return EXIT_FAILURE;

    /* the leaves tile the root: extents are sane, areas add up */
    double area = 0.0;
    for (int i = 0; i < logs[0].n; i++) {
        const float *b = logs[0].b[i];

        if (b[0] < 0.0f || b[3] > 10.0f || b[0] >= b[3])    return EXIT_FAILURE;
        if (b[2] < 0.0f || b[5] > 10.0f || b[2] >= b[5])    return EXIT_FAILURE;
        if (b[1] != 0.0f || b[4] != 0.0f)                   return EXIT_FAILURE;
        area += (double)(b[3] - b[0]) * (b[5] - b[2]);
    }
    if (fabs(area - 100.0) > 1e-3)              return EXIT_FAILURE;

    /* a different seed produces a different layout */
    struct bsp_bounds_log other = {};
    LOCAL_SET(prng, r) = CRES_RET(prng_new(PRNG_DEFAULT, 8), return EXIT_FAILURE);
    LOCAL_SET(bsp_tree, t) = CRES_RET(bsp_build(&params, r), return EXIT_FAILURE);
    bsp_for_each_leaf(t, bsp_test_log_bounds, &other);
    if (!memcmp(&logs[0], &other, sizeof(other)))   return EXIT_FAILURE;

    /* 3D: all three axes split, points land in containing leaves */
    struct bsp_test_ctx ctx3 = { .split_below = 2 };
    bsp_params params3 = params;
    params3.dims = 3;
    params3.extent[0] = params3.extent[1] = params3.extent[2] = 4.0f;
    params3.priv = &ctx3;

    LOCAL_SET(prng, r3) = CRES_RET(prng_new(PRNG_DEFAULT, 21), return EXIT_FAILURE);
    LOCAL_SET(bsp_tree, t3) = CRES_RET(bsp_build(&params3, r3), return EXIT_FAILURE);

    static const float points[][3] = { { 1, 1, 1 }, { 3.5f, 0.5f, 2 }, { 2, 3, 0.25f } };
    for (size_t i = 0; i < array_size(points); i++) {
        bsp_leaf *l = CRES_RET(bsp_find(t3, points[i]), return EXIT_FAILURE);

        if (l->dims != 3)                       return EXIT_FAILURE;
        for (int ax = 0; ax < 3; ax++)
            if (points[i][ax] < l->bounds[ax] || points[i][ax] > l->bounds[ax + 3])  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static bsp_split bsp_test_split_inactive(const bsp_leaf *l, prng *r, void *priv)
{
    return (bsp_split) { .axis = BSP_Y, .where = 0.5f };
}

static bsp_split bsp_test_split_degenerate(const bsp_leaf *l, prng *r, void *priv)
{
    return (bsp_split) { .axis = BSP_X, .where = 0.0f };
}

static int bsp_validation_test(void)
{
    struct bsp_test_ctx ctx = { .split_below = 2 };
    bsp_params good = {
        .dims           = 2,
        .extent         = { 8.0f, 0.0f, 8.0f },
        .alloc_leaf     = bsp_test_alloc,
        .free_leaf      = bsp_test_free,
        .should_split   = bsp_test_should_split,
        .split          = bsp_test_split_half,
        .on_leaf        = bsp_test_on_leaf,
        .priv           = &ctx,
    };
    bsp_params bad;
    cresp(bsp_tree) res;

    bad = good;
    bad.alloc_leaf = NULL;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    bad = good;
    bad.dims = 4;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    /* three nonzero extents don't make a 2D tree */
    bad = good;
    bad.extent[1] = 1.0f;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    bad = good;
    bad.extent[0] = -8.0f;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    /* the default splitter without a stream */
    bad = good;
    bad.split = NULL;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    /* splits along an inactive axis or collapsing a child fail the build */
    bad = good;
    bad.split = bsp_test_split_inactive;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    bad = good;
    bad.split = bsp_test_split_degenerate;
    res = bsp_build(&bad, NULL);
    if (!IS_CERR(res))                          return EXIT_FAILURE;

    /* a failing leaf allocator unwinds the leaves already built */
    ctx.fail_at = 3;
    res = bsp_build(&good, NULL);
    if (!IS_CERR(res) || !IS_CERR_CODE(res, CERR_NOMEM))    return EXIT_FAILURE;
    if (ctx.allocs != 2 || ctx.frees != 2)      return EXIT_FAILURE;
    ctx.fail_at = 0;

    /* a leaf from one tree isn't valid in another */
    ctx.allocs = ctx.frees = ctx.next_id = 0;
    LOCAL_SET(bsp_tree, t1) = CRES_RET(bsp_build(&good, NULL), return EXIT_FAILURE);
    struct bsp_test_room *room = ctx.rooms[0];
    LOCAL_SET(bsp_tree, t2) = CRES_RET(bsp_build(&good, NULL), return EXIT_FAILURE);
    cresp(bsp_leaf) neigh = bsp_neighbor(t2, &room->leaf, BSP_X, 1);
    if (!IS_CERR(neigh))                        return EXIT_FAILURE;

    cresp(bsp_leaf) found = bsp_find(NULL, (float [3]){ 0, 0, 0 });
    if (!IS_CERR(found))                        return EXIT_FAILURE;
    found = bsp_find(t1, NULL);
    if (!IS_CERR(found))                        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "bsp grid", .test = bsp_grid_test },
    { .name = "bsp random split", .test = bsp_random_test },
    { .name = "bsp validation", .test = bsp_validation_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
