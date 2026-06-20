// SPDX-License-Identifier: Apache-2.0
#include "bsp.h"
#include "memory.h"

/*
 * Internal node: leaves carry the user's bsp_leaf, internal nodes carry
 * the split plane and two children. The tree only chains user-owned
 * leaves together; their storage comes from bsp_params::alloc_leaf.
 */
struct bsp_node {
    struct bsp_node *child[2];  /* [0] below the plane, [1] above; NULL on leaves */
    struct bsp_node *parent;
    bsp_leaf        *leaf;      /* leaf nodes only */
    bsp_axis        axis;       /* internal nodes only */
    float           where;      /* absolute split coordinate along axis */
};

struct bsp_tree {
    bsp_params      params;
    struct bsp_node *root;
};

static void bsp_node_free(bsp_tree *t, struct bsp_node *n)
{
    if (!n)         return;

    bsp_node_free(t, n->child[0]);
    bsp_node_free(t, n->child[1]);
    if (n->leaf)    t->params.free_leaf(n->leaf, t->params.priv);
    mem_free(n);
}

static bsp_split bsp_split_default(const bsp_leaf *l, prng *r)
{
    int active[3], nr = 0;

    for (int i = 0; i < 3; i++)
        if (l->bounds[i + 3] - l->bounds[i] > 0.0f)
            active[nr++] = i;

    return (bsp_split) {
        .axis   = active[prng_u64(r) % nr],
        .where  = 0.2f + prng_unitd(r) * 0.6f,
    };
}

static cerr bsp_build_node(bsp_tree *t, struct bsp_node *parent, const float bounds[6],
                           int depth, prng *r, struct bsp_node **out)
{
    const bsp_params *p = &t->params;

    struct bsp_node *node = mem_alloc(sizeof(*node), .zero = 1);
    if (!node)  return CERR_NOMEM;
    node->parent = parent;

    bsp_leaf probe = { .depth = depth, .dims = p->dims };
    memcpy(probe.bounds, bounds, sizeof(probe.bounds));

    if (!p->should_split(&probe, p->priv)) {
        bsp_leaf *leaf = p->alloc_leaf(bounds, p->dims, depth, p->priv);
        if (!leaf) {
            mem_free(node);
            return CERR_NOMEM;
        }

        memcpy(leaf->bounds, bounds, sizeof(leaf->bounds));
        leaf->depth = depth;
        leaf->dims = p->dims;
        node->leaf = leaf;

        if (p->on_leaf) p->on_leaf(leaf, p->priv);

        *out = node;
        return CERR_OK;
    }

    bsp_split split = p->split ? p->split(&probe, r, p->priv) : bsp_split_default(&probe, r);

    if (split.axis < BSP_X || split.axis > BSP_Z) {
        mem_free(node);
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "bad split axis %d",
            .arg0   = (void *)(intptr_t)split.axis
        );
    }

    float lo = bounds[split.axis], hi = bounds[split.axis + 3];
    node->axis = split.axis;
    node->where = lo + split.where * (hi - lo);

    /*
     * Strictly-inside also catches inactive axes (lo == hi) and NaN; the
     * fraction isn't reported, converting an out-of-range float to an
     * integer is UB
     */
    if (!(node->where > lo) || !(node->where < hi)) {
        mem_free(node);
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "split along axis %d collapses a child",
            .arg0   = (void *)(intptr_t)split.axis
        );
    }

    /*
     * Each subtree draws from its own split-off stream, so its layout
     * depends only on the seed and the path from the root
     */
    LOCAL(prng, r0);
    LOCAL(prng, r1);
    if (r) {
        r0 = CRES_RET(prng_split(r), { mem_free(node); return cerr_error_cres(__resp); });
        r1 = CRES_RET(prng_split(r), { mem_free(node); return cerr_error_cres(__resp); });
    }

    float cb[6];
    memcpy(cb, bounds, sizeof(cb));
    cb[split.axis + 3] = node->where;
    CERR_RET(bsp_build_node(t, node, cb, depth + 1, r0, &node->child[0]),
             { bsp_node_free(t, node); return __cerr; });

    memcpy(cb, bounds, sizeof(cb));
    cb[split.axis] = node->where;
    CERR_RET(bsp_build_node(t, node, cb, depth + 1, r1, &node->child[1]),
             { bsp_node_free(t, node); return __cerr; });

    *out = node;
    return CERR_OK;
}

cresp(bsp_tree) bsp_build(const bsp_params *p, prng *r)
{
    if (!p || !p->alloc_leaf || !p->free_leaf || !p->should_split)
        return cresp_error(bsp_tree, CERR_INVALID_ARGUMENTS);

    if (!p->split && !r)
        return cresp_error(bsp_tree, CERR_INVALID_ARGUMENTS,
                           .fmt = "the default splitter needs a random stream");

    int active = 0;
    for (int i = 0; i < 3; i++) {
        if (p->extent[i] < 0.0f)
            return cresp_error(bsp_tree, CERR_INVALID_ARGUMENTS,
                               .fmt = "negative extent on axis %d", .arg0 = (void *)(intptr_t)i);
        active += p->extent[i] > 0.0f;
    }

    if ((p->dims != 2 && p->dims != 3) || active != p->dims)
        return cresp_error(bsp_tree, CERR_INVALID_ARGUMENTS,
                           .fmt = "%d nonzero extents for %d dims",
                           .arg0 = (void *)(intptr_t)active, .arg1 = (void *)(intptr_t)p->dims);

    bsp_tree *t = mem_alloc(sizeof(*t), .zero = 1);
    if (!t)     return cresp_error(bsp_tree, CERR_NOMEM);
    t->params = *p;

    float bounds[6] = { 0.0f, 0.0f, 0.0f, p->extent[0], p->extent[1], p->extent[2] };
    CERR_RET(bsp_build_node(t, NULL, bounds, 0, r, &t->root),
             { mem_free(t); return cresp_error_cerr(bsp_tree, __cerr); });

    return cresp_val(bsp_tree, t);
}

void bsp_free(bsp_tree *t)
{
    if (!t)     return;

    bsp_node_free(t, t->root);
    mem_free(t);
}

DEFINE_CLEANUP(bsp_tree, if (*p) bsp_free(*p))

static struct bsp_node *bsp_descend(bsp_tree *t, const float point[3])
{
    struct bsp_node *n = t->root;

    while (!n->leaf)
        n = point[n->axis] < n->where ? n->child[0] : n->child[1];

    return n;
}

cresp(bsp_leaf) bsp_find(bsp_tree *t, const float point[3])
{
    if (!t || !point)   return cresp_error(bsp_leaf, CERR_INVALID_ARGUMENTS);

    /* inverted so that a NaN coordinate is outside too */
    for (int i = 0; i < 3; i++)
        if (t->params.extent[i] > 0.0f &&
            (!(point[i] >= 0.0f) || !(point[i] <= t->params.extent[i])))
            return cresp_error(bsp_leaf, CERR_NOT_FOUND);

    return cresp_val(bsp_leaf, bsp_descend(t, point)->leaf);
}

cresp(bsp_leaf) bsp_neighbor(bsp_tree *t, const bsp_leaf *l, bsp_axis ax, int dir)
{
    if (!t || !l || ax < BSP_X || ax > BSP_Z || !dir)   return cresp_error(bsp_leaf, CERR_INVALID_ARGUMENTS);

    if (t->params.extent[ax] <= 0.0f)
        return cresp_error(bsp_leaf, CERR_INVALID_ARGUMENTS,
                           .fmt = "axis %d is inactive", .arg0 = (void *)(intptr_t)ax);

    /* the leaf's node is where its center descends to */
    float c[3];
    for (int i = 0; i < 3; i++)
        c[i] = (l->bounds[i] + l->bounds[i + 3]) * 0.5f;

    struct bsp_node *n = bsp_descend(t, c);
    if (n->leaf != l)   return cresp_error(bsp_leaf, CERR_INVALID_ARGUMENTS, .fmt = "leaf is not in the tree");

    /*
     * Climb to the nearest ancestor whose plane separates the leaf from
     * the sought direction, then descend its far subtree hugging the
     * crossed face
     */
    int side = dir > 0 ? 0 : 1;
    struct bsp_node *sib = NULL;
    for (struct bsp_node *a = n; a->parent; a = a->parent)
        if (a->parent->axis == ax && a->parent->child[side] == a) {
            sib = a->parent->child[1 - side];
            break;
        }

    if (!sib)           return cresp_error(bsp_leaf, CERR_NOT_FOUND);

    n = sib;
    while (!n->leaf)
        n = n->axis == ax ? n->child[side] : (c[n->axis] < n->where ? n->child[0] : n->child[1]);

    return cresp_val(bsp_leaf, n->leaf);
}

static void bsp_node_for_each(struct bsp_node *n, bsp_leaf_fn fn, void *priv)
{
    if (n->leaf) {
        fn(n->leaf, priv);
        return;
    }

    bsp_node_for_each(n->child[0], fn, priv);
    bsp_node_for_each(n->child[1], fn, priv);
}

void bsp_for_each_leaf(bsp_tree *t, bsp_leaf_fn fn, void *priv)
{
    if (!t || !fn)      return;

    bsp_node_for_each(t->root, fn, priv);
}
