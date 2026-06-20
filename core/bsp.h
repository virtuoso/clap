/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_BSP_H__
#define __CLAP_BSP_H__

#include "error.h"
#include "prng.h"
#include "util.h"

/**
 * enum bsp_axis - splitting axis of a BSP node
 * @BSP_X: the X axis
 * @BSP_Y: the Y axis
 * @BSP_Z: the Z axis
 */
typedef enum bsp_axis {
    BSP_X = 0,
    BSP_Y,
    BSP_Z,
} bsp_axis;

/**
 * struct bsp_leaf - embeddable header of a BSP leaf
 * @bounds: axis-aligned bounds: min x/y/z in [0..2], max x/y/z in [3..5];
 *          unused axes stay at 0
 * @depth:  number of splits above this leaf
 * @dims:   dimensionality of the tree, 2 or 3
 *
 * The tree doesn't own leaf storage: the user embeds bsp_leaf in their own
 * struct (like struct list or struct ref), allocates it in
 * bsp_params::alloc_leaf and recovers the containing struct from any
 * bsp_leaf the tree hands back with container_of(). The header fields are
 * filled in by the tree machinery.
 */
typedef struct bsp_leaf {
    float   bounds[6];
    int     depth;
    int     dims;
} bsp_leaf;

/**
 * struct bsp_split - a splitter callback's verdict
 * @axis:   axis to split along; must have nonzero extent in the node
 * @where:  split position as a fraction of the node's extent, in (0, 1)
 */
typedef struct bsp_split {
    bsp_axis    axis;
    float       where;
} bsp_split;

/**
 * bsp_alloc_fn - allocate storage for one leaf
 * @bounds: bounds the leaf will cover (see bsp_leaf::bounds)
 * @dims:   dimensionality of the tree
 * @depth:  depth of the leaf
 * @priv:   bsp_params::priv
 *
 * The tree fills in the header fields of the returned leaf afterwards.
 *
 * Return: the bsp_leaf member of a caller-owned struct, or NULL to fail the
 * build with CERR_NOMEM.
 */
typedef bsp_leaf *(*bsp_alloc_fn)(const float bounds[6], int dims, int depth, void *priv);

/**
 * bsp_free_fn - release one leaf's storage
 * @l:      the leaf being released
 * @priv:   bsp_params::priv
 */
typedef void (*bsp_free_fn)(bsp_leaf *l, void *priv);

/**
 * bsp_should_split_fn - decide whether a region splits further
 * @l:      candidate region described as a leaf (bounds, depth, dims)
 * @priv:   bsp_params::priv
 *
 * Return: true to subdivide the region, false to finalize it as a leaf.
 */
typedef bool (*bsp_should_split_fn)(const bsp_leaf *l, void *priv);

/**
 * bsp_split_fn - choose where a region splits
 * @l:      the region being split
 * @r:      the region's random stream (the @r given to bsp_build() for the
 *          root, an independent prng_split() child per subtree below it);
 *          NULL when bsp_build() was called without one
 * @priv:   bsp_params::priv
 *
 * Return: the split; an inactive axis or a fraction outside (0, 1) fails
 * the build with CERR_INVALID_ARGUMENTS.
 */
typedef bsp_split (*bsp_split_fn)(const bsp_leaf *l, prng *r, void *priv);

/**
 * bsp_leaf_fn - per-leaf callback
 * @l:      the leaf
 * @priv:   bsp_params::priv for bsp_params::on_leaf, the caller's pointer
 *          for bsp_for_each_leaf()
 */
typedef void (*bsp_leaf_fn)(bsp_leaf *l, void *priv);

/**
 * struct bsp_params - bsp_build() parameters
 * @dims:           2 or 3; must match the number of nonzero extents
 * @extent:         root size per axis, growing from the origin; unused
 *                  axes stay at 0
 * @alloc_leaf:     leaf allocator, required
 * @free_leaf:      leaf releaser, required
 * @should_split:   split predicate, required
 * @split:          splitter; NULL picks a uniformly random active axis and
 *                  a fraction in [0.2, 0.8), which requires bsp_build()'s
 *                  random stream
 * @on_leaf:        optional hook, runs as each leaf is finalized during the
 *                  build
 * @priv:           opaque pointer handed to every callback; must stay valid
 *                  until bsp_free(), which calls @free_leaf
 */
typedef struct bsp_params {
    int                 dims;
    float               extent[3];
    bsp_alloc_fn        alloc_leaf;
    bsp_free_fn         free_leaf;
    bsp_should_split_fn should_split;
    bsp_split_fn        split;
    bsp_leaf_fn         on_leaf;
    void                *priv;
} bsp_params;

typedef struct bsp_tree bsp_tree;
cresp_ret(bsp_tree);
cresp_ret(bsp_leaf);
DECLARE_CLEANUP(bsp_tree);

/**
 * bsp_build() - recursively partition a 2D or 3D region
 * @p:  build parameters
 * @r:  random stream for split decisions, required when @p->split is NULL;
 *      the caller retains ownership. Each subtree gets its own
 *      prng_split() child, so a subtree's layout depends only on the seed
 *      and its path from the root, not on what its siblings did.
 *
 * Return: the tree to be bsp_free()d by the caller, or CERR_NOMEM /
 * CERR_INVALID_ARGUMENTS.
 */
cresp_check(bsp_tree) bsp_build(const bsp_params *p, prng *r);

/**
 * bsp_free() - free a BSP tree
 * @t:  the tree, may be NULL
 *
 * Calls bsp_params::free_leaf on every leaf.
 */
void bsp_free(bsp_tree *t);

/**
 * bsp_find() - locate the leaf containing a point
 * @t:      the tree
 * @point:  the point; unused axes are ignored
 *
 * A point on a split plane resolves to the higher-side leaf.
 *
 * Return: the leaf, CERR_NOT_FOUND for points outside the tree, or
 * CERR_INVALID_ARGUMENTS.
 */
cresp_check(bsp_leaf) bsp_find(bsp_tree *t, const float point[3]);

/**
 * bsp_neighbor() - step from a leaf to the adjacent leaf along an axis
 * @t:      the tree
 * @l:      a leaf of @t
 * @ax:     axis to step along; must be an active axis of the tree
 * @dir:    positive to step towards +@ax, negative towards -@ax
 *
 * When several leaves border the crossed face, the one straddling the
 * center of that face is returned.
 *
 * Return: the neighboring leaf, CERR_NOT_FOUND when @l borders the tree on
 * that side, or CERR_INVALID_ARGUMENTS.
 */
cresp_check(bsp_leaf) bsp_neighbor(bsp_tree *t, const bsp_leaf *l, bsp_axis ax, int dir);

/**
 * bsp_for_each_leaf() - walk every leaf of the tree
 * @t:      the tree
 * @fn:     callback to run on each leaf
 * @priv:   opaque pointer handed to @fn
 *
 * Leaves are visited in spatial order: within any split, the lower-side
 * subtree comes first.
 */
void bsp_for_each_leaf(bsp_tree *t, bsp_leaf_fn fn, void *priv);

#endif /* __CLAP_BSP_H__ */
