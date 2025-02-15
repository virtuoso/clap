#ifndef __CLAP_MESH_H__
#define __CLAP_MESH_H__

#include <sys/types.h>
#include "error.h"
#include "logger.h"
#include "object.h"

enum {
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

struct mesh_attr {
    void            *data;
    size_t          nr;
    unsigned int    stride;
    unsigned int    type;
};

struct mesh {
    struct ref          ref;
    const char          *name;
    size_t              idxsz;
    struct mesh_attr    attr[MESH_MAX];
};
typedef struct mesh mesh_t;

DEFINE_REFCLASS_INIT_OPTIONS(mesh);
DECLARE_REFCLASS(mesh);
DECLARE_CLEANUP(mesh_t);

static inline struct mesh_attr *mesh_attr(struct mesh *mesh, unsigned int attr)
{
    err_on(attr >= MESH_MAX);
    return &mesh->attr[attr];
}

static inline size_t mesh_nr(struct mesh *mesh, unsigned int attr)
{
    err_on(attr >= MESH_MAX);
    return mesh->attr[attr].nr;
}

static inline size_t mesh_stride(struct mesh *mesh, unsigned int attr)
{
    err_on(attr >= MESH_MAX);
    return mesh->attr[attr].stride;
}

static inline size_t mesh_sz(struct mesh *mesh, int attr)
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

int mesh_attr_add(struct mesh *mesh, unsigned int attr, void *data, size_t stride, size_t nr);
int mesh_attr_alloc(struct mesh *mesh, unsigned int attr, size_t stride, size_t nr);
int mesh_attr_dup(struct mesh *mesh, unsigned int attr, void *data, size_t stride, size_t nr);
struct mesh *mesh_new(const char *name);
void mesh_push_mesh(struct mesh *mesh, struct mesh *src,
                    float x, float y, float z, float scale);

void mesh_push(float *vx, float *tx, float *norm, unsigned short *idx,
               int *nr_vx, int *nr_tx, int *nr_idx,
               float *src_vx, float *src_tx, float *src_norm, unsigned short *src_idx,
               size_t nr_src_vx, size_t nr_src_idx,
               float x, float y, float z, float scale);
void mesh_optimize(struct mesh *mesh);
ssize_t mesh_idx_to_lod(struct mesh *mesh, int lod, unsigned short **idx, size_t orig_idx);
void mesh_init(void);

#endif /* __CLAP_MESH_H__ */
