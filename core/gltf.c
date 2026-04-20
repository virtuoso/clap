// SPDX-License-Identifier: Apache-2.0
#include <inttypes.h>
#include "base64.h"
#include "datatypes.h"
#include "draw.h"
#include "json.h"
#include "librarian.h"
#include "model.h"
#include "object.h"
#include "shader.h"
#include "gltf.h"

#define DATA_URI "data:application/octet-stream;base64,"

struct gltf_bufview {
    unsigned int buffer;
    size_t       offset;
    size_t       length;
};

static size_t gltf_type_size(unsigned int gltf_type)
{
    /* these correspond to GL_* macros */
    switch (gltf_type) {
        case 0x1400: /* byte */
        case 0x1401: /* unsigned byte */
            return 1;
        case 0x1402: /* short */
        case 0x1403: /* unsigned short */
            return 2;
        case 0x1404: /* int */
        case 0x1405: /* unsigned int */
        case 0x1406: /* float */
            return 4;
        case 0x1407: /* 2 bytes */
            return 2;
        case 0x1408: /* 3 bytes */
            return 3;
        case 0x1409: /* 4 bytes */
            return 4;
        case 0x140a: /* double */
            return 8;
        default:
            break;
    }

    clap_unreachable();

    return 0;
}

struct gltf_accessor {
    unsigned int bufview;
    unsigned int comptype;
    unsigned int count;
    data_type    type;
    size_t       offset;
};

enum gltf_extra_type {
    GLTF_EXTRA_STRING,
    GLTF_EXTRA_NUMBER,
    GLTF_EXTRA_BOOL,
};

struct gltf_extra {
    char                *key;
    enum gltf_extra_type type;
    union {
        char   *s;
        double  n;
        bool    b;
    };
};

struct gltf_node {
    const char  *name;
    quat        rotation;
    vec3        scale;
    vec3        translation;
    struct list children;
    struct list entry;
    darray(struct gltf_extra, extras);
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
    /*
     * A GLTF mesh can contain multiple primitives (different materials on
     * the same logical object). We flatten the JSON meshes[].primitives[]
     * array into gd->meshes, with siblings stored consecutively. `group`
     * is the index into gd->mesh_groups (i.e. the original JSON mesh
     * index); `group_first` and `group_count` describe the contiguous run
     * of siblings within gd->meshes.
     */
    int         group;
    int         group_first;
    int         group_count;
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
    canvas *base_canvas;
    canvas *emit_canvas;
    /*
     * A GLTF material with neither baseColorTexture nor baseColorFactor
     * can't be rendered meaningfully but still occupies a slot in
     * gd->mats so primitive material indices stay aligned. At
     * instantiation we plug black_pixel() as a stand-in diffuse and the
     * scene marks the primitive's entity invisible.
     */
    bool   fallback;
};

struct gltf_data {
    struct mq                     *mq;
    pipeline                      *pl;
    darray(void *,                buffers);
    darray(struct gltf_bufview,   bufvws);
    darray(struct gltf_accessor,  accrs);
    darray(struct gltf_mesh,      meshes);
    /* maps GLTF mesh (group) index -> flat index of group's first primitive */
    darray(int,                   mesh_groups);
    darray(struct gltf_material,  mats);
    darray(struct gltf_node,      nodes);
    darray(struct gltf_animation, anis);
    darray(struct gltf_skin,      skins);
    // struct darray        texs;
    darray(int,                   imgs);
    unsigned int         *texs;
    void                 *bin;
    int                  root_node;
    unsigned int         warned;
    unsigned int nr_texs;
    unsigned int texid;
    bool         fix_origin;
    bool         textures_png;
};

void gltf_free(struct gltf_data *gd)
{
    struct gltf_skin *skin;
    struct gltf_node *node;
    int i;

    ref_put(gd->pl);
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

    struct gltf_material *mat;
    darray_for_each(mat, gd->mats) {
        if (mat->base_canvas)   canvas_free(mat->base_canvas);
        if (mat->emit_canvas)   canvas_free(mat->emit_canvas);
    }

    darray_for_each(node, gd->nodes) {
        struct gltf_extra *ex;
        free((void *)node->name);
        free(node->ch_arr);
        darray_for_each(ex, node->extras) {
            free(ex->key);
            if (ex->type == GLTF_EXTRA_STRING)
                free(ex->s);
        }
        darray_clearout(node->extras);
    }
    darray_for_each(skin, gd->skins) {
        free((void *)skin->joints);
        free(skin->nodes);
        free((void *)skin->name);
    }
    darray_clearout(gd->buffers);
    darray_clearout(gd->bufvws);
    darray_clearout(gd->meshes);
    darray_clearout(gd->mesh_groups);
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

    return data_comp_count(ga->type) * gltf_type_size(ga->comptype);
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
    elsz = gltf_type_size(ga->comptype);

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

int gltf_get_mesh_groups(struct gltf_data *gd)
{
    return darray_count(gd->mesh_groups);
}

int gltf_group_first(struct gltf_data *gd, int group)
{
    if (group < 0 || group >= darray_count(gd->mesh_groups))
        return -1;

    return *DA(gd->mesh_groups, group);
}

int gltf_group_count(struct gltf_data *gd, int group)
{
    int first = gltf_group_first(gd, group);
    if (first < 0)
        return 0;

    return DA(gd->meshes, first)->group_count;
}

int gltf_mesh_group(struct gltf_data *gd, int mesh)
{
    if (mesh < 0 || mesh >= darray_count(gd->meshes))
        return -1;

    return DA(gd->meshes, mesh)->group;
}

bool gltf_mesh_fallback(struct gltf_data *gd, int mesh)
{
    struct gltf_material *mat = gltf_material(gd, mesh);
    return mat ? mat->fallback : false;
}

int gltf_get_nodes(struct gltf_data *gd)
{
    return darray_count(gd->nodes);
}

const char *gltf_node_name(struct gltf_data *gd, int node)
{
    if (node < 0 || node >= darray_count(gd->nodes))
        return NULL;

    return DA(gd->nodes, node)->name;
}

int gltf_node_mesh(struct gltf_data *gd, int node)
{
    if (node < 0 || node >= darray_count(gd->nodes))
        return -1;

    return DA(gd->nodes, node)->mesh;
}

void gltf_node_translation(struct gltf_data *gd, int node, vec3 out)
{
    if (node < 0 || node >= darray_count(gd->nodes)) {
        vec3_setup(out, 0.0f, 0.0f, 0.0f);
        return;
    }

    struct gltf_node *n = DA(gd->nodes, node);
    vec3_dup(out, n->translation);
}

void gltf_node_rotation(struct gltf_data *gd, int node, quat out)
{
    if (node < 0 || node >= darray_count(gd->nodes)) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
        return;
    }

    struct gltf_node *n = DA(gd->nodes, node);
    /* GLTF default: identity (0,0,0,1); node zero-init is (0,0,0,0) */
    if (vec4_len(n->rotation) == 0.0f) {
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; out[3] = 1.0f;
    } else {
        out[0] = n->rotation[0];
        out[1] = n->rotation[1];
        out[2] = n->rotation[2];
        out[3] = n->rotation[3];
    }
}

void gltf_node_scale(struct gltf_data *gd, int node, vec3 out)
{
    if (node < 0 || node >= darray_count(gd->nodes)) {
        vec3_setup(out, 1.0f, 1.0f, 1.0f);
        return;
    }

    struct gltf_node *n = DA(gd->nodes, node);
    vec3_dup(out, n->scale);
}

static struct gltf_extra *gltf_node_extra_find(struct gltf_data *gd, int node, const char *key)
{
    if (node < 0 || node >= darray_count(gd->nodes))
        return NULL;

    struct gltf_node *n = DA(gd->nodes, node);
    struct gltf_extra *ex;
    darray_for_each(ex, n->extras)
        if (!strcmp(ex->key, key))
            return ex;

    return NULL;
}

bool gltf_node_extras_string(struct gltf_data *gd, int node, const char *key, const char **out)
{
    struct gltf_extra *ex = gltf_node_extra_find(gd, node, key);
    if (!ex || ex->type != GLTF_EXTRA_STRING)
        return false;

    *out = ex->s;
    return true;
}

bool gltf_node_extras_number(struct gltf_data *gd, int node, const char *key, double *out)
{
    struct gltf_extra *ex = gltf_node_extra_find(gd, node, key);
    if (!ex || ex->type != GLTF_EXTRA_NUMBER)
        return false;

    *out = ex->n;
    return true;
}

bool gltf_node_extras_bool(struct gltf_data *gd, int node, const char *key, bool *out)
{
    struct gltf_extra *ex = gltf_node_extra_find(gd, node, key);
    if (!ex || ex->type != GLTF_EXTRA_BOOL)
        return false;

    *out = ex->b;
    return true;
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

/* XXX: return cerr */
static void gltf_load_images(struct gltf_data *gd, JsonNode *imgs)
{
    for (JsonNode *n = imgs->children.head; n; n = n->next) {
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
}

/* XXX: return cerr */
static void gltf_load_textures(struct gltf_data *gd, JsonNode *texs)
{
    for (JsonNode *n = texs->children.head; n; n = n->next) {
        JsonNode *jsrc; // mipmapping: *jsampler

        //jsampler = json_find_member(n, "sampler");
        jsrc = json_find_member(n, "source");
        if (!jsrc)
            continue;
        if (jsrc->number_ >= darray_count(gd->imgs))
            continue;

        gd->texs = mem_realloc_array(gd->texs, (gd->nr_texs + 1), sizeof(unsigned int), .fatal_fail = 1);
        gd->texs[gd->nr_texs] = jsrc->number_;
        gd->nr_texs++;
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
    darray_init(gd->mesh_groups);
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
        JsonNode *jname, *jmesh, *jskin, *jchildren, *jrot, *jtrans, *jscale, *jextras;
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
        jextras = json_find_member(n, "extras");
        if (!jname || jname->tag != JSON_STRING) /* actually, there only name is guaranteed */
            continue;

        CHECK(node = darray_add(gd->nodes));
        darray_init(node->extras);
        node->name = strdup(jname->string_);
        node->id = nid;
        node->mesh = -1;
        node->skin = -1;
        node->scale[0] = 1.0f;
        node->scale[1] = 1.0f;
        node->scale[2] = 1.0f;
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
        if (jextras && jextras->tag == JSON_OBJECT) {
            for (JsonNode *je = jextras->children.head; je; je = je->next) {
                struct gltf_extra *ex;
                if (!je->key)
                    continue;
                if (je->tag != JSON_STRING && je->tag != JSON_NUMBER && je->tag != JSON_BOOL)
                    continue;
                CHECK(ex = darray_add(node->extras));
                ex->key = strdup(je->key);
                if (je->tag == JSON_STRING) {
                    ex->type = GLTF_EXTRA_STRING;
                    ex->s = strdup(je->string_);
                } else if (je->tag == JSON_NUMBER) {
                    ex->type = GLTF_EXTRA_NUMBER;
                    ex->n = je->number_;
                } else {
                    ex->type = GLTF_EXTRA_BOOL;
                    ex->b = je->bool_;
                }
            }
        }
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
        if (!darray_count(gd->buffers) && gd->bin && juri)      continue;
        if ((darray_count(gd->buffers) || !gd->bin) && !juri)   continue;

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
        
        jbufvw = json_find_member(n, "bufferView");
        joffset = json_find_member(n, "byteOffset");
        jcount = json_find_member(n, "count");
        jtype = json_find_member(n, "type");
        jcomptype = json_find_member(n, "componentType");
        if (!jbufvw || !jcount || !jtype || !jcomptype)
            continue;
        
        if (jbufvw->number_ >= gd->bufvws.da.nr_el)
            continue;
        
        data_type type = data_type_by_name(jtype->string_);
        if (type == DT_NONE)
            continue;

        CHECK(ga = darray_add(gd->accrs));
        ga->bufview = jbufvw->number_;
        ga->comptype = jcomptype->number_;
        ga->count = jcount->number_;
        ga->type = type;
        if (joffset && joffset->tag == JSON_NUMBER)
            ga->offset = joffset->number_;

        // dbg("accessor %d: bufferView: %d count: %d componentType: %d type: %s\n", gd->nr_accrs,
        //     ga->bufview, ga->count, ga->comptype,
        //     types[i]);
    }

    gltf_load_animations(gd, anis);
    gltf_load_skins(gd, skins);

    /* Images */
    if (imgs)   gltf_load_images(gd, imgs);

    /* Textures */
    if (texs)   gltf_load_textures(gd, texs);

    /* Materials */
    for (n = mats->children.head; n; n = n->next) {
        struct gltf_material *mat;
        JsonNode *jwut, *jpbr, *jem;
        LOCAL(canvas, base_canvas);

        /*
         * Reserve a slot up front so gd->mats stays 1:1 with the JSON
         * materials[] array. Any material with neither a baseColorTexture
         * nor a baseColorFactor falls back to the GLTF default (opaque
         * white) — the slot is still valid, so primitives whose material
         * index refers to it don't overshoot gd->mats.
         */
        CHECK(mat = darray_add(gd->mats));
        mat->base_tex = -1;
        mat->normal_tex = -1;
        mat->emission_tex = -1;

        jpbr = json_find_member(n, "pbrMetallicRoughness");
        jwut = jpbr && jpbr->tag == JSON_OBJECT ?
               json_find_member(jpbr, "baseColorTexture") : NULL;

        float base_color[4];
        bool have_color = false;
        bool have_tex = false;

        if (jwut && jwut->tag == JSON_OBJECT) {
            JsonNode *jidx = json_find_member(jwut, "index");
            if (jidx && jidx->tag == JSON_NUMBER && jidx->number_ < gd->nr_texs) {
                mat->base_tex = jidx->number_;
                /* XXX: model3dtx::buffers_png applies to all textures: break it up */
                gd->textures_png = true;
                have_tex = true;
            }
        } else if (jpbr && jpbr->tag == JSON_OBJECT) {
            JsonNode *jcolor = json_find_member(jpbr, "baseColorFactor");
            if (jcolor && jcolor->tag == JSON_ARRAY &&
                !json_float_array(jcolor, base_color, array_size(base_color)))
                have_color = true;
        }

        if (have_color) {
            /* XXX: hardcoded color format */
            base_canvas = CRES_RET(canvas_new(TEX_FMT_RGBA8, 1, 1), {});
            if (base_canvas) {
                canvas_write(base_canvas, .color = base_color);
                mat->base_canvas = NOCU(base_canvas);
            }
        } else if (!have_tex) {
            mat->fallback = true;
        }

        jem = json_find_member(n, "emissiveTexture");
        if (jem && jem->tag == JSON_OBJECT) {
            JsonNode *jemidx = json_find_member(jem, "index");
            if (jemidx && jemidx->tag == JSON_NUMBER)
                mat->emission_tex = jemidx->number_;
        } else if (!jem) {
            JsonNode *jemcolor = json_find_member(n, "emissiveFactor");
            if (jemcolor && jemcolor->tag == JSON_ARRAY) {
                /* XXX: hardcoded color format */
                auto cres = canvas_new(TEX_FMT_RGBA8, 1, 1);
                if (!IS_CERR(cres)) {
                    float emit_color[4] = {};
                    json_float_array(jemcolor, emit_color, 3);

                    mat->emit_canvas = cres.val;
                    canvas_write(mat->emit_canvas, .color = emit_color);
                }
            }
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

    int group_idx = 0;
    for (n = meshes->children.head; n; n = n->next, group_idx++) {
        JsonNode *jname, *jprims, *jprim, *jattr, *jindices, *jmat, *p;
        struct gltf_mesh *mesh;

        /*
         * Reserve a group slot in step with the JSON meshes[] index so the
         * mapping survives skipped/invalid groups. The entry stays -1 if
         * nothing valid landed in this group.
         */
        int *group_slot;
        CHECK(group_slot = darray_add(gd->mesh_groups));
        *group_slot = -1;

        jname = json_find_member(n, "name"); /* like, "Cube", thanks blender */
        jprims = json_find_member(n, "primitives");
        if (!jname || !jprims)
            continue;
        if (jprims->tag != JSON_ARRAY)
            continue;

        int group_first = darray_count(gd->meshes);
        int group_count = 0;

        for (jprim = jprims->children.head; jprim; jprim = jprim->next) {
            jindices = json_find_member(jprim, "indices");
            jmat = json_find_member(jprim, "material");
            jattr = json_find_member(jprim, "attributes");
            if (!jattr || jattr->tag != JSON_OBJECT || !jindices || !jmat)
                continue;

            CHECK(mesh = darray_add(gd->meshes));
            gltf_mesh_init(mesh, jname->string_,
                           jindices->number_, jmat->number_);
            mesh->group = group_idx;
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
            group_count++;
        }

        if (!group_count)
            continue;

        /* backfill group_first / group_count on each primitive */
        for (int i = 0; i < group_count; i++) {
            struct gltf_mesh *gm = DA(gd->meshes, group_first + i);
            gm->group_first = group_first;
            gm->group_count = group_count;
        }
        *DA(gd->mesh_groups, group_idx) = group_first;
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
    CERR_RET(
        gltf_json_parse(h->buf, data),
        {
            /* this error has nowhere further to go, print it here */
            err_cerr(__cerr, "couldn't parse '%s'\n", h->name);
            h->state = RES_ERROR;
        }
    );

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
    int skin;

    if (mesh < 0 || mesh >= gd->meshes.da.nr_el)
        return CERR_INVALID_ARGUMENTS;

    LOCAL_SET(mesh_t, me) = ref_new(mesh, .name = gltf_mesh_name(gd, mesh), .fix_origin = gd->fix_origin);
    CERR_RET_CERR(mesh_attr_dup(me, MESH_VX, gltf_vx(gd, mesh), gltf_vx_stride(gd, mesh), gltf_nr_vx(gd, mesh)));
    CERR_RET_CERR(mesh_attr_dup(me, MESH_TX, gltf_tx(gd, mesh), gltf_tx_stride(gd, mesh), gltf_nr_tx(gd, mesh)));
    CERR_RET_CERR(mesh_attr_dup(me, MESH_IDX, gltf_idx(gd, mesh), gltf_idx_stride(gd, mesh), gltf_nr_idx(gd, mesh)));
    if (gltf_has_norm(gd, mesh))
        CERR_RET_CERR(
            mesh_attr_dup(me, MESH_NORM, gltf_norm(gd, mesh), gltf_norm_stride(gd, mesh), gltf_nr_norm(gd, mesh))
        );
    if (gltf_has_joints(gd, mesh))
        CERR_RET_CERR(
            mesh_attr_dup(me, MESH_JOINTS, gltf_joints(gd, mesh), gltf_joints_stride(gd, mesh), gltf_nr_joints(gd, mesh))
        );
    if (gltf_has_weights(gd, mesh))
        CERR_RET_CERR(
            mesh_attr_dup(me, MESH_WEIGHTS, gltf_weights(gd, mesh), gltf_weights_stride(gd, mesh), gltf_nr_weights(gd, mesh))
        );
    if (gltf_has_tangent(gd, mesh))
        CERR_RET_CERR(
            mesh_attr_dup(me, MESH_TANGENTS, gltf_tangent(gd, mesh), gltf_tangent_stride(gd, mesh), gltf_nr_tangent(gd, mesh))
        );
    mesh_optimize(me);

    struct shader_prog *prog = CRES_RET_CERR(pipeline_shader_find_get(gd->pl, "model"));
    /*
     * ref_put(prog) on failure is not necessary, model3d constructor
     * already drops this reference; plus ref_pass(prog) sets prog to NULL
     * effectively making this a requirement
     */
    model3d *m = CRES_RET_CERR(
        ref_new_checked(
            model3d,
            .prog   = ref_pass(prog),
            .name   = gltf_mesh_name(gd, mesh),
            .mesh   = me
        )
    );

    if (gltf_has_tangent(gd, mesh)) {
        dbg("added tangents for mesh '%s'\n", gltf_mesh_name(gd, mesh));
    }

    auto mat = gltf_material(gd, mesh);
    model3dtx *txm = CRES_RET(
        ref_new_checked(
            model3dtx,
            .model            = ref_pass(m),
            .tex              = mat->fallback ? black_pixel() : NULL,
            .buffers_png      = mat->fallback ? false : gd->textures_png, /* XXX: one switch for all texture buffers */
            .texture_buffer   = mat->fallback ? NULL : (mat->base_canvas ? canvas_data(mat->base_canvas) : gltf_tex(gd, mesh)),
            .texture_size     = mat->fallback ? 0    : (mat->base_canvas ? canvas_size(mat->base_canvas) : gltf_texsz(gd, mesh)), /* XXX: need texture*/
            .texture_width    = mat->base_canvas ? 1 : 0,
            .texture_height   = mat->base_canvas ? 1 : 0,
            .normal_buffer    = gltf_nmap(gd, mesh),
            .normal_size      = gltf_nmapsz(gd, mesh),
            .emission_buffer  = mat->emit_canvas ? canvas_data(mat->emit_canvas) : gltf_em(gd, mesh),
            .emission_size    = mat->emit_canvas ? canvas_size(mat->emit_canvas) : gltf_emsz(gd, mesh),
            .emission_width   = mat->base_canvas ? 1 : 0,
            .emission_height  = mat->base_canvas ? 1 : 0
        ),
        {
            /*
             * ref_put(m) on failure is not necessary, model3dtx constructor
             * already drops this reference; plus, ref_pass(m) sets m to NULL
             * effectively making this a requirement
             */
            warn("failed to load texture(s) for mesh '%s'\n", gltf_mesh_name(gd, mesh));
            return cerr_error_cres(__resp);
        }
    );

    skin = gltf_mesh_skin(gd, mesh);
    if (skin >= 0) {
        struct gltf_skin *s = DA(gd->skins, skin);
        struct gltf_animation *ga;
        struct gltf_node *node;
        struct animation *an;
        mat4x4 root_pose;
        int err, i;

        err = model3d_add_skinning(txm->model, s->nr_joints, s->invmxs);
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
            txm->model->joints[i].id   = node->id;

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
                    if (!gd->warned++)
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
    txm->mat.metallic = clampf(gltf_material(gd, mesh)->metallic, 0.0, 1.0);
    txm->mat.roughness = clampf(gltf_material(gd, mesh)->roughness, 0.0, 1.0);

    mq_add_model(gd->mq, txm);

    return CERR_OK;
}

void gltf_instantiate_all(struct gltf_data *gd)
{
    int i;

    for (i = 0; i < gd->meshes.da.nr_el; i++) {
        cerr err = gltf_instantiate_one(gd, i);
        if (IS_CERR(err))
            err_cerr(err, "couldn't instantiate mesh '%s'\n", gltf_mesh_name(gd, i));
    }
}

struct gltf_data *_gltf_load(const gltf_load_options *opts)
{
    if (!opts->name || !opts->mq || !opts->pipeline)
        return NULL;

    struct gltf_data  *gd;
    struct lib_handle *lh;
    enum res_state state;

    gd = mem_alloc(sizeof(*gd), .zero = 1, .fatal_fail = 1);
    gd->pl = ref_get((pipeline *)opts->pipeline);
    gd->mq = opts->mq;
    gd->fix_origin = opts->fix_origin;
    lh = lib_request(RES_ASSET, opts->name, gltf_onload, gd);
    state = lh->state;
    ref_put(lh);

    if (state == RES_ERROR) {
        gltf_free(gd);
        return NULL;
    }
    return gd;
}
