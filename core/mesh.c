#include <meshoptimizer.h>
#include <stdlib.h>
#include <limits.h>
#include "common.h"
#include "mesh.h"

static void mesh_drop(struct ref *ref)
{
    struct mesh *mesh = container_of(ref, struct mesh, ref);
    int i;

    for (i = 0; i < MESH_MAX; i++) {
        struct mesh_attr *ma = mesh_attr(mesh, i);

        if (!ma->nr)
            continue;
        
        mem_free(ma->data);
        ma->nr = ma->stride = 0;
    }
}
DECLARE_REFCLASS(mesh);

struct mesh *mesh_new(const char *name)
{
    struct mesh *mesh;

    CHECK(mesh = ref_new(mesh));
    mesh->name = name;

    return mesh;
}

int mesh_attr_add(struct mesh *mesh, unsigned int attr, void *data, size_t stride, size_t nr)
{
    if (attr >= MESH_MAX)
        return -1;

    mesh->attr[attr].data = data;
    mesh->attr[attr].stride = stride;
    mesh->attr[attr].nr = nr;
    mesh->attr[attr].type = attr;

    return 0;
}

int mesh_attr_alloc(struct mesh *mesh, unsigned int attr, size_t stride, size_t nr)
{
    if (attr >= MESH_MAX)
        return -1;

    mesh->attr[attr].data = mem_alloc(stride, .nr = nr, .fatal_fail = 1);
    mesh->attr[attr].stride = stride;
    mesh->attr[attr].nr = 0;
    mesh->attr[attr].type = attr;

    return 0;
}

int mesh_attr_dup(struct mesh *mesh, unsigned int attr, void *data, size_t stride, size_t nr)
{
    if (mesh_attr_alloc(mesh, attr, stride, nr) < 0)
        return -1;

    memcpy(mesh->attr[attr].data, data, stride * nr);
    mesh->attr[attr].nr = nr;

    return 0;
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
            CHECK(ma->data = realloc(ma->data, nr_new_vx * ma->stride));
            ma->nr = nr_new_vx;
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
            CHECK(ma->data = realloc(ma->data, nr_new_vx * ma->stride));
            ma->nr = nr_new_vx;
        }
    }
    mem_free(remap);

    mesh_idx_from_idx32(mesh, idx32);
}

ssize_t mesh_idx_to_lod(struct mesh *mesh, int lod, unsigned short **idx, size_t orig_idx)
{
    struct mesh_attr *vxa = mesh_attr(mesh, MESH_VX);
    int nr_idx, target = orig_idx / (1 << (lod + 1));
    float target_error = 1e-2;
    unsigned int *idx32;

    idx32 = mesh_idx_to_idx32(mesh);
    nr_idx = meshopt_simplify(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                              target, 0.02f, 0, &target_error);

#define goodenough(_new, _orig) ((_new) * 11 / 10 < (_orig))
    if (!goodenough(nr_idx, orig_idx) || nr_idx < 0) {
        nr_idx = meshopt_simplifySloppy(idx32, idx32, orig_idx, vxa->data, vxa->nr, vxa->stride,
                                        target, 0.02f, &target_error);
        if (!goodenough(nr_idx, orig_idx)) {
            nr_idx = -1;
            goto out;
        }
    }

    *idx = idx32_to_idx(idx32, nr_idx);
out:
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
