#include "cerrs.h"
#include "logger.h"
#include "memory.h"
#include <meshoptimizer.h>
#include <stdlib.h>
#include <limits.h>
#include "datatypes.h"
#include "linmath.h"
#include "mesh.h"
#include "util.h"

static cerr mesh_make(struct ref *ref, void *_opts)
{
    rc_init_opts(mesh) *opts = _opts;

    if (!opts->name)
        return CERR_INVALID_ARGUMENTS;

    struct mesh *mesh = container_of(ref, struct mesh, ref);
    mesh->name = opts->name;
    mesh->fix_origin = opts->fix_origin;

    return CERR_OK;
}

static void mesh_drop(struct ref *ref)
{
    struct mesh *mesh = container_of(ref, struct mesh, ref);
    int i;

    for (i = 0; i < MESH_MAX; i++) {
        struct mesh_attr *ma = mesh_attr(mesh, i);

        if (!ma->data)
            continue;
        
        mem_free(ma->data);
        ma->nr = ma->stride = 0;
    }
}
DEFINE_REFCLASS2(mesh);

DEFINE_CLEANUP(mesh_t, if (*p) ref_put(*p))

static inline bool attr_is_valid(struct mesh *mesh, enum mesh_attrs attr, size_t nr)
{
    if (attr != MESH_VX && attr != MESH_IDX && mesh->attr[MESH_VX].nr != nr)
        return false;

    return true;
}

static const data_type attr_comp_type[MESH_MAX] = {
    [MESH_VX]       = DT_VEC3,
    [MESH_TX]       = DT_VEC2,
    [MESH_NORM]     = DT_VEC3,
    [MESH_TANGENTS] = DT_VEC4,
    [MESH_WEIGHTS]  = DT_VEC4,
    [MESH_JOINTS]   = DT_UVEC4,
};

static const unsigned int attr_comp_count[MESH_MAX] = {
    [MESH_VX]       = 1,
    [MESH_TX]       = 1,
    [MESH_NORM]     = 1,
    [MESH_TANGENTS] = 1,
    [MESH_WEIGHTS]  = 1,
    [MESH_JOINTS]   = 1,
};

data_type mesh_attr_type(enum mesh_attrs a)
{
    return attr_comp_type[a];
}

unsigned int mesh_attr_comp_count(enum mesh_attrs a)
{
    return attr_comp_count[a];
}

cerr mesh_attr_add(struct mesh *mesh, enum mesh_attrs attr, void *data, size_t stride, size_t nr)
{
    if (attr >= MESH_MAX && attr_is_valid(mesh, attr, nr))
        return CERR_INVALID_ARGUMENTS;

    mesh->attr[attr].data = data;
    mesh->attr[attr].stride = stride;
    mesh->attr[attr].nr = nr;
    mesh->attr[attr].type = attr;

    if (attr == MESH_VX)
        vertex_array_aabb_calc(mesh->aabb, data, nr * stride, stride);
    /* @data doesn't belong to us, mesh->fix_origin does not apply */

    return CERR_OK;
}

cerr mesh_attr_alloc(struct mesh *mesh, enum mesh_attrs attr, size_t stride, size_t nr)
{
    if (attr >= MESH_MAX && attr_is_valid(mesh, attr, nr))
        return CERR_INVALID_ARGUMENTS;

    mem_free(mesh->attr[attr].data);
    mesh->attr[attr].data = mem_alloc(stride, .nr = nr, .fatal_fail = 1);
    mesh->attr[attr].stride = stride;
    mesh->attr[attr].nr = 0;
    mesh->attr[attr].type = attr;

    return CERR_OK;
}

cerr mesh_attr_dup(struct mesh *mesh, enum mesh_attrs attr, void *data, size_t stride, size_t nr)
{
    if (attr == MESH_JOINTS && stride == 4) {
        CERR_RET_CERR(mesh_attr_alloc(mesh, attr, stride * data_type_size(DT_INT), nr));

        unsigned char *src = data;
        unsigned int *dst = mesh->attr[attr].data;
        for (size_t i = 0; i < nr; i++)
            for (size_t j = 0; j < stride; j++)
                dst[i * stride + j] = src[i * stride + j];
    } else {
        CERR_RET_CERR(mesh_attr_alloc(mesh, attr, stride, nr));
        memcpy(mesh->attr[attr].data, data, stride * nr);
    }

    mesh->attr[attr].nr = nr;

    if (attr == MESH_VX) {
        vertex_array_aabb_calc(mesh->aabb, data, nr * stride, stride);
        if (mesh->fix_origin)
            vertex_array_fix_origin(mesh_vx(mesh), nr * stride, stride, mesh->aabb);
    }

    return CERR_OK;
}

cerr mesh_attr_resize(struct mesh *mesh, enum mesh_attrs attr, size_t nr)
{
    auto ma = &mesh->attr[attr];

    // nothing to hold: free the buffer here, as realloc() to zero bytes
    // may free it and return NULL (glibc, ASan), which would read as a
    // failed shrink that kept it
    if (!nr) {
        mem_free(ma->data);
        ma->data = nullptr;
        ma->nr = 0;
        return CERR_OK;
    }

    auto resized = mem_realloc_array(ma->data, nr, ma->stride);

    if (!resized) {
        // a failed grow nukes the attribute: an attribute that can't
        // hold the vertices about to be emitted does more damage than
        // a missing one; a failed shrink keeps it, the data is whole
        if (nr > ma->nr) {
            mem_free(ma->data);
            ma->data = nullptr;
            ma->nr = 0;
            return CERR_NOMEM_REASON(
                .fmt  = "mesh '%s': dropped attr %u on a failed grow to %zu",
                .arg0 = mesh->name,
                .arg1 = (void *)(uintptr_t)attr,
                .arg2 = (void *)(uintptr_t)nr
            );
        }
        return CERR_NOMEM;
    }

    ma->data = resized;
    ma->nr = min(ma->nr, nr);

    return CERR_OK;
}

void mesh_aabb_calc(struct mesh *mesh)
{
    if (!mesh_vx(mesh) || !mesh_vx_sz(mesh))
        return;

    vertex_array_aabb_calc(mesh->aabb, mesh_vx(mesh), mesh_vx_sz(mesh), mesh_vx_stride(mesh));
}

cerr mesh_tangents_calc(struct mesh *mesh)
{
    size_t nr_vx = mesh_nr_vx(mesh), nr_idx = mesh_nr_idx(mesh);

    if (mesh_nr_tangent(mesh))                                                          return CERR_OK;
    if (!nr_vx || !nr_idx || mesh_nr_tx(mesh) != nr_vx || mesh_nr_norm(mesh) != nr_vx)  return CERR_INVALID_ARGUMENTS;
    if (mesh_vx_stride(mesh) != sizeof(float) * 3 || mesh_tx_stride(mesh) != sizeof(float) * 2 ||
        mesh_norm_stride(mesh) != sizeof(float) * 3 || mesh_idx_stride(mesh) != sizeof(unsigned short))
        return CERR_INVALID_ARGUMENTS;

    LOCAL_SET(float, tan) = mem_alloc(sizeof(float) * 3, .nr = nr_vx * 2, .zero = 1);
    if (!tan)   return CERR_NOMEM;

    CERR_RET_CERR(mesh_attr_alloc(mesh, MESH_TANGENTS, sizeof(float) * 4, nr_vx));

    float *vx = mesh_vx(mesh), *tx = mesh_tx(mesh), *norm = mesh_norm(mesh);
    unsigned short *idx = mesh_idx(mesh);
    float *bitan = tan + nr_vx * 3;

    for (size_t t = 0; t + 2 < nr_idx; t += 3) {
        unsigned short i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        vec3 tdir, bdir;

        if (!triangle_tangent(&vx[i0 * 3], &vx[i1 * 3], &vx[i2 * 3],
                              &tx[i0 * 2], &tx[i1 * 2], &tx[i2 * 2], tdir, bdir))
            continue;

        for (int c = 0; c < 3; c++) {
            unsigned short i = idx[t + c];

            vec3_add(&tan[i * 3], &tan[i * 3], tdir);
            vec3_add(&bitan[i * 3], &bitan[i * 3], bdir);
        }
    }

    float *out = mesh_tangent(mesh);
    for (size_t i = 0; i < nr_vx; i++)  tangent_orthonormalize(&norm[i * 3], &tan[i * 3], &bitan[i * 3], &out[i * 4]);

    mesh_attr(mesh, MESH_TANGENTS)->nr = nr_vx;

    return CERR_OK;
}

static unsigned int *idx_to_idx32(unsigned short *idx, size_t nr_idx)
{
    unsigned int *idx32;
    int i;

    idx32 = mem_alloc(nr_idx * sizeof(*idx32), .fatal_fail = 1);
    for (i = 0; i < nr_idx; i++)
        idx32[i] = idx[i];

    return idx32;
}

static unsigned short *idx32_to_idx(unsigned int *idx32, size_t nr_idx)
{
    unsigned short *idx;
    int i;

    idx = mem_alloc(nr_idx * sizeof(*idx), .fatal_fail = 1);
    for (i = 0; i < nr_idx; i++) {
        /* XXX: CU() */
        if (idx32[i] > USHRT_MAX) {
            mem_free(idx);
            return NULL;
        }
        idx[i] = idx32[i];
    }

    return idx;
}

unsigned int *mesh_idx_to_idx32(struct mesh *mesh)
{
    if (mesh_idx_stride(mesh) == sizeof(unsigned int))
        return mesh->attr[MESH_IDX].data;

    return idx_to_idx32(mesh_idx(mesh), mesh_nr_idx(mesh));
}

void mesh_idx_from_idx32(struct mesh *mesh, unsigned int *idx32)
{
    mem_free(mesh->attr[MESH_IDX].data);
    mesh->attr[MESH_IDX].data = idx32_to_idx(idx32, mesh_nr_idx(mesh));
    mesh->attr[MESH_IDX].stride = sizeof(unsigned short); /* because GLES/WebGL */
    mem_free(idx32);
    /* XXX: propagate the error up the stack */
    assert(mesh_idx(mesh));
}

unsigned short *mesh_lod_from_idx32(struct mesh *mesh, unsigned int *idx32)
{
    unsigned int i, nr_idx = mesh_nr_idx(mesh);
    unsigned short *idx;

    idx = mem_alloc(sizeof(*idx), .nr = nr_idx, .fatal_fail = 1);
    for (i = 0; i < nr_idx; i++)
        idx[i] = idx32[i];
    mem_free(idx32);

    return idx;
}

#define test_bit(mask, bit) (!!((mask) & (1 << (bit))))

#define MESH_1to1_ATTRS (MESH_TX_BIT | MESH_NORM_BIT | MESH_TANGENTS_BIT | MESH_WEIGHTS_BIT)

void mesh_push_mesh(struct mesh *mesh, struct mesh *src,
                    float x, float y, float z, float scale)
{
    size_t nr_vx = mesh_nr_vx(mesh);
    struct mesh_attr *ma, *ma_src;
    unsigned short *idx, *idx_src;
    float *vx, *vx_src;
    int i, attr;

    /* texture coordinates, normals and joint weights are 1:1 */
    for (attr = 0; attr < MESH_MAX; attr++) {
        ma = mesh_attr(mesh, attr);
        ma_src = mesh_attr(src, attr);

        if (!test_bit(MESH_1to1_ATTRS, attr))
            continue;

        if (!ma_src->nr || ma->stride != ma_src->stride)
            continue;
        
        memcpy(ma->data + mesh_sz(mesh, attr), ma_src->data, ma->stride * ma_src->nr);
        ma->nr += ma_src->nr;
    }

    /* vertices require translation */
    ma = mesh_attr(mesh, MESH_VX);
    ma_src = mesh_attr(src, MESH_VX);
    err_on(!ma_src->nr || ma->stride != ma_src->stride);
    vx = ma->data + mesh_vx_sz(mesh);
    vx_src = ma_src->data;
    for (i = 0; i < mesh_nr_vx(src); i++) {
        vx[i * 3 + 0] = x + vx_src[i * 3 + 0] * scale;
        vx[i * 3 + 1] = y + vx_src[i * 3 + 1] * scale;
        vx[i * 3 + 2] = z + vx_src[i * 3 + 2] * scale;
    }
    ma->nr += ma_src->nr;

    /* indices require mapping */
    ma = mesh_attr(mesh, MESH_IDX);
    ma_src = mesh_attr(src, MESH_IDX);
    err_on(!ma_src->nr || ma->stride != ma_src->stride);
    idx = ma->data + mesh_idx_sz(mesh);
    idx_src = ma_src->data;
    for (i = 0; i < mesh_nr_idx(src); i++) {
        idx[i] = nr_vx + idx_src[i];
    }
    ma->nr += ma_src->nr;
}

void mesh_optimize(struct mesh *mesh)
{
    struct mesh_attr *vxa = mesh_attr(mesh, MESH_VX);
    size_t nr_new_vx, nr_vx = mesh_nr_vx(mesh);
    struct meshopt_Stream streams[MESH_MAX];
    size_t nr_idx = mesh_nr_idx(mesh);
    unsigned int *remap, *idx32;
    struct mesh_attr *ma;
    int attr, nr_streams;

    for (attr = 0, nr_streams = 0; attr < MESH_MAX; attr++) {
        ma = mesh_attr(mesh, attr);

        if (!test_bit(MESH_VX_ATTRS, attr) || !ma->nr)
            continue;
        
        streams[nr_streams].data   = ma->data;
        streams[nr_streams].size   = ma->stride;
        streams[nr_streams].stride = ma->stride;
        nr_streams++;
    }

    err_on(!nr_streams);
    remap = mem_alloc(sizeof(*remap), .nr = nr_idx, .fatal_fail = 1);
    idx32 = mesh_idx_to_idx32(mesh);
    nr_new_vx = meshopt_generateVertexRemapMulti(remap, idx32, nr_idx, nr_vx, streams, nr_streams);
    dbg("remapping mesh vertices vx: %zd -> %zd\n", nr_vx, nr_new_vx);

    meshopt_remapIndexBuffer(idx32, idx32, nr_idx, remap);
    for (attr = 0; attr < MESH_MAX; attr++) {
        ma = mesh_attr(mesh, attr);

        if (!test_bit(MESH_VX_ATTRS, attr) || !ma->nr)
            continue;
        
        meshopt_remapVertexBuffer(ma->data, ma->data, ma->nr, ma->stride, remap);
        if (nr_new_vx < ma->nr) {
            /* Failure to shrink is not fatal, it's safe to proceed */
            CERR_RET(
                mesh_attr_resize(mesh, attr, nr_new_vx),
                err_cerr(__cerr, "mesh '%s' attr %d resize failed\n", mesh->name, attr);
            );
        }
    }
    mem_free(remap);

    meshopt_optimizeVertexCache(idx32, idx32, nr_idx, nr_new_vx);
    meshopt_optimizeOverdraw(idx32, idx32, nr_idx, vxa->data, vxa->nr, vxa->stride, 1.05f);

    remap = mem_alloc(sizeof(*remap), .nr = vxa->nr, .fatal_fail = 1);
    nr_new_vx = meshopt_optimizeVertexFetchRemap(remap, idx32, nr_idx, vxa->nr);
    meshopt_remapIndexBuffer(idx32, idx32, nr_idx, remap);

    for (attr = 0; attr < MESH_MAX; attr++) {
        ma = mesh_attr(mesh, attr);

        if (!test_bit(MESH_VX_ATTRS, attr) || !ma->nr)
            continue;
        
        meshopt_remapVertexBuffer(ma->data, ma->data, ma->nr, ma->stride, remap);
        if (nr_new_vx < ma->nr) {
            /* Failure to shrink is not fatal, it's safe to proceed */
            CERR_RET(
                mesh_attr_resize(mesh, attr, nr_new_vx),
                err_cerr(__cerr, "mesh '%s' attr %d resize failed\n", mesh->name, attr);
            );
        }
    }
    mem_free(remap);

    mesh_idx_from_idx32(mesh, idx32);
}

cerr mesh_make_collision(struct mesh *mesh, float **out_vx, size_t *out_vxsz,
                         unsigned short **out_idx, size_t *out_idxsz)
{
    size_t nr_vx = mesh_nr_vx(mesh);
    size_t nr_idx = mesh_nr_idx(mesh);
    size_t stride = mesh_vx_stride(mesh);
    unsigned short *sidx = mesh_idx(mesh);

    /*
     * The render mesh splits vertices at normal/UV seams: one position
     * becomes several vertices with different attributes, so triangles
     * that share an edge geometrically don't share it by index. A
     * collision/navigation trimesh wants the opposite - one vertex per
     * position, so the surface is topologically connected (ODE's
     * trimesh and the navmesh both rely on it). Remap by position only.
     */
    if (!nr_vx || !nr_idx || stride != 3 * sizeof(float) ||
        mesh_idx_stride(mesh) != sizeof(unsigned short))
        return CERR_INVALID_ARGUMENTS;

    unsigned int *idx32 = idx_to_idx32(sidx, nr_idx);
    unsigned int *remap = mem_alloc(sizeof(*remap), .nr = nr_vx, .fatal_fail = 1);

    size_t nr_new = meshopt_generateVertexRemap(remap, idx32, nr_idx,
                                                mesh_vx(mesh), nr_vx, stride);

    /* the render mesh is 16-bit indexed, so the welded count fits too */
    float *vx = mem_alloc(stride, .nr = nr_new, .fatal_fail = 1);

    meshopt_remapVertexBuffer(vx, mesh_vx(mesh), nr_vx, stride, remap);
    meshopt_remapIndexBuffer(idx32, idx32, nr_idx, remap);
    unsigned short *idx = idx32_to_idx(idx32, nr_idx);

    mem_free(idx32);
    mem_free(remap);

    trace("collision mesh '%s': %zu -> %zu verts by position weld\n",
          mesh->name, nr_vx, nr_new);

    *out_vx = vx;
    *out_vxsz = nr_new * stride;
    *out_idx = idx;
    *out_idxsz = nr_idx * sizeof(*idx);

    return CERR_OK;
}

static inline void *mesh_attr_element(struct mesh *mesh, enum mesh_attrs attr, size_t el)
{
    struct mesh_attr *ma = mesh_attr(mesh, attr);

    if (!ma || ma->nr <= el)
        return NULL;

    return ma->data + ma->stride * el;
}

void mesh_part_done(struct mesh_part *part)
{
    darray_clearout(part->vx);
    darray_clearout(part->idx);
}

void mesh_parts_done(mesh_parts *parts)
{
    struct mesh_part *part;

    darray_for_each(part, *parts)
        mesh_part_done(part);
    darray_clearout(*parts);
}

cresp(mesh_part) mesh_part_find(mesh_parts *parts, unsigned int joint)
{
    struct mesh_part *part;

    darray_for_each(part, *parts)
        if (part->joint == joint)   return cresp_val(mesh_part, part);

    return cresp_error(mesh_part, CERR_NOT_FOUND);
}

/* @joint's piece, a new empty one if it has none yet */
static struct mesh_part *mesh_part_get(mesh_parts *parts, unsigned int joint)
{
    cresp(mesh_part) found = mesh_part_find(parts, joint);
    if (!IS_CERR(found))    return found.val;

    struct mesh_part *part = darray_add(*parts);
    if (!part)  return nullptr;

    part->joint = joint;
    darray_init(part->vx);
    darray_init(part->idx);

    return part;
}

/* the joint weighing on vertex @v the most */
static unsigned int vertex_joint(struct mesh *mesh, size_t v)
{
    const unsigned int *joints = mesh_attr_element(mesh, MESH_JOINTS, v);
    const float *weights = mesh_attr_element(mesh, MESH_WEIGHTS, v);
    unsigned int best = 0;

    for (unsigned int i = 1; i < 4; i++)
        if (weights[i] > weights[best]) best = i;

    return joints[best];
}

/* element @i of an index buffer @stride bytes wide */
static inline unsigned int idx_get(const void *idx, size_t stride, size_t i)
{
    return stride == sizeof(uint32_t) ? ((const uint32_t *)idx)[i] : ((const uint16_t *)idx)[i];
}

cerr mesh_parts_by_joint(struct mesh *mesh, const void *idx, size_t stride, size_t nr_idx, mesh_parts *parts)
{
    size_t nr_vx = mesh_nr_vx(mesh);

    if (mesh_nr(mesh, MESH_JOINTS) != nr_vx || mesh_nr(mesh, MESH_WEIGHTS) != nr_vx)    return CERR_INVALID_ARGUMENTS;

    if (!idx) {
        idx = mesh_idx(mesh);
        stride = mesh_idx_stride(mesh);
        nr_idx = mesh_nr_idx(mesh);
    }
    if (stride != sizeof(uint16_t) && stride != sizeof(uint32_t))   return CERR_INVALID_ARGUMENTS;

    size_t nr_tri = nr_idx / 3;
    if (!nr_vx || !nr_tri)  return CERR_INVALID_ARGUMENTS;

    /* each triangle's joint, -1 once it's in its part */
    LOCAL_SET(int, tri_joint) = mem_alloc(sizeof(*tri_joint), .nr = nr_tri);
    /* each vertex's index in the part being collected, -1 for not in it yet */
    LOCAL_SET(int, slot) = mem_alloc(sizeof(*slot), .nr = nr_vx);
    if (!tri_joint || !slot)    return CERR_NOMEM;

    for (size_t t = 0; t < nr_tri; t++) {
        unsigned int j0 = vertex_joint(mesh, idx_get(idx, stride, t * 3 + 0));
        unsigned int j1 = vertex_joint(mesh, idx_get(idx, stride, t * 3 + 1));
        unsigned int j2 = vertex_joint(mesh, idx_get(idx, stride, t * 3 + 2));

        tri_joint[t] = j1 == j2 ? j1 : j0;
    }

    for (size_t first = 0; first < nr_tri; first++) {
        int joint = tri_joint[first];

        if (joint < 0)  continue;

        struct mesh_part *part = mesh_part_get(parts, joint);
        if (!part)  return CERR_NOMEM;

        memset(slot, 0xff, nr_vx * sizeof(*slot));
        for (size_t t = first; t < nr_tri; t++) {
            if (tri_joint[t] != joint)  continue;

            tri_joint[t] = -1;
            for (unsigned int k = 0; k < 3; k++) {
                unsigned int v = idx_get(idx, stride, t * 3 + k);

                if (slot[v] < 0) {
                    vec3 *dst = darray_add(part->vx);
                    if (!dst)   return CERR_NOMEM;

                    vec3_dup(*dst, mesh_attr_element(mesh, MESH_VX, v));
                    slot[v] = darray_count(part->vx) - 1;
                }

                unsigned int *dst = darray_add(part->idx);
                if (!dst)   return CERR_NOMEM;
                *dst = slot[v];
            }
        }
    }

    return CERR_OK;
}

cresp(void) mesh_flatten(struct mesh *mesh, const enum mesh_attrs *attrs,
                         size_t *offs, unsigned int nr_attrs, unsigned int stride)
{
    size_t total_size = stride * mesh_nr_vx(mesh);

    LOCAL_SET(void, flat) = mem_alloc(total_size, .zero = 1);
    if (!flat)
        return cresp_error(void, CERR_NOMEM);

    for (size_t el = 0; el < mesh_nr(mesh, MESH_VX); el++) {
        for (int i = 0; i < nr_attrs; i++) {
            enum mesh_attrs attr = attrs[i];
            void *dest = flat + stride * el + offs[i];

            struct mesh_attr *ma = mesh_attr(mesh, attr);
            if (ma && ma->data) {
                void *src = mesh_attr_element(mesh, attr, el);
                err_on(!src, "mesh[%s] attr[%d] el[%zu]\n", mesh->name, attr, el);
                memcpy(dest, src, ma->stride);
            }
        }
    }

    return cresp_val(void, NOCU(flat));
}

size_t mesh_idx_to_lod(struct mesh *mesh, int lod, unsigned short **idx, float *error)
{
    struct mesh_attr *vxa = mesh_attr(mesh, MESH_VX);
    size_t orig_idx = mesh_nr_idx(mesh);
    size_t nr_idx, target = orig_idx / (1 << (lod + 1));
    float result_error = 0.0, target_error = 0.01f + 0.02f * lod;
    unsigned int *idx32;

    /* first, try 50% */
    idx32 = mesh_idx_to_idx32(mesh);
    nr_idx = meshopt_simplify(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                              target, target_error, 0, &result_error);

    if (nr_idx <= target)
        goto out;

    /* then, try 75% */
    target += orig_idx / (1 << (lod + 2));
    mem_free(idx32);
    idx32 = mesh_idx_to_idx32(mesh);
    nr_idx = meshopt_simplify(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                              target, target_error, 0, &result_error);

    if (nr_idx <= target)
        goto out;

    /* then, try 87.5% */
    target += orig_idx / (1 << (lod + 3));
    mem_free(idx32);
    idx32 = mesh_idx_to_idx32(mesh);
    nr_idx = meshopt_simplify(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                              target, target_error, 0, &result_error);

    if (nr_idx <= target)
        goto out;

    /* then, fall back to slappy */
    mem_free(idx32);
    idx32 = mesh_idx_to_idx32(mesh);
    nr_idx = meshopt_simplifySloppy(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                                    nullptr, target, target_error, &result_error);
    result_error = -result_error;

out:
    *error = result_error;
    *idx = idx32_to_idx(idx32, nr_idx);
    mem_free(idx32);

    return nr_idx;
}

static void *meshopt_alloc(size_t size)
{
    return mem_alloc(size);
}

static void meshopt_dealloc(void *ptr)
{
    mem_free(ptr);
}

void mesh_init(void)
{
    meshopt_setAllocator(meshopt_alloc, meshopt_dealloc);
}
