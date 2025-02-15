// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <inttypes.h>
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
    [T_VEC2]   = "VEC2",
    [T_VEC3]   = "VEC3",
    [T_VEC4]   = "VEC4",
    [T_MAT4]   = "MAT4",
    [T_SCALAR] = "SCALAR"
};

static const size_t typesz[] = {
    [T_VEC2]   = 2,
    [T_VEC3]   = 3,
    [T_VEC4]   = 4,
    [T_MAT4]   = 16,
    [T_SCALAR] = 1,
};

/* these correspond to GL_* macros */
enum {
    COMP_BYTE           = 0x1400,
    COMP_UNSIGNED_BYTE  = 0x1401,
    COMP_SHORT          = 0x1402,
    COMP_UNSIGNED_SHORT = 0x1403,
    COMP_INT            = 0x1404,
    COMP_UNSIGNED_INT   = 0x1405,
    COMP_FLOAT          = 0x1406,
    COMP_2_BYTES        = 0x1407,
    COMP_3_BYTES        = 0x1408,
    COMP_4_BYTES        = 0x1409,
    COMP_DOUBLE         = 0x140A,
};

/* XXX: this is wasteful for a lookup table, use a switch instead */
static const size_t comp_sizes[] = {
    [COMP_BYTE]           = 1,
    [COMP_UNSIGNED_BYTE]  = 1,
    [COMP_SHORT]          = 2,
    [COMP_UNSIGNED_SHORT] = 2,
    [COMP_INT]            = 4,
    [COMP_UNSIGNED_INT]   = 4,
    [COMP_FLOAT]          = 4,
    [COMP_2_BYTES]        = 2,
    [COMP_3_BYTES]        = 3,
    [COMP_4_BYTES]        = 4,
    [COMP_DOUBLE]         = 8,
};

struct gltf_accessor {
    unsigned int bufview;
    unsigned int comptype;
    unsigned int count;
    unsigned int type;
    size_t       offset;
};

struct gltf_node {
    const char  *name;
    quat        rotation;
    vec3        scale;
    vec3        translation;
    struct list children;
    struct list entry;
    int         mesh;
    int         skin;
    unsigned int id;
    unsigned int nr_children;
    int         *ch_arr;
};

struct gltf_skin {
    mat4x4      *invmxs;
    const char  *name;
    int         *joints;
    int         *nodes;
    unsigned int nr_joints;
    unsigned int nr_invmxs;
};

struct gltf_mesh {
    const char  *name;
    int         indices;
    int         material;
    int         POSITION;
    int         NORMAL;
    int         TEXCOORD_0;
    int         TANGENT;
    int         COLOR_0;
    int         JOINTS_0;
    int         WEIGHTS_0;
};

static void gltf_mesh_init(struct gltf_mesh *mesh, const char *name,
                           int indices, int material)
{
    mesh->name = strdup(name);
    mesh->indices = indices;
    mesh->material = material;
    mesh->POSITION = -1;
    mesh->NORMAL = -1;
    mesh->TEXCOORD_0 = -1;
    mesh->TANGENT = -1;
    mesh->COLOR_0 = -1;
    mesh->JOINTS_0 = -1;
    mesh->WEIGHTS_0 = -1;
}

enum {
    I_STEP = 0,
    I_LINEAR,
    I_CUBICSPLINE,
    I_NONE,
};

static const char *interps[] = {
    [I_STEP]        = "STEP",
    [I_LINEAR]      = "LINEAR",
    [I_CUBICSPLINE] = "CUBICSPLINE",
    [I_NONE]        = "NONE"
};

struct gltf_sampler {
    int    input;
    int    output;
    int    interp;
};

static const char *paths[] = {
    [PATH_TRANSLATION] = "translation",
    [PATH_ROTATION]    = "rotation",
    [PATH_SCALE]       = "scale",
    [PATH_NONE]        = "none",
};

struct gltf_channel {
    int             sampler;
    int             node;
    enum chan_path  path;
};

struct gltf_animation {
    const char                  *name;
    darray(struct gltf_sampler, samplers);
    darray(struct gltf_channel, channels);
};

struct gltf_material {
    int    base_tex;
    int    normal_tex;
    int    emission_tex;
    double metallic;
    double roughness;
};

struct gltf_data {
    struct scene *scene;
    darray(void *,                buffers);
    darray(struct gltf_bufview,   bufvws);
    darray(struct gltf_accessor,  accrs);
    darray(struct gltf_mesh,      meshes);
    darray(struct gltf_material,  mats);
    darray(struct gltf_node,      nodes);
    darray(struct gltf_animation, anis);
    darray(struct gltf_skin,      skins);
    // struct darray        texs;
    darray(int,                   imgs);
    unsigned int         *texs;
    void                 *bin;
    int                  root_node;
    unsigned int nr_texs;
    unsigned int texid;
};

void gltf_free(struct gltf_data *gd)
{
    struct gltf_skin *skin;
    struct gltf_node *node;
    int i;

    for (i = 0; i < gd->anis.da.nr_el; i++) {
        struct gltf_animation *ani = DA(gd->anis, i);
        free((void *)ani->name);

        darray_clearout(ani->channels);
        darray_clearout(ani->samplers);
    }

    for (i = 0; i < gd->buffers.da.nr_el; i++)
        free(*DA(gd->buffers, i));

    for (i = 0; i < gd->meshes.da.nr_el; i++)
        free((void *)DA(gd->meshes, i)->name);
    darray_for_each(node, gd->nodes) {
        free((void *)node->name);
        free(node->ch_arr);
    }
    darray_for_each(skin, gd->skins) {
        free((void *)skin->joints);
        free(skin->nodes);
        free((void *)skin->name);
    }
    darray_clearout(gd->buffers);
    darray_clearout(gd->bufvws);
    darray_clearout(gd->meshes);
    darray_clearout(gd->accrs);
    darray_clearout(gd->nodes);
    darray_clearout(gd->mats);
    darray_clearout(gd->anis);
    darray_clearout(gd->skins);
    darray_clearout(gd->imgs);
    free(gd->texs);
    free(gd);
}

int gltf_get_meshes(struct gltf_data *gd)
{
    return gd->meshes.da.nr_el;
}

int gltf_mesh_by_name(struct gltf_data *gd, const char *name)
{
    int i;

    for (i = 0; i < darray_count(gd->meshes); i++)
        if (!strcasecmp(name, DA(gd->meshes, i)->name))
            return i;

    return -1;
}

struct gltf_mesh *gltf_mesh(struct gltf_data *gd, int mesh)
{
    return DA(gd->meshes, mesh);
}

const char *gltf_mesh_name(struct gltf_data *gd, int mesh)
{
    struct gltf_mesh *m = gltf_mesh(gd, mesh);

    if (!m)
        return NULL;

    return m->name;
}

struct gltf_accessor *gltf_accessor(struct gltf_data *gd, int accr)
{
    struct gltf_accessor *ga = DA(gd->accrs, accr);

    if (!ga)
        return NULL;

    return ga;
}

size_t gltf_accessor_stride(struct gltf_data *gd, int accr)
{
    struct gltf_accessor *ga = gltf_accessor(gd, accr);

    return typesz[ga->type] * comp_sizes[ga->comptype];
}

size_t gltf_accessor_nr(struct gltf_data *gd, int accr)
{
    struct gltf_accessor *ga = gltf_accessor(gd, accr);

    return ga->count;
}

struct gltf_bufview *gltf_bufview_accr(struct gltf_data *gd, int accr)
{
    struct gltf_accessor *ga = DA(gd->accrs, accr);

    if (!ga)
        return NULL;

    return DA(gd->bufvws, ga->bufview);
}

struct gltf_bufview *gltf_bufview_tex(struct gltf_data *gd, int tex)
{
    if (tex >= gd->nr_texs || tex < 0)
        return NULL;

    unsigned int img = gd->texs[tex];
    if (img >= darray_count(gd->imgs))
        return NULL;

    int bv = *DA(gd->imgs, img);
    if (bv >= darray_count(gd->bufvws) || bv < 0)
        return NULL;

    return DA(gd->bufvws, bv);
}

void *gltf_accessor_buf(struct gltf_data *gd, int accr)
{
    struct gltf_accessor *ga = DA(gd->accrs, accr);
    struct gltf_bufview *bv;

    bv = gltf_bufview_accr(gd, accr);
    if (!bv)
        return NULL;

    return *DA(gd->buffers, bv->buffer) + ga->offset + bv->offset;
}

void *gltf_accessor_element(struct gltf_data *gd, int accr, size_t el)
{
    struct gltf_accessor *ga = DA(gd->accrs, accr);
    struct gltf_bufview *bv;
    size_t elsz;
    void *buf;

    bv = gltf_bufview_accr(gd, accr);
    if (!bv)
        return NULL;

    buf = *DA(gd->buffers, bv->buffer) + ga->offset + bv->offset;
    elsz = comp_sizes[ga->comptype];

    return buf + elsz * el;
}

unsigned int gltf_accessor_sz(struct gltf_data *gd, int accr)
{
    struct gltf_bufview *bv = gltf_bufview_accr(gd, accr);

    if (!bv)
        return 0;

    return bv->length;
}

#define GLTF_MESH_ATTR(_attr, _name, _type) \
_type *gltf_ ## _name(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_mesh *m = gltf_mesh(gd, mesh); \
    return m ? gltf_accessor_buf(gd, m->_attr) : NULL; \
} \
unsigned int gltf_ ## _name ## sz(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_mesh *m = gltf_mesh(gd, mesh); \
    return m ? gltf_accessor_sz(gd, m->_attr) : 0; \
} \
bool gltf_has_ ## _name(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_mesh *m = gltf_mesh(gd, mesh); \
    return m ? (m->_attr != -1) : false; \
} \
size_t gltf_ ## _name ## _stride(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_mesh *m = gltf_mesh(gd, mesh); \
    if (!m) return 0; \
    return gltf_accessor_stride(gd, m->_attr); \
} \
size_t gltf_nr_ ## _name(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_mesh *m = gltf_mesh(gd, mesh); \
    if (!m) return 0; \
    return gltf_accessor_nr(gd, m->_attr); \
}

GLTF_MESH_ATTR(POSITION,   vx,      float)
GLTF_MESH_ATTR(indices,    idx,     unsigned short)
GLTF_MESH_ATTR(TEXCOORD_0, tx,      float)
GLTF_MESH_ATTR(NORMAL,     norm,    float)
GLTF_MESH_ATTR(TANGENT,    tangent, float)
GLTF_MESH_ATTR(COLOR_0,    color,   float)
GLTF_MESH_ATTR(JOINTS_0,   joints,  unsigned char)
GLTF_MESH_ATTR(WEIGHTS_0,  weights, float)

struct gltf_material *gltf_material(struct gltf_data *gd, int mesh)
{
    struct gltf_mesh *m = gltf_mesh(gd, mesh);

    if (!m)
        return NULL;

    return DA(gd->mats, m->material);
}

#define GLTF_MAT_TEX(_attr, _name) \
bool gltf_has_ ## _name(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_material *mat = gltf_material(gd, mesh); \
    int tex; \
    if (!mat) \
        return false; \
    tex = mat->_attr ## _tex; \
    return tex >= 0; \
} \
void *gltf_ ## _name(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_material *mat = gltf_material(gd, mesh); \
 \
    if (!mat) \
        return NULL; \
 \
    int tex = mat->_attr ## _tex; \
    struct gltf_bufview *bv = gltf_bufview_tex(gd, tex); \
 \
    if (!bv) \
        return NULL; \
 \
    return *DA(gd->buffers, bv->buffer) + bv->offset; \
} \
unsigned int gltf_ ## _name ## sz(struct gltf_data *gd, int mesh) \
{ \
    struct gltf_material *mat = gltf_material(gd, mesh); \
 \
    if (!mat) \
        return 0; \
 \
    int tex = mat->_attr ## _tex; \
    struct gltf_bufview *bv = gltf_bufview_tex(gd, tex); \
 \
    if (!bv) \
        return 0; \
 \
    return bv->length; \
}

GLTF_MAT_TEX(base, tex)
GLTF_MAT_TEX(normal, nmap)
GLTF_MAT_TEX(emission, em)

int gltf_root_mesh(struct gltf_data *gd)
{
    int root = gd->root_node;

    /* not detected -- use mesh 0 */
    if (root < 0)
        return 0;

    return DA(gd->nodes, root)->mesh;
}

int gltf_mesh_skin(struct gltf_data *gd, int mesh)
{
    int i;

    if (!gltf_has_joints(gd, mesh) || !gltf_has_weights(gd, mesh))
        return -1;

    for (i = 0; i < gd->nodes.da.nr_el; i++)
        if (DA(gd->nodes, i)->mesh == mesh && DA(gd->nodes, i)->skin >= 0)
            return DA(gd->nodes, i)->skin;
    return -1;
}

bool gltf_mesh_is_skinned(struct gltf_data *gd, int mesh)
{
    int skin = gltf_mesh_skin(gd, mesh);

    if (skin >= 0)
        return true;

    return false;
}

static void __unused nodes_print(struct gltf_data *gd, struct gltf_node *node, int level)
{
    int child;

    list_init(&node->children);
    dbg("%.*s-> node %d '%s'\n", level, "----------", node->id, node->name);
    for (child = 0; child < node->nr_children; child++) {
        nodes_print(gd, DA(gd->nodes, node->ch_arr[child]), level + 1);
        list_append(&node->children, &DA(gd->nodes, node->ch_arr[child])->entry);
    }
}

static void gltf_load_animations(struct gltf_data *gd, JsonNode *anis)
{
    JsonNode *n;

    if (!anis)
        return;

    /* Animations */
    for (n = anis->children.head; n; n = n->next) {
        JsonNode *jname, *jchans, *jsamplers, *jn;
        struct gltf_animation *ani;

        jname = json_find_member(n, "name");
        jchans = json_find_member(n, "channels");
        jsamplers = json_find_member(n, "samplers");

        CHECK(ani = darray_add(gd->anis));
        if (jname && jname->tag == JSON_STRING)
            ani->name = strdup(jname->string_);

        darray_init(ani->channels);
        darray_init(ani->samplers);
        for (jn = jchans->children.head; jn; jn = jn->next) {
            JsonNode *jsampler, *jtarget, *jnode, *jpath;
            struct gltf_channel *chan;

            CHECK(chan = darray_add(ani->channels));
            chan->sampler = -1;
            chan->node = -1;
            chan->path = PATH_NONE;

            if (jn->tag != JSON_OBJECT)
                continue;

            jsampler = json_find_member(jn, "sampler");
            if (jsampler && jsampler->tag == JSON_NUMBER)
                chan->sampler = jsampler->number_;
            jtarget = json_find_member(jn, "target");
            if (jtarget && jtarget->tag == JSON_OBJECT) {
                int i;

                jnode = json_find_member(jtarget, "node");
                if (jnode && jnode->tag == JSON_NUMBER)
                    chan->node = jnode->number_;
                jpath = json_find_member(jtarget, "path");
                if (jpath && jpath->tag == JSON_STRING) {
                    for (i = 0; i < array_size(paths); i++)
                        if (!strcmp(paths[i], jpath->string_))
                            goto found_path;
                    continue;
                found_path:
                    chan->path = i;
                }
                // dbg("## chan node: %d path: %s\n", chan->node, paths[chan->path]);
            }
        }

        for (jn = jsamplers->children.head; jn; jn = jn->next) {
            JsonNode *jinput, *joutput, *jinterp;
            struct gltf_sampler *sampler;
            int i;

            CHECK(sampler = darray_add(ani->samplers));
            sampler->input = -1;
            sampler->output = -1;
            sampler->interp = -1;

            if (jn->tag != JSON_OBJECT)
                continue;

            jinput = json_find_member(jn, "input");
            if (jinput && jinput->tag == JSON_NUMBER)
                sampler->input = jinput->number_;

            joutput = json_find_member(jn, "output");
            if (joutput && joutput->tag == JSON_NUMBER)
                sampler->output = joutput->number_;
            jinterp = json_find_member(jn, "interpolation");
            if (jinterp && jinterp->tag == JSON_STRING) {
                for (i = 0; i < array_size(interps); i++)
                    if (!strcmp(interps[i], jinterp->string_))
                        goto found_interp;
                continue;
            found_interp:
                sampler->interp = i;
            }
            // dbg("## sampler input: %d output: %d interp: %s\n",
            //     sampler->input, sampler->output, interps[sampler->interp]);
        }
    }
}

static void gltf_load_skins(struct gltf_data *gd, JsonNode *skins)
{
    JsonNode *n;

    if (!skins)
        return;

    for (n = skins->children.head; n; n = n->next) {
        JsonNode *jmat, *jname, *jjoints;
        struct gltf_skin *skin;

        CHECK(skin = darray_add(gd->skins));

        jmat = json_find_member(n, "inverseBindMatrices");
        if (jmat && jmat->tag == JSON_NUMBER) {
            struct gltf_accessor *ga = DA(gd->accrs, (int)jmat->number_);

            skin->invmxs = gltf_accessor_buf(gd, jmat->number_);
            skin->nr_invmxs = ga->count;
        }

        jname = json_find_member(n, "name");
        if (jname && jname->tag == JSON_STRING)
            skin->name = strdup(jname->string_);

        jjoints = json_find_member(n, "joints");
        if (jjoints && jjoints->tag == JSON_ARRAY) {
            int j;
            skin->joints = json_int_array_alloc(jjoints, &skin->nr_joints);
            skin->nodes = mem_alloc(sizeof(int), .nr = skin->nr_joints);
            for (j = 0; j < skin->nr_joints; j++)
                skin->nodes[skin->joints[j]] = j;
        }
        dbg("skin '%s' nr_joints: %d\n", skin->name, skin->nr_joints);
    }
}

static cerr gltf_json_parse(const char *buf, struct gltf_data *gd)
{
    JsonNode *nodes, *mats, *meshes, *texs, *imgs, *accrs, *bufvws, *bufs;
    JsonNode *scenes, *scene, *skins, *anis;
    LOCAL(JsonNode, root);
    unsigned int nid;
    JsonNode *n;

    root = json_decode(buf);
    if (!root)
        return CERR_PARSE_FAILED;

    gd->root_node = -1;
    darray_init(gd->nodes);
    darray_init(gd->meshes);
    darray_init(gd->bufvws);
    darray_init(gd->mats);
    darray_init(gd->accrs);
    darray_init(gd->buffers);
    darray_init(gd->anis);
    darray_init(gd->skins);
    darray_init(gd->imgs);

    scenes = json_find_member(root, "scenes");
    scene = json_find_member(root, "scene");
    nodes = json_find_member(root, "nodes");
    mats = json_find_member(root, "materials");
    meshes = json_find_member(root, "meshes");
    anis = json_find_member(root, "animations");
    texs = json_find_member(root, "textures");
    imgs = json_find_member(root, "images");
    skins = json_find_member(root, "skins");
    accrs = json_find_member(root, "accessors");
    bufvws = json_find_member(root, "bufferViews");
    bufs = json_find_member(root, "buffers");

#define GLTF_CHECK_PROP(_node, _name, _tag) \
    if (!(_node)) { \
        warn("GLTF doesn't have '" _name "' property\n"); \
        return CERR_PARSE_FAILED; \
    } \
    if ((_node)->tag != (_tag)) { \
        warn("GLTF has '" _name "' property that's not " # _tag "\n"); \
        return CERR_PARSE_FAILED; \
    }

    GLTF_CHECK_PROP(scenes, "scenes",       JSON_ARRAY);
    GLTF_CHECK_PROP(scene,  "scene",        JSON_NUMBER);
    GLTF_CHECK_PROP(nodes,  "nodes",        JSON_ARRAY);
    GLTF_CHECK_PROP(mats,   "materials",    JSON_ARRAY);
    GLTF_CHECK_PROP(meshes, "meshes",       JSON_ARRAY);
    GLTF_CHECK_PROP(texs,   "textures",     JSON_ARRAY);
    GLTF_CHECK_PROP(imgs,   "images",       JSON_ARRAY);
    GLTF_CHECK_PROP(accrs,  "accessors",    JSON_ARRAY);
    GLTF_CHECK_PROP(bufvws, "bufferViews",  JSON_ARRAY);
    GLTF_CHECK_PROP(bufs,   "buffers",      JSON_ARRAY);
#undef GLTF_CHECK_PROP

    if (anis && anis->tag != JSON_ARRAY) {
        warn("GLTF has animations node that's not an array\n")
        return CERR_PARSE_FAILED;
    }

    /* Nodes */
    for (n = nodes->children.head, nid = 0; n; n = n->next, nid++) {
        JsonNode *jname, *jmesh, *jskin, *jchildren, *jrot, *jtrans, *jscale;
        struct gltf_node *node;

        if (n->tag != JSON_OBJECT)
            continue;

        jname = json_find_member(n, "name");
        jmesh = json_find_member(n, "mesh");
        jskin = json_find_member(n, "skin");
        jchildren = json_find_member(n, "children");
        jrot = json_find_member(n, "rotation");
        jtrans = json_find_member(n, "translation");
        jscale = json_find_member(n, "scale");
        if (!jname || jname->tag != JSON_STRING) /* actually, there only name is guaranteed */
            continue;

        CHECK(node = darray_add(gd->nodes));
        node->name = strdup(jname->string_);
        node->id = nid;
        if (jmesh && jmesh->tag == JSON_NUMBER)
            node->mesh = jmesh->number_;
        if (jskin && jskin->tag == JSON_NUMBER)
            node->skin = jskin->number_;
        if (jrot && jrot->tag == JSON_ARRAY)
            CHECK0(json_float_array(jrot, node->rotation, array_size(node->rotation)));
        if (jtrans && jtrans->tag == JSON_ARRAY)
            CHECK0(json_float_array(jtrans, node->translation, array_size(node->translation)));
        if (jscale && jscale->tag == JSON_ARRAY)
            CHECK0(json_float_array(jscale, node->scale, array_size(node->scale)));
        if (jchildren && jchildren->tag == JSON_ARRAY) 
            CHECK(node->ch_arr = json_int_array_alloc(jchildren, &node->nr_children));
    }
    /* unpack node.children arrays */

    /* Scenes */
    for (n = scenes->children.head; n; n = n->next) {
        JsonNode *jname, *jnodes;
        unsigned int nr_nodes;
        int *nodes, i;

        if (n->tag != JSON_OBJECT)
            continue;

        jname = json_find_member(n, "name");
        jnodes = json_find_member(n, "nodes");

        if (!jname || jname->tag != JSON_STRING)
            continue;
        if (!jnodes || jnodes->tag != JSON_ARRAY)
            continue;

        nodes = json_int_array_alloc(jnodes, &nr_nodes);
        if (!nodes || !nr_nodes)
            continue;

        for (i = 0; i < nr_nodes; i++) {
            struct gltf_node *node = DA(gd->nodes, nodes[i]);

            if (!node)
                continue;
            if (!strcmp(node->name, "Light") || !strcmp(node->name, "Camera"))
                continue;
            gd->root_node = nodes[i];
            dbg("root node: '%s'\n", node->name);
            break;
        }
        free(nodes);
    }

    // nodes_print(gd, &gd->nodes.x[gd->root_node], 0);

    /* Buffers */
    for (n = bufs->children.head; n; n = n->next) {
        JsonNode *jlen, *juri;
        size_t   len, slen;
        ssize_t  dlen;
        void **buf;

        if (n->tag != JSON_OBJECT)
            continue;

        jlen = json_find_member(n, "byteLength");
        juri = json_find_member(n, "uri");
        if (!jlen)
            continue;
        /* GLB bin buffer can't have uri, otherwise it must */
        if (!darray_count(gd->buffers) && gd->bin && juri)
            continue;
        else if ((darray_count(gd->buffers) || !gd->bin) && !juri)
            continue;

        len = jlen->number_;
        if (juri) {
            if (juri->tag != JSON_STRING ||
                strlen(juri->string_) < sizeof(DATA_URI) - 1 ||
                strncmp(juri->string_, DATA_URI, sizeof(DATA_URI) - 1))
                continue;

            slen = strlen(juri->string_) - sizeof(DATA_URI) + 1;
            len = max(len, base64_decoded_length(slen));
        }

        CHECK(buf = darray_add(gd->buffers));
        if (juri) {
            *buf = mem_alloc(len, .fatal_fail = 1);
            dlen = base64_decode(*buf, len, juri->string_ + sizeof(DATA_URI) - 1, slen);
            if (dlen < 0) {
                err("error decoding base64 buffer %zu\n", darray_count(gd->buffers) - 1);
                mem_free(*buf);
                /*
                * leave a NULL hole in the buffers array to keep the GLTF buffer
                * indices
                */
                *buf = NULL;
            }
        } else if (gd->bin) {
            CHECK(*buf = memdup(gd->bin, len));
        } else {
            err("no uri and not a GLB bin buffer\n");
        }
    }

    /* BufferViews */
    for (n = bufvws->children.head; n; n = n->next) {
        JsonNode *jbuf, *jlen, *joff;
        struct gltf_bufview *bv;

        jbuf = json_find_member(n, "buffer");
        jlen = json_find_member(n, "byteLength");
        joff = json_find_member(n, "byteOffset");
        if (!jbuf || !jlen || !joff)
            continue;

        if (jbuf->number_ >= gd->buffers.da.nr_el)
            continue;

        CHECK(bv = darray_add(gd->bufvws));
        bv->buffer = jbuf->number_;
        bv->offset = joff->number_;
        bv->length = jlen->number_;
        // dbg("buffer view %d: buf %d offset %zu size %zu\n", gd->nr_bufvws, bv->buffer,
        //     bv->offset, bv->length);
    }

    /* Accessors */
    for (n = accrs->children.head; n; n = n->next) {
        JsonNode *jbufvw, *jcount, *jtype, *jcomptype, *joffset;
        struct gltf_accessor *ga;
        int i;
        
        jbufvw = json_find_member(n, "bufferView");
        joffset = json_find_member(n, "byteOffset");
        jcount = json_find_member(n, "count");
        jtype = json_find_member(n, "type");
        jcomptype = json_find_member(n, "componentType");
        if (!jbufvw || !jcount || !jtype || !jcomptype)
            continue;
        
        if (jbufvw->number_ >= gd->bufvws.da.nr_el)
            continue;
        
        for (i = 0; i < array_size(types); i++)
            if (!strcmp(types[i], jtype->string_))
                break;
        
        if (i == array_size(types))
            continue;

        CHECK(ga = darray_add(gd->accrs));
        ga->bufview = jbufvw->number_;
        ga->comptype = jcomptype->number_;
        ga->count = jcount->number_;
        ga->type = i;
        if (joffset && joffset->tag == JSON_NUMBER)
            ga->offset = joffset->number_;

        // dbg("accessor %d: bufferView: %d count: %d componentType: %d type: %s\n", gd->nr_accrs,
        //     ga->bufview, ga->count, ga->comptype,
        //     types[i]);
    }

    gltf_load_animations(gd, anis);
    gltf_load_skins(gd, skins);

    /* Images */
    for (n = imgs->children.head; n; n = n->next) {
        JsonNode *jbufvw, *jmime, *jname;
        bool supported = true;

        jbufvw = json_find_member(n, "bufferView");
        jmime = json_find_member(n, "mimeType");
        jname = json_find_member(n, "name");
        if (!jbufvw || !jmime || !jname)
            continue;
        
        if (strcmp(jmime->string_, "image/png")) {
            warn("image '%s' as it's '%s' and not image/png\n", jname->string_, jmime->string_);
            supported = false;
        }

        if (jbufvw->number_ >= gd->bufvws.da.nr_el)
            continue;

        int *img = darray_add(gd->imgs);
        *img = supported ? jbufvw->number_ : -1;
        dbg("image %zu: bufferView: %d\n", darray_count(gd->imgs), *img);
    }

    /* Textures */
    for (n = texs->children.head; n; n = n->next) {
        JsonNode *jsrc; // mipmapping: *jsampler

        //jsampler = json_find_member(n, "sampler");
        jsrc = json_find_member(n, "source");
        if (!jsrc)
            continue;
        if (jsrc->number_ >= darray_count(gd->imgs))
            continue;

        CHECK(gd->texs = realloc(gd->texs, (gd->nr_texs + 1) * sizeof(unsigned int)));
        gd->texs[gd->nr_texs] = jsrc->number_;
        // dbg("texture %d: source: %d\n", gd->nr_texs, gd->texs[gd->nr_texs]);
        gd->nr_texs++;
    }

    /* Materials */
    for (n = mats->children.head; n; n = n->next) {
        struct gltf_material *mat;
        JsonNode *jwut, *jpbr, *jem;

        jpbr = json_find_member(n, "pbrMetallicRoughness");
        if (!jpbr || jpbr->tag != JSON_OBJECT)
            continue;

        jwut = json_find_member(jpbr, "baseColorTexture");
        if (!jwut || jwut->tag != JSON_OBJECT)
            continue;

        if (!jwut || jwut->tag != JSON_OBJECT)
            continue;
        jwut = json_find_member(jwut, "index");
        if (jwut->tag != JSON_NUMBER || jwut->number_ >= gd->nr_texs)
            continue;

        CHECK(mat = darray_add(gd->mats));
        mat->base_tex = jwut->number_;
        mat->normal_tex = -1;
        mat->emission_tex = -1;

        jem = json_find_member(n, "emissiveTexture");
        if (jem && jem->tag == JSON_OBJECT) {
            JsonNode *jemidx = json_find_member(jem, "index");
            if (jemidx && jemidx->tag == JSON_NUMBER)
                mat->emission_tex = jemidx->number_;
        }

        if (jpbr) {
            jwut = json_find_member(jpbr, "metallicFactor");
            if (jwut && jwut->tag == JSON_NUMBER)
                mat->metallic = jwut->number_;
        
            jwut = json_find_member(jpbr, "roughnessFactor");
            if (jwut && jwut->tag == JSON_NUMBER)
                mat->roughness = jwut->number_;
        
            jwut = json_find_member(n, "normalTexture");
            if (jwut && jwut->tag == JSON_OBJECT) {
                jwut = json_find_member(jwut, "index");
                if (jwut->tag == JSON_NUMBER && jwut->number_ < gd->nr_texs)
                    mat->normal_tex = jwut->number_;
            }
        }

        dbg("material %zu: tex: %d nmap: %d emission: %d met: %f rough: %f\n",
            darray_count(gd->mats) - 1, mat->base_tex,
            mat->normal_tex,
            mat->emission_tex,
            mat->metallic,
            mat->roughness
        );
    }

    for (n = meshes->children.head; n; n = n->next) {
        JsonNode *jname, *jprim, *jattr, *jindices, *jmat, *p;
        struct gltf_mesh *mesh;

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

        CHECK(mesh = darray_add(gd->meshes));
        gltf_mesh_init(mesh, jname->string_,
                       jindices->number_, jmat->number_);
        for (p = jattr->children.head; p; p = p->next) {
            if (!strcmp(p->key, "POSITION") && p->tag == JSON_NUMBER)
                mesh->POSITION = p->number_;
            else if (!strcmp(p->key, "NORMAL") && p->tag == JSON_NUMBER)
                mesh->NORMAL = p->number_;
            else if (!strcmp(p->key, "TANGENT") && p->tag == JSON_NUMBER)
                mesh->TANGENT = p->number_;
            else if (!strcmp(p->key, "TEXCOORD_0") && p->tag == JSON_NUMBER)
                mesh->TEXCOORD_0 = p->number_;
            else if (!strcmp(p->key, "COLOR_0") && p->tag == JSON_NUMBER)
                mesh->COLOR_0 = p->number_;
            else if (!strcmp(p->key, "JOINTS_0") && p->tag == JSON_NUMBER)
                mesh->JOINTS_0 = p->number_;
            else if (!strcmp(p->key, "WEIGHTS_0") && p->tag == JSON_NUMBER)
                mesh->WEIGHTS_0 = p->number_;
        }
        // dbg("mesh %d: '%s' POSITION: %d\n", gd->nr_meshes, jname->string_, mesh->POSITION);
    }

    return CERR_OK;
}

/* https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#binary-gltf-layout */
struct glb_header {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    length;
};

struct glb_chunk {
    uint32_t    length;
    uint32_t    type;
    uint8_t     data[0];
};

#define GLB_MAGIC       0x46546C67
#define GLB_TYPE_JSON   0x4E4F534A
#define GLB_TYPE_BIN    0x004E4942

static cerr gltf_bin_parse(void *buf, size_t size, struct gltf_data *gd)
{
    struct glb_header *hdr;

    if (size < sizeof(*hdr))
        return CERR_PARSE_FAILED;

    hdr = buf;
    if (hdr->magic != GLB_MAGIC ||
        hdr->version < 2        ||
        hdr->length != size) {
        return CERR_PARSE_FAILED;
    }

    struct glb_chunk *json_chunk = buf + sizeof(*hdr);
    if (json_chunk->type != GLB_TYPE_JSON)
        return CERR_PARSE_FAILED;

    struct glb_chunk *bin_chunk = buf + sizeof(*hdr) +
                                  offsetof(struct glb_chunk, data[json_chunk->length]);
    if (bin_chunk->type != GLB_TYPE_BIN)
        return CERR_PARSE_FAILED;
    if (json_chunk->length + bin_chunk->length + sizeof(*hdr) + sizeof(struct glb_chunk) * 2 != size)
        return CERR_PARSE_FAILED;

    gd->bin = bin_chunk->data;
    char *json_buf = strndup((const char *)json_chunk->data, json_chunk->length);
    cerr err = gltf_json_parse(json_buf, gd);
    mem_free(json_buf);

    return err;
}

static void gltf_onload(struct lib_handle *h, void *data)
{
    dbg("loading '%s'\n", h->name);

    if (h->state == RES_ERROR) {
        warn("couldn't load '%s'\n", h->name);
        goto out;
    }

    /* try GLB first, if it's not GLB, it fails quickly */
    cerr err = gltf_bin_parse(h->buf, h->size, data);
    if (!IS_CERR(err))
        goto out;

    /* if GLTF embedded fails, nothing more to do */
    err = gltf_json_parse(h->buf, data);
    if (IS_CERR(err)) {
        warn("couldn't parse '%s'\n", h->name);
        h->state = RES_ERROR;
    }

out:
    ref_put(h);
}

void gltf_mesh_data(struct gltf_data *gd, int mesh, float **vx, size_t *vxsz, unsigned short **idx, size_t *idxsz,
                    float **tx, size_t *txsz, float **norm, size_t *normsz)
{
    if (mesh >= gd->meshes.da.nr_el)
        return;

    if (vx) {
        *vx = memdup(gltf_vx(gd, mesh), gltf_vxsz(gd, mesh));
        *vxsz = gltf_vxsz(gd, mesh);
    }
    if (idx) {
        *idx = memdup(gltf_idx(gd, mesh), gltf_idxsz(gd, mesh));
        *idxsz = gltf_idxsz(gd, mesh);
    }
    if (tx) {
        *tx = memdup(gltf_tx(gd, mesh), gltf_txsz(gd, mesh));
        *txsz = gltf_txsz(gd, mesh);
    }
    if (norm) {
        *norm = memdup(gltf_norm(gd, mesh), gltf_normsz(gd, mesh));
        *normsz = gltf_normsz(gd, mesh);
    }
}

int gltf_skin_node_to_joint(struct gltf_data *gd, int skin, int node)
{
    if (node >= DA(gd->skins, skin)->nr_joints)
        return -1;

    return DA(gd->skins, skin)->nodes[node];
}

cerr gltf_instantiate_one(struct gltf_data *gd, int mesh)
{
    model3dtx *txm;
    int skin;

    if (mesh < 0 || mesh >= gd->meshes.da.nr_el)
        return CERR_INVALID_ARGUMENTS;

    LOCAL_SET(mesh_t, me) = mesh_new(gltf_mesh_name(gd, mesh));
    mesh_attr_dup(me, MESH_VX, gltf_vx(gd, mesh), gltf_vx_stride(gd, mesh), gltf_nr_vx(gd, mesh));
    mesh_attr_dup(me, MESH_TX, gltf_tx(gd, mesh), gltf_tx_stride(gd, mesh), gltf_nr_tx(gd, mesh));
    mesh_attr_dup(me, MESH_IDX, gltf_idx(gd, mesh), gltf_idx_stride(gd, mesh), gltf_nr_idx(gd, mesh));
    if (gltf_has_norm(gd, mesh))
        mesh_attr_dup(me, MESH_NORM, gltf_norm(gd, mesh), gltf_norm_stride(gd, mesh), gltf_nr_norm(gd, mesh));
    if (gltf_has_joints(gd, mesh))
        mesh_attr_dup(me, MESH_JOINTS, gltf_joints(gd, mesh), gltf_joints_stride(gd, mesh), gltf_nr_joints(gd, mesh));
    if (gltf_has_weights(gd, mesh))
        mesh_attr_dup(me, MESH_WEIGHTS, gltf_weights(gd, mesh), gltf_weights_stride(gd, mesh), gltf_nr_weights(gd, mesh));
    if (gltf_has_tangent(gd, mesh))
        mesh_attr_dup(me, MESH_TANGENTS, gltf_tangent(gd, mesh), gltf_tangent_stride(gd, mesh), gltf_nr_tangent(gd, mesh));
    mesh_optimize(me);

    struct shader_prog *prog = shader_prog_find(&gd->scene->shaders, "model");

    cresp(model3d) res = ref_new2(model3d,
                                  .prog   = ref_pass(prog),
                                  .name   = gltf_mesh_name(gd, mesh),
                                  .mesh   = me);
    if (IS_CERR(res))
        return cerr_error_cres(res);

    if (gltf_has_tangent(gd, mesh)) {
        dbg("added tangents for mesh '%s'\n", gltf_mesh_name(gd, mesh));
    }

    txm = model3dtx_new_from_png_buffers(ref_pass(res.val), gltf_tex(gd, mesh), gltf_texsz(gd, mesh),
                                         gltf_nmap(gd, mesh), gltf_nmapsz(gd, mesh),
                                         gltf_em(gd, mesh), gltf_emsz(gd, mesh));

    if (!txm) {
        warn("failed to load texture(s) for mesh '%s'\n", gltf_mesh_name(gd, mesh));
        return CERR_TEXTURE_NOT_LOADED; /* XXX: until model3dtx gets a constructor */
    }

    skin = gltf_mesh_skin(gd, mesh);
    if (skin >= 0) {
        struct gltf_skin *s = DA(gd->skins, skin);
        struct gltf_animation *ga;
        struct gltf_node *node;
        struct animation *an;
        mat4x4 root_pose;
        int err, i;

        err = model3d_add_skinning(txm->model,
                                   mesh_joints(me), mesh_joints_sz(me),
                                   mesh_weights(me), mesh_weights_sz(me), s->nr_joints, s->invmxs);
        if (err)
            goto no_skinning;

        mat4x4_identity(root_pose);
        darray_for_each(node, gd->nodes) {
            if (!strcmp(node->name, s->name)) {
                if (vec4_len(node->rotation)) {
                    mat4x4_from_quat(root_pose, node->rotation);
                    root_pose[3][0] = node->translation[0];
                    root_pose[3][1] = node->translation[1];
                    root_pose[3][2] = node->translation[2];
                    root_pose[3][3] = 1;
                }
                break;
            }
        }

        /* XXX: -> model.c */
        memcpy(txm->model->root_pose, root_pose, sizeof(mat4x4));

        for (i = 0; i < s->nr_joints; i++) {
            int ch;

            node = DA(gd->nodes, s->joints[i]);
            txm->model->joints[i].name = strdup(node->name);

            for (ch = 0; ch < node->nr_children; ch++) {
                int *pchild = darray_add(txm->model->joints[i].children);
                *pchild = gltf_skin_node_to_joint(gd, skin, node->ch_arr[ch]);//node->ch_arr[ch];
            }
        }

        darray_for_each(ga, gd->anis) {
            struct gltf_channel *chan;

            /*
             * There are no keyframes as such, that span all properties of all
             * joints. Instead each transformation channel has a timeline, some
             * share timelines and some don't.
             * Interpolation is done for each channel separately, based on the
             * timeline that it uses, in the renderer.
             * If we were to stuff all this into poses, assuming that all timelines
             * are reducible to one linear timeline, we'd have to interpolate here
             * for the channels that are sparser.
             * OTOH, the 'pose' / 'frame' based data will fit into channel based
             * model trivially, where all channels move at the same time increments.
             */
            CHECK(an = animation_new(txm->model, ga->name,
                                     ga->channels.da.nr_el));
            darray_for_each(chan, ga->channels) {
                int accr_in = DA(ga->samplers, chan->sampler)->input;
                int accr_out = DA(ga->samplers, chan->sampler)->output;
                size_t frames = gltf_accessor_nr(gd, accr_in);
                float *time = gltf_accessor_buf(gd, accr_in);
                float *data = gltf_accessor_buf(gd, accr_out);

                size_t data_stride = gltf_accessor_stride(gd, accr_out);

                int joint = gltf_skin_node_to_joint(gd, skin, chan->node);
                if (joint < 0) {
                    warn("animation '%s' channel %d references a non-existent joint %d in skin '%s'\n",
                         an->name, an->cur_channel, chan->node, DA(gd->skins, skin)->name);
                    continue;
                }

                animation_add_channel(an, frames, time, data, data_stride, joint, chan->path);
            }

            /*
             * An animation with no channels has no reason to exist. This may
             * be a result of bezier curves getting accidentally exported to
             * the gltf, their animations don't touch the skeleton's joints,
             * so it's safe to skip them. See the warn() above.
             */
            if (!an->cur_channel)
                animation_delete(an);
        }
    }
no_skinning:
    txm->metallic = clampf(gltf_material(gd, mesh)->metallic, 0.1, 1.0);
    txm->roughness = clampf(gltf_material(gd, mesh)->roughness, 0.2, 1.0);

    scene_add_model(gd->scene, txm);

    return CERR_OK;
}

void gltf_instantiate_all(struct gltf_data *gd)
{
    int i;

    for (i = 0; i < gd->meshes.da.nr_el; i++)
        gltf_instantiate_one(gd, i);
}

struct gltf_data *gltf_load(struct scene *scene, const char *name)
{
    struct gltf_data  *gd;
    struct lib_handle *lh;
    enum res_state state;

    gd = mem_alloc(sizeof(*gd), .zero = 1, .fatal_fail = 1);
    gd->scene = scene;
    lh = lib_request(RES_ASSET, name, gltf_onload, gd);
    state = lh->state;
    ref_put(lh);

    if (state == RES_ERROR) {
        gltf_free(gd);
        return NULL;
    }
    return gd;
}
