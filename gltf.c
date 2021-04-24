#include <errno.h>
#include "base64.h"
#include "common.h"
#include "json.h"
#include "librarian.h"
#include "model.h"
#include "object.h"
#include "pngloader.h"
#include "scene.h"
#include "shader.h"

#define DATA_URI "data:application/octet-stream;base64,"

struct gltf_bufview {
    unsigned int buffer;
    size_t       offset;
    size_t       length;
};

enum {
    T_VEC2 = 0,
    T_VEC3,
    T_VEC4,
    T_MAT4,
    T_SCALAR,
};

static const char *types[] = {
    [T_VEC2] = "VEC2",
    [T_VEC3] = "VEC3",
    [T_VEC4] = "VEC4",
    [T_MAT4] = "MAT4",
    [T_SCALAR] = "SCALAR"
};

struct gltf_accessor {
    unsigned int bufview;
    unsigned int comptype;
    unsigned int count;
    unsigned int type;
};

struct gltf_mesh {
    const char   *name;
    unsigned int indices;
    unsigned int material;
    unsigned int POSITION;
    unsigned int NORMAL;
    unsigned int TEXCOORD_0;
    unsigned int COLOR_0;
    unsigned int JOINTS_0;
    unsigned int WEIGHTS_0;
};

enum {
    I_LINEAR = 0,
};

struct glft_anisampler {
    unsigned int    input;
    unsigned int    output;
    unsigned int    interp;
};

struct glft_animation {
    const char             *name;
    struct glft_anisampler *samplers;
    unsigned int           nr_samplers;
};

struct gltf_data {
    struct scene *scene;
    void         **buffers;
    struct gltf_bufview *bufvws;
    struct gltf_accessor *accrs;
    struct gltf_mesh     *meshes;
    unsigned int         *imgs;
    unsigned int         *texs;
    unsigned int         *mats;
    unsigned int nr_buffers;
    unsigned int nr_bufvws;
    unsigned int nr_meshes;
    unsigned int nr_accrs;
    unsigned int nr_imgs;
    unsigned int nr_texs;
    unsigned int nr_mats;
    unsigned int texid;
};

void gltf_free(struct gltf_data *gd)
{
    int i;

    for (i = 0; i < gd->nr_buffers; i++)
        free(gd->buffers[i]);
    free(gd->buffers);

    for (i = 0; i < gd->nr_meshes; i++)
        free((void *)gd->meshes[i].name);
    free(gd->meshes);
    free(gd->bufvws);
    free(gd->accrs);
    free(gd->imgs);
    free(gd->texs);
    free(gd->mats);
    free(gd);
}

int gltf_get_meshes(struct gltf_data *gd)
{
    return gd->nr_meshes;
}

int gltf_mesh(struct gltf_data *gd, const char *name)
{
    int i;

    for (i = 0; i < gd->nr_meshes; i++)
        if (!strcasecmp(name, gd->meshes[i].name))
            return i;

    return -1;
}

const char *gltf_mesh_name(struct gltf_data *gd, int mesh)
{
    if (mesh < 0 || mesh >= gd->nr_meshes)
        return NULL;
    return gd->meshes[mesh].name;
}

void *gltf_accessor_buf(struct gltf_data *gd, int accr)
{
    int bv = gd->accrs[accr].bufview;
    int buf = gd->bufvws[bv].buffer;
    void *data = gd->buffers[buf];

    return data + gd->bufvws[bv].offset;
}

unsigned int gltf_accessor_sz(struct gltf_data *gd, int accr)
{
    int bv = gd->accrs[accr].bufview;

    return gd->bufvws[bv].length;
}

float *gltf_vx(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].POSITION);
}

unsigned int gltf_vxsz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].POSITION);
}

unsigned short *gltf_idx(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].indices);
}

unsigned int gltf_idxsz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].indices);
}

float *gltf_tx(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].TEXCOORD_0);
}

unsigned int gltf_txsz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].TEXCOORD_0);
}

float *gltf_norm(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].NORMAL);
}

unsigned int gltf_normsz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].NORMAL);
}

float *gltf_color(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].COLOR_0);
}

unsigned int gltf_colorsz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].COLOR_0);
}

float *gltf_joints(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].JOINTS_0);
}

unsigned int gltf_jointssz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].JOINTS_0);
}

float *gltf_weights(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_buf(gd, gd->meshes[mesh].WEIGHTS_0);
}

unsigned int gltf_weightssz(struct gltf_data *gd, int mesh)
{
    return gltf_accessor_sz(gd, gd->meshes[mesh].WEIGHTS_0);
}

void *gltf_tex(struct gltf_data *gd, int mesh)
{
    int mat = gd->meshes[mesh].material;
    int tex = gd->mats[mat];
    int img = gd->texs[tex];
    int bv = gd->imgs[img];
    int buf = gd->bufvws[bv].buffer;
    void *data = gd->buffers[buf];

    return data + gd->bufvws[bv].offset;
}

unsigned int gltf_texsz(struct gltf_data *gd, int mesh)
{
    int mat = gd->meshes[mesh].material;
    int tex = gd->mats[mat];
    int img = gd->texs[tex];
    int bv = gd->imgs[img];

    return gd->bufvws[bv].length;
}

static void gltf_onload(struct lib_handle *h, void *data)
{
    JsonNode *root = json_decode(h->buf);
    JsonNode *nodes, *mats, *meshes, *texs, *imgs, *accrs, *bufvws, *bufs;
    struct gltf_data *gd = data;
    JsonNode *n;

    dbg("loading '%s'\n", h->name);
    if (!root) {
        warn("couldn't parse '%s'\n", h->name);
        return;
    }

    nodes = json_find_member(root, "nodes");
    mats = json_find_member(root, "materials");
    meshes = json_find_member(root, "meshes");
    texs = json_find_member(root, "textures");
    imgs = json_find_member(root, "images");
    accrs = json_find_member(root, "accessors");
    bufvws = json_find_member(root, "bufferViews");
    bufs = json_find_member(root, "buffers");
    if (nodes->tag != JSON_ARRAY ||
        mats->tag != JSON_ARRAY ||
        meshes->tag != JSON_ARRAY ||
        texs->tag != JSON_ARRAY ||
        imgs->tag != JSON_ARRAY ||
        accrs->tag != JSON_ARRAY ||
        bufvws->tag != JSON_ARRAY ||
        bufs->tag != JSON_ARRAY) {
        dbg("type error %d/%d/%d/%d/%d/%d/%d\n",
            mats->tag, meshes->tag, texs->tag, imgs->tag,
            accrs->tag, bufvws->tag, bufs->tag
        );
        return;
    }

    /* Nodes */
    for (n = nodes->children.head; n; n = n->next) {
        JsonNode *jname, *jmesh;//, *jskin, *jchildren, *jrot, *jtrans;

        if (n->tag != JSON_OBJECT)
            continue;

        jname = json_find_member(n, "name");
        jmesh = json_find_member(n, "mesh");
        /*jskin = json_find_member(n, "skin");
        jchildren = json_find_member(n, "children");
        jrot = json_find_member(n, "rotation");
        jtrans = json_find_member(n, "translation");*/
        if (!jname || !jmesh) /* actually, there only name is guaranteed */
            continue;
    }
    
    /* Buffers */
    for (n = bufs->children.head; n; n = n->next) {
        JsonNode *jlen, *juri;
        size_t   len, dlen, slen;

        if (n->tag != JSON_OBJECT)
            continue;

        jlen = json_find_member(n, "byteLength");
        juri = json_find_member(n, "uri");
        if (!jlen && !juri)
            continue;

        len = jlen->number_;
        if (juri->tag != JSON_STRING ||
            strlen(juri->string_) < sizeof(DATA_URI) - 1 ||
            strncmp(juri->string_, DATA_URI, sizeof(DATA_URI) - 1))
            continue;

        slen = strlen(juri->string_) - sizeof(DATA_URI) + 1;
        len = max(len, base64_decoded_length(slen));

        CHECK(gd->buffers = realloc(gd->buffers, (gd->nr_buffers + 1) * sizeof(void *)));
        CHECK(gd->buffers[gd->nr_buffers] = malloc(len));
        dlen = base64_decode(gd->buffers[gd->nr_buffers], len, juri->string_ + sizeof(DATA_URI) - 1, slen);
        // dbg("buffer %d: byteLength=%d uri length=%d dlen=%d/%d slen=%d '%.10s' errno=%d\n",
        //     gd->nr_buffers, (int)jlen->number_,
        //     strlen(juri->string_), dlen, len,
        //     slen, juri->string_ + sizeof(DATA_URI) - 1, errno);
        gd->nr_buffers++;
    }

    /* BufferViews */
    for (n = bufvws->children.head; n; n = n->next) {
        JsonNode *jbuf, *jlen, *joff;

        jbuf = json_find_member(n, "buffer");
        jlen = json_find_member(n, "byteLength");
        joff = json_find_member(n, "byteOffset");
        if (!jbuf || !jlen || !joff)
            continue;

        if (jbuf->number_ >= gd->nr_buffers)
            continue;

        CHECK(gd->bufvws = realloc(gd->bufvws, (gd->nr_bufvws + 1) * sizeof(struct gltf_bufview)));
        gd->bufvws[gd->nr_bufvws].buffer = jbuf->number_;
        gd->bufvws[gd->nr_bufvws].offset = joff->number_;
        gd->bufvws[gd->nr_bufvws].length = jlen->number_;
        // dbg("buffer view %d: buf %d offset %zu size %zu\n", gd->nr_bufvws, gd->bufvws[gd->nr_bufvws].buffer,
        //     gd->bufvws[gd->nr_bufvws].offset, gd->bufvws[gd->nr_bufvws].length);
        gd->nr_bufvws++;
    }

    /* Accessors */
    for (n = accrs->children.head; n; n = n->next) {
        JsonNode *jbufvw, *jcount, *jtype, *jcomptype;
        int i;
        
        jbufvw = json_find_member(n, "bufferView");
        jcount = json_find_member(n, "count");
        jtype = json_find_member(n, "type");
        jcomptype = json_find_member(n, "componentType");
        if (!jbufvw || !jcount || !jtype || !jcomptype)
            continue;
        
        if (jbufvw->number_ >= gd->nr_bufvws)
            continue;
        
        for (i = 0; i < array_size(types); i++)
            if (!strcmp(types[i], jtype->string_))
                break;
        
        if (i == array_size(types))
            continue;

        CHECK(gd->accrs = realloc(gd->accrs, (gd->nr_accrs + 1) * sizeof(struct gltf_accessor)));
        gd->accrs[gd->nr_accrs].bufview = jbufvw->number_;
        gd->accrs[gd->nr_accrs].comptype = jcomptype->number_;
        gd->accrs[gd->nr_accrs].count = jcount->number_;
        gd->accrs[gd->nr_accrs].type = i;
        // dbg("accessor %d: bufferView: %d count: %d componentType: %d type: %s\n", gd->nr_accrs,
        //     gd->accrs[gd->nr_accrs].bufview, gd->accrs[gd->nr_accrs].count, gd->accrs[gd->nr_accrs].comptype,
        //     types[i]);
        gd->nr_accrs++;
    }

    /* Images */
    for (n = imgs->children.head; n; n = n->next) {
        JsonNode *jbufvw, *jmime, *jname;

        jbufvw = json_find_member(n, "bufferView");
        jmime = json_find_member(n, "mimeType");
        jname = json_find_member(n, "name");
        if (!jbufvw || !jmime || !jname)
            continue;
        
        if (strcmp(jmime->string_, "image/png"))
            continue;

        if (jbufvw->number_ >= gd->nr_bufvws)
            continue;
        
        CHECK(gd->imgs = realloc(gd->imgs, (gd->nr_imgs + 1) * sizeof(unsigned int)));
        gd->imgs[gd->nr_imgs] = jbufvw->number_;
        // dbg("image %d: bufferView: %d\n", gd->nr_imgs, gd->imgs[gd->nr_imgs]);
        gd->nr_imgs++;
    }

    /* Textures */
    for (n = texs->children.head; n; n = n->next) {
        JsonNode *jsrc; // mipmapping: *jsampler

        //jsampler = json_find_member(n, "sampler");
        jsrc = json_find_member(n, "source");
        if (!jsrc)
            continue;
        if (jsrc->number_ >= gd->nr_imgs)
            continue;

        CHECK(gd->texs = realloc(gd->texs, (gd->nr_texs + 1) * sizeof(unsigned int)));
        gd->texs[gd->nr_texs] = jsrc->number_;
        // dbg("texture %d: source: %d\n", gd->nr_texs, gd->texs[gd->nr_texs]);
        gd->nr_texs++;
    }

    /* Materials */
    for (n = mats->children.head; n; n = n->next) {
        JsonNode *jwut;

        jwut = json_find_member(n, "pbrMetallicRoughness");
        if (!jwut)
            continue;
        if (jwut->tag != JSON_OBJECT)
            continue;
        jwut = json_find_member(jwut, "baseColorTexture");
        if (!jwut)
            continue;
        if (jwut->tag != JSON_OBJECT)
            continue;
        jwut = json_find_member(jwut, "index");
        if (jwut->tag != JSON_NUMBER || jwut->number_ >= gd->nr_texs)
            continue;

        CHECK(gd->mats = realloc(gd->mats, (gd->nr_mats + 1) * sizeof(unsigned int)));
        gd->mats[gd->nr_mats] = jwut->number_;
        // dbg("material %d: texture: %d\n", gd->nr_mats, gd->mats[gd->nr_mats]);
        gd->nr_mats++;
    }

    for (n = meshes->children.head; n; n = n->next) {
        JsonNode *jname, *jprim, *jattr, *jindices, *jmat, *p;

        jname = json_find_member(n, "name"); /* like, "Cube", thanks blender */
        jprim = json_find_member(n, "primitives");
        if (!jname || !jprim)
            continue;
        if (jprim->tag != JSON_ARRAY)
            continue;

        /* XXX: why is this an array? */
        jprim = jprim->children.head;
        if (!jprim)
            continue;
        jindices = json_find_member(jprim, "indices");
        jmat = json_find_member(jprim, "material");
        jattr = json_find_member(jprim, "attributes");
        if (!jattr || jattr->tag != JSON_OBJECT || !jindices || !jmat)
            continue;

        CHECK(gd->meshes = realloc(gd->meshes, (gd->nr_meshes + 1) * sizeof(struct gltf_mesh)));
        gd->meshes[gd->nr_meshes].name = strdup(jname->string_);
        gd->meshes[gd->nr_meshes].indices = jindices->number_;
        gd->meshes[gd->nr_meshes].material = jmat->number_;
        for (p = jattr->children.head; p; p = p->next) {
            if (!strcmp(p->key, "POSITION") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].POSITION = p->number_;
            else if (!strcmp(p->key, "NORMAL") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].NORMAL = p->number_;
            else if (!strcmp(p->key, "TEXCOORD_0") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].TEXCOORD_0 = p->number_;
            else if (!strcmp(p->key, "COLOR_0") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].COLOR_0 = p->number_;
            else if (!strcmp(p->key, "JOINTS_0") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].JOINTS_0 = p->number_;
            else if (!strcmp(p->key, "WEIGHTS_0") && p->tag == JSON_NUMBER)
                gd->meshes[gd->nr_meshes].WEIGHTS_0 = p->number_;
        }
        // dbg("mesh %d: '%s' POSITION: %d\n", gd->nr_meshes, jname->string_, gd->meshes[gd->nr_meshes].POSITION);
        gd->nr_meshes++;
    }

    json_free(root);
    ref_put(&h->ref);

    return;
}

void gltf_mesh_data(struct gltf_data *gd, int mesh, float **vx, size_t *vxsz, unsigned short **idx, size_t *idxsz,
                    float **tx, size_t *txsz, float **norm, size_t *normsz)
{
    if (mesh < 0 || mesh >= gd->nr_meshes)
        return;

    if (vx) {
        *vx = gltf_vx(gd, mesh);
        *vxsz = gltf_vxsz(gd, mesh);
    }
    if (idx) {
        *idx = gltf_idx(gd, mesh);
        *idxsz = gltf_idxsz(gd, mesh);
    }
    if (tx) {
        *tx = gltf_tx(gd, mesh);
        *txsz = gltf_txsz(gd, mesh);
    }
    if (norm) {
        *norm = gltf_norm(gd, mesh);
        *normsz = gltf_normsz(gd, mesh);
    }
}

void gltf_instantiate_one(struct gltf_data *gd, int mesh)
{
    struct model3dtx *txm;
    struct model3d   *m;

    if (mesh < 0 || mesh >= gd->nr_meshes)
        return;

    m = model3d_new_from_vectors(gltf_mesh_name(gd, mesh), gd->scene->prog,
                                 gltf_vx(gd, mesh), gltf_vxsz(gd, mesh),
                                 gltf_idx(gd, mesh), gltf_idxsz(gd, mesh),
                                 gltf_tx(gd, mesh), gltf_txsz(gd, mesh),
                                 gltf_norm(gd, mesh), gltf_normsz(gd, mesh));
    gd->scene->_model = m;
    txm = model3dtx_new_from_buffer(gd->scene->_model, gltf_tex(gd, mesh), gltf_texsz(gd, mesh));
    ref_put(&m->ref);
    scene_add_model(gd->scene, txm);
}

void gltf_instantiate_all(struct gltf_data *gd)
{
    int i;

    for (i = 0; i < gd->nr_meshes; i++)
        gltf_instantiate_one(gd, i);
}

struct gltf_data *gltf_load(struct scene *scene, const char *name)
{
    struct gltf_data  *gd;
    struct lib_handle *lh;

    CHECK(gd = calloc(1, sizeof(*gd)));
    gd->scene = scene;
    lh = lib_request(RES_ASSET, name, gltf_onload, gd);
    ref_put(&lh->ref);

    return gd;
}
