#ifndef __CLAP_MESH_H__
#define __CLAP_MESH_H__

#include <sys/types.h>
#include "error.h"
#include "object.h"
#include "datatypes.h"

enum mesh_attrs {
    MESH_VX = 0,
    MESH_TX,
    MESH_NORM,
    MESH_TANGENTS,
    MESH_WEIGHTS,
    MESH_IDX,
    MESH_JOINTS,
    MESH_MAX
};

#define MESH_VX_BIT         (1 << MESH_VX)
#define MESH_TX_BIT         (1 << MESH_TX)
#define MESH_NORM_BIT       (1 << MESH_NORM)
#define MESH_TANGENTS_BIT   (1 << MESH_TANGENTS)
#define MESH_WEIGHTS_BIT    (1 << MESH_WEIGHTS)
#define MESH_IDX_BIT        (1 << MESH_IDX)
#define MESH_JOINTS_BIT     (1 << MESH_JOINTS)

#define MESH_VX_ATTRS ( \
    MESH_VX_BIT | \
    MESH_TX_BIT | \
    MESH_NORM_BIT | \
    MESH_TANGENTS_BIT | \
    MESH_WEIGHTS_BIT |\
    MESH_JOINTS_BIT \
)

/**
 * mesh_attr_type() - get the data type of a mesh attribute
 * @a:  mesh (vertex) attribute
 *
 * Get a data type of a mesh attribute element for a given vertex attribute.
 * Return: data type
 */
data_type mesh_attr_type(enum mesh_attrs a);

/**
 * mesh_attr_comp_count() - get the number of components of a vertex attribute
 * @a:  mesh (vertex) attribute
 *
 * Get the number of components in a mesh attribute element, corresponding to
 * the data_type returned by mesh_attr_type(). For vectors it would be 1. For
 * joints it would be 4. A vector can be represented by either multiple scalar
 * of a certain type (DT_FLOAT) or a single vector (DT_VEC*). Joints is an
 * exception, because it's a 4-component vector of DT_BYTEs.
 * Return: the number of components
 */
unsigned int mesh_attr_comp_count(enum mesh_attrs a);

struct mesh_attr {
    void            *data;
    size_t          nr;
    unsigned int    stride;
    enum mesh_attrs type;
};

struct mesh {
    struct ref          ref;
    const char          *name;
    size_t              idxsz;
    struct mesh_attr    attr[MESH_MAX];
    float               aabb[6];
    bool                fix_origin;
};
typedef struct mesh mesh_t;

DEFINE_REFCLASS_INIT_OPTIONS(mesh,
    const char *name;
    bool       fix_origin;
);
DECLARE_REFCLASS(mesh);
DECLARE_CLEANUP(mesh_t);

static inline struct mesh_attr *mesh_attr(struct mesh *mesh, enum mesh_attrs attr)
{
    err_on(attr >= MESH_MAX);
    return &mesh->attr[attr];
}

static inline size_t mesh_nr(struct mesh *mesh, enum mesh_attrs attr)
{
    err_on(attr >= MESH_MAX);
    return mesh->attr[attr].nr;
}

static inline size_t mesh_stride(struct mesh *mesh, enum mesh_attrs attr)
{
    err_on(attr >= MESH_MAX);
    return mesh->attr[attr].stride;
}

static inline size_t mesh_sz(struct mesh *mesh, enum mesh_attrs attr)
{
    err_on(attr >= MESH_MAX);
    return mesh->attr[attr].nr * mesh->attr[attr].stride;
}

#define ATTR_ACCESSORS(_n, _N, _t) \
static inline _t *mesh_ ## _n(struct mesh *mesh) \
{ \
    return mesh->attr[MESH_ ## _N].data; \
} \
static inline size_t mesh_nr_ ## _n(struct mesh *mesh) \
{ \
    return mesh_nr(mesh, MESH_ ## _N); \
} \
static inline size_t mesh_ ## _n ## _stride(struct mesh *mesh) \
{ \
    return mesh_stride(mesh, MESH_ ## _N); \
} \
static inline size_t mesh_ ## _n ## _sz(struct mesh *mesh) \
{ \
    return mesh_sz(mesh, MESH_ ## _N); \
}

ATTR_ACCESSORS(vx, VX, float);
ATTR_ACCESSORS(tx, TX, float);
ATTR_ACCESSORS(norm, NORM, float);
ATTR_ACCESSORS(tangent, TANGENTS, float);
ATTR_ACCESSORS(joints, JOINTS, unsigned char);
ATTR_ACCESSORS(weights, WEIGHTS, float);
ATTR_ACCESSORS(idx, IDX, unsigned short);

cerr_check mesh_attr_add(struct mesh *mesh, enum mesh_attrs attr, void *data, size_t stride, size_t nr);
cerr_check mesh_attr_alloc(struct mesh *mesh, enum mesh_attrs attr, size_t stride, size_t nr);
cerr_check mesh_attr_dup(struct mesh *mesh, enum mesh_attrs attr, void *data, size_t stride, size_t nr);
void mesh_push_mesh(struct mesh *mesh, struct mesh *src,
                    float x, float y, float z, float scale);

void mesh_aabb_calc(struct mesh *mesh);
void mesh_optimize(struct mesh *mesh);

/**
 * mesh_flatten() - produce a contiguous buffer with multiple mesh attributes interleaved
 * @mesh:       source of the attributes
 * @attrs:      array of attributes as enum mesh_attrs
 * @sizes:      element size per attribute
 * @offs:       element offset from the beginning of a stride per attribute
 * @nr_attrs:   number of attributes
 * @stride:     total length of a vertex stride
 *
 * Most of the above, except for @attrs itself, can be calculated here, but
 * these are shader properties, calculated once at shader load time, and this
 * function runs for every model3d creation.
 *
 * Return: void * of the interleaved buffer on success, CERR on failure
 */
cresp(void) mesh_flatten(struct mesh *mesh, const enum mesh_attrs *attrs, size_t *sizes,
                         size_t *offs, unsigned int nr_attrs, unsigned int stride);
size_t mesh_idx_to_lod(struct mesh *mesh, int lod, unsigned short **idx, float *error);
void mesh_init(void);

#endif /* __CLAP_MESH_H__ */
