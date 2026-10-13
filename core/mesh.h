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
    vec3                aabb[2];
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

/**
 * mesh_attr_resize() - resize a given attribute in a mesh
 * @mesh:   mesh
 * @attr:   attribute
 * @nr:     new number of elements
 *
 * Resize attribute's backing buffer and set the number of elements
 * accordingly. Resizing to zero elements frees the buffer, leaving the
 * attribute empty. If allocation fails, a shrinking resize leaves the
 * attribute as it was, while a growing one frees and clears it: the
 * attribute vanishes instead of standing too small for the data
 * that was headed its way.
 * Return: CERR_OK on success or CERR_NOMEM on failure to allocate.
 */
cerr mesh_attr_resize(struct mesh *mesh, enum mesh_attrs attr, size_t nr);
void mesh_push_mesh(struct mesh *mesh, struct mesh *src,
                    float x, float y, float z, float scale);

void mesh_aabb_calc(struct mesh *mesh);
void mesh_optimize(struct mesh *mesh);

/**
 * mesh_tangents_calc() - derive per-vertex tangents from UVs
 * @mesh:   mesh with positions, texture coordinates, normals and indices
 *
 * Compute the vec4 tangent attribute (xyz along +U, w the bitangent
 * handedness) that tangent-space normal mapping needs, accumulated over
 * triangles and orthonormalized against the vertex normal. Loaded meshes
 * ship authored tangents; this is for meshes that come without, and a
 * mesh that already has tangents is left alone.
 * Return: CERR_OK, CERR_NOMEM, or CERR_INVALID_ARGUMENTS on a missing
 * attribute or an unexpected layout.
 */
cerr_check mesh_tangents_calc(struct mesh *mesh);

/**
 * mesh_make_collision() - build a position-only welded copy for collision
 * @mesh:       source mesh (16-bit indexed, vec3 positions)
 * @out_vx:     receives the welded position buffer (caller frees)
 * @out_vxsz:   receives its size in bytes
 * @out_idx:    receives the remapped 16-bit index buffer (caller frees)
 * @out_idxsz:  receives its size in bytes
 *
 * Collapses the render mesh's normal/UV vertex seams so the trimesh is
 * topologically connected, which ODE and the navmesh both need. The
 * render mesh is left untouched.
 * Return: CERR_OK or CERR_INVALID_ARGUMENTS (empty or unexpected layout)
 */
cerr_check mesh_make_collision(struct mesh *mesh, float **out_vx, size_t *out_vxsz,
                               unsigned short **out_idx, size_t *out_idxsz);

/**
 * mesh_flatten() - produce a contiguous buffer with multiple mesh attributes interleaved
 * @mesh:       source of the attributes
 * @attrs:      array of attributes as enum mesh_attrs
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
cresp(void) mesh_flatten(struct mesh *mesh, const enum mesh_attrs *attrs,
                         size_t *offs, unsigned int nr_attrs, unsigned int stride);
size_t mesh_idx_to_lod(struct mesh *mesh, int lod, unsigned short **idx, float *error);

/**
 * struct mesh_part - the triangles one joint of a skinned mesh carries
 * @vx:     the piece's vertices, as the mesh has them (the bind pose)
 * @idx:    its triangles, into @vx
 * @joint:  the joint
 */
struct mesh_part {
    darray(vec3,            vx);
    darray(unsigned int,    idx);
    unsigned int            joint;
};

cresp_ret(mesh_part, struct);

/**
 * typedef mesh_parts - a skinned mesh's pieces, one per joint
 */
typedef darray(struct mesh_part, mesh_parts);

/**
 * mesh_part_done() - free a piece's vertices and triangles
 * @part:   the piece
 */
void mesh_part_done(struct mesh_part *part);

/**
 * mesh_parts_done() - free every piece, and the array
 * @parts:  the pieces
 */
void mesh_parts_done(mesh_parts *parts);

/**
 * mesh_part_find() - find a joint's piece
 * @parts:  the pieces
 * @joint:  the joint
 *
 * Return: the piece; CERR_NOT_FOUND if @joint has none
 */
cresp_check(mesh_part) mesh_part_find(mesh_parts *parts, unsigned int joint);

/**
 * mesh_parts_by_joint() - split a skinned mesh into rigid pieces by joint
 * @mesh:   a mesh with joints and weights
 * @idx:    the triangles to split, a LOD's, say; nullptr: the mesh's own
 * @stride: the bytes of an index of @idx, 2 or 4; the mesh's own
 *          (mesh_idx_stride()) without @idx
 * @nr_idx: the number of indices in @idx
 * @parts:  the pieces, each joint's after what it has already
 *
 * Each vertex belongs to the joint weighing on it the most, each triangle
 * to the joint two of its vertices share, or to its first vertex's.
 * Return: CERR_OK, CERR_INVALID_ARGUMENTS for a mesh without skinning or
 * a @stride that isn't 2 or 4, CERR_NOMEM
 */
cerr_check mesh_parts_by_joint(struct mesh *mesh, const void *idx, size_t stride, size_t nr_idx,
                               mesh_parts *parts);

/**
 * mesh_parts_merge() - add one set of pieces to another
 * @parts:  the set that grows: a joint's piece gets the vertices and
 *          triangles of @from's piece of the same joint
 * @from:   the pieces to add; left empty
 *
 * A rig built of several meshes, split mesh by mesh, makes one set: a
 * joint's piece, whichever meshes it comes from, is one shape.
 * Return: CERR_OK or CERR_NOMEM, which leaves @from as it was and @parts
 * with the pieces merged before it ran out
 */
cerr_check mesh_parts_merge(mesh_parts *parts, mesh_parts *from);
void mesh_init(void);

#endif /* __CLAP_MESH_H__ */
