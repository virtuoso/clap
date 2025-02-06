// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "shader_constants.h"
#include "librarian.h"
#include "clap.h"
#include "common.h"
#include "interp.h"
#include "render.h"
#include "matrix.h"
#include "util.h"
#include "object.h"
#include "mesh.h"
#include "model.h"
#include "pngloader.h"
#include "physics.h"
#include "shader.h"
#include "scene.h"
#include "ui-debug.h"

/****************************************************************************
 * model3d
 * the actual rendered model
 ****************************************************************************/

static cerr model3d_make(struct ref *ref)
{
    struct model3d *m = container_of(ref, struct model3d, ref);

    m->depth_testing = true;
    m->cull_face     = true;
    m->draw_type     = DRAW_TYPE_TRIANGLES;

    return CERR_OK;
}

static void model3d_drop(struct ref *ref)
{
    struct model3d *m = container_of(ref, struct model3d, ref);
    int i;

    /* delete gl buffers */
    buffer_deinit(&m->vertex);
    for (i = 0; i < m->nr_lods; i++)
        buffer_deinit(&m->index[i]);
    buffer_deinit(&m->norm);
    buffer_deinit(&m->tex);
    if (m->nr_joints) {
        buffer_deinit(&m->vjoints);
        buffer_deinit(&m->weights);
    }
    vertex_array_done(&m->vao);
    ref_put(m->prog);
    trace("dropping model '%s'\n", m->name);

    while (darray_count(m->anis))
        animation_delete(&m->anis.x[0]);
    for (i = 0; i < m->nr_joints; i++) {
        darray_clearout(m->joints[i].children);
        mem_free(m->joints[i].name);
    }
    mem_free(m->joints);
    mem_free(m->collision_vx);
    mem_free(m->collision_idx);
    mem_free(m->name);
}

DECLARE_REFCLASS2(model3d);

static cerr load_gl_texture_buffer(struct shader_prog *p, void *buffer, int width, int height,
                                   int has_alpha, enum shader_vars var, texture_t *tex)
{
    texture_format color_type = has_alpha ? TEX_FMT_RGBA : TEX_FMT_RGB;
    if (!buffer)
        return CERR_INVALID_ARGUMENTS;

    if (!shader_has_var(p, var))
        return CERR_OK;

    texture_init(tex,
                 .target       = shader_get_texture_slot(p, var),
                 .wrap         = TEX_WRAP_REPEAT,
                 .min_filter   = TEX_FLT_NEAREST,
                 .mag_filter   = TEX_FLT_NEAREST);

    cerr err = texture_load(tex, color_type, width, height, buffer);
    if (err)
        return err;

    shader_set_texture(p, var);

    return CERR_OK;
}

static cerr model3dtx_add_texture_from_buffer(struct model3dtx *txm, enum shader_vars var, void *input,
                                              int width, int height, int has_alpha)
{
    texture_t *targets[] = { txm->texture, txm->normals, txm->emission, txm->sobel };
    struct shader_prog *prog = txm->model->prog;
    int slot;
    cerr err;

    slot = shader_get_texture_slot(prog, var);
    if (slot < 0)
        return -EINVAL;

    shader_prog_use(prog);
    err = load_gl_texture_buffer(prog, input, width, height, has_alpha, var,
                                 targets[slot]);
    shader_prog_done(prog);
    dbg("loaded texture%d %d %dx%d\n", slot, texture_id(txm->texture), width, height);

    return err;
}

static cerr_check model3dtx_add_texture_from_png_buffer(struct model3dtx *txm, enum shader_vars var, void *input, size_t length)
{
    int width, height, has_alpha;
    unsigned char *buffer;

    buffer = decode_png(input, length, &width, &height, &has_alpha);
    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, has_alpha);
    mem_free(buffer);

    return err;
}

static cerr model3dtx_add_texture_at(struct model3dtx *txm, enum shader_vars var, const char *name)
{
    int width = 0, height = 0, has_alpha = 0;
    unsigned char *buffer = fetch_png(name, &width, &height, &has_alpha);

    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, has_alpha);
    mem_free(buffer);

    return err;
}

static cerr model3dtx_add_texture(struct model3dtx *txm, const char *name)
{
    return model3dtx_add_texture_at(txm, UNIFORM_MODEL_TEX, name);
}

static cerr model3dtx_make(struct ref *ref)
{
    struct model3dtx *txm = container_of(ref, struct model3dtx, ref);
    txm->texture = &txm->_texture;
    txm->normals = &txm->_normals;
    txm->emission = &txm->_emission;
    txm->sobel = &txm->_sobel;
    list_init(&txm->entities);
    list_init(&txm->entry);
    return CERR_OK;
}

static bool model3dtx_tex_is_ext(struct model3dtx *txm)
{
    return txm->texture != &txm->_texture;
}

static void model3dtx_drop(struct ref *ref)
{
    struct model3dtx *txm = container_of(ref, struct model3dtx, ref);
    const char *name = txm->model->name;

    trace("dropping model3dtx [%s]\n", name);
    list_del(&txm->entry);
    /* XXX this is a bit XXX */
    if (model3dtx_tex_is_ext(txm))
        texture_done(txm->texture);
    else
        texture_deinit(txm->texture);
    if (txm->normals)
        texture_deinit(txm->normals);
    if (txm->emission)
        texture_deinit(txm->emission);
    if (txm->sobel)
        texture_deinit(txm->sobel);
    ref_put(txm->model);
}

DECLARE_REFCLASS2(model3dtx);

struct model3dtx *model3dtx_new2(struct model3d *model, const char *tex, const char *norm)
{
    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);
    if (model3dtx_add_texture(txm, tex)) {
        ref_put_last(txm);
        return NULL;
    }

    if (norm && model3dtx_add_texture_at(txm, UNIFORM_NORMAL_MAP, norm)) {
        ref_put_last(txm);
        return NULL;
    }

    txm->roughness = 0.65;
    txm->metallic = 0.45;

    return txm;

err:
    ref_put_passed(model);
    return NULL;
}

struct model3dtx *model3dtx_new(struct model3d *model, const char *name)
{
    return model3dtx_new2(model, name, NULL);
}

static void model3dtx_add_fake_emission(struct model3dtx *txm)
{
    struct model3d *model = txm->model;
    float fake_emission[4] = { 0, 0, 0, 1.0 };

    shader_prog_use(model->prog);
    cerr err = load_gl_texture_buffer(model->prog, fake_emission, 1, 1, true, UNIFORM_EMISSION_MAP,
                                      txm->emission);
    shader_prog_done(model->prog);

    warn_on(err != CERR_OK, "%s failed: %d\n", __func__, err);
}

static void model3dtx_add_fake_sobel(struct model3dtx *txm)
{
    struct model3d *model = txm->model;
    float fake_sobel[4] = { 1.0, 1.0, 1.0, 1.0 };

    shader_prog_use(model->prog);
    cerr err = load_gl_texture_buffer(model->prog, fake_sobel, 1, 1, true, UNIFORM_SOBEL_TEX,
                                      txm->sobel);
    shader_prog_done(model->prog);

    warn_on(err != CERR_OK, "%s failed: %d\n", __func__, err);
}

struct model3dtx *model3dtx_new_from_png_buffers(struct model3d *model, void *tex, size_t texsz, void *norm, size_t normsz,
                                                 void *em, size_t emsz)
{
    if (!tex || !texsz)
        goto err;

    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);

    cerr err;
    err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_MODEL_TEX, tex, texsz);
    if (err)
        goto err_3dtx;

    if (norm && normsz) {
        err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_NORMAL_MAP, norm, normsz);
        if (err)
            goto err_3dtx;
    }

    if (em && emsz) {
        err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_EMISSION_MAP, em, emsz);
        if (err)
            goto err_3dtx;
    } else {
        model3dtx_add_fake_emission(txm);
    }

    model3dtx_add_fake_sobel(txm);

    return txm;

err_3dtx:
    ref_put_last(txm);
    return NULL;
err:
    ref_put_passed(model);
    return NULL;
}

struct model3dtx *model3dtx_new_texture(struct model3d *model, texture_t *tex)
{
    if (!tex)
        goto err;

    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);
    txm->texture = tex;

    return txm;

err:
    ref_put_passed(model);
    return NULL;
}

void model3dtx_set_texture(struct model3dtx *txm, enum shader_vars var, texture_t *tex)
{
    struct shader_prog *prog = txm->model->prog;
    texture_t **targets[] = { &txm->texture, &txm->normals, &txm->emission, &txm->sobel };
    int slot = shader_get_texture_slot(prog, var);

    if (slot < 0) {
        dbg("program '%s' doesn't have texture %s or it's not a texture\n", prog->name,
            shader_get_var_name(var));
        return;
    }

    if (slot >= array_size(targets))
        return;

    *targets[slot] = tex;
}

cres(int) model3d_set_name(struct model3d *m, const char *fmt, ...)
{
    va_list ap;

    mem_free(m->name);
    va_start(ap, fmt);
    cres(int) res = mem_vasprintf(&m->name, fmt, ap);
    va_end(ap);

    return res;
}

static void model3d_calc_aabb(struct model3d *m, float *vx, size_t vxsz)
{
    int i;

    vxsz /= sizeof(float);
    vxsz /= 3;
    m->aabb[0] = m->aabb[2] = m->aabb[4] = INFINITY;
    m->aabb[1] = m->aabb[3] = m->aabb[5] = -INFINITY;
    for (i = 0; i < vxsz; i += 3) {
        m->aabb[0] = min(vx[i + 0], m->aabb[0]);
        m->aabb[1] = max(vx[i + 0], m->aabb[1]);
        m->aabb[2] = min(vx[i + 1], m->aabb[2]);
        m->aabb[3] = max(vx[i + 1], m->aabb[3]);
        m->aabb[4] = min(vx[i + 2], m->aabb[4]);
        m->aabb[5] = max(vx[i + 2], m->aabb[5]);
    }

    // dbg("bounding box for '%s': %f..%f,%f..%f,%f..%f\n", m->name,
    //     m->aabb[0], m->aabb[1], m->aabb[2], m->aabb[3], m->aabb[4], m->aabb[5]);
}

float model3d_aabb_X(struct model3d *m)
{
    return fabs(m->aabb[1] - m->aabb[0]);
}

float model3d_aabb_Y(struct model3d *m)
{
    return fabs(m->aabb[3] - m->aabb[2]);
}

float model3d_aabb_Z(struct model3d *m)
{
    return fabs(m->aabb[5] - m->aabb[4]);
}

void model3d_aabb_center(struct model3d *m, vec3 center)
{
    vec3 minv = { m->aabb[0], m->aabb[2], m->aabb[4] };
    vec3 maxv = { m->aabb[1], m->aabb[3], m->aabb[5] };

    vec3_sub(center, maxv, minv);
    vec3_scale(center, center, 0.5);
}

void model3d_add_tangents(struct model3d *m, float *tg, size_t tgsz)
{
    if (!shader_has_var(m->prog, ATTR_TANGENT)) {
        dbg("no tangent input in program '%s'\n", m->prog->name);
        return;
    }

    shader_prog_use(m->prog);
    vertex_array_bind(&m->vao);
    shader_setup_attribute(m->prog, ATTR_TANGENT, &m->tangent,
                           .type       = BUF_ARRAY,
                           .usage      = BUF_STATIC,
                           .comp_type  = DT_FLOAT,
                           .data       = tg,
                           .size       = tgsz);
    vertex_array_unbind(&m->vao);
    shader_prog_done(m->prog);
}

int model3d_add_skinning(struct model3d *m, unsigned char *joints, size_t jointssz,
                         float *weights, size_t weightssz, size_t nr_joints, mat4x4 *invmxs)
{
    int v, j, jmax = 0;

    if (jointssz != m->nr_vertices * 4 ||
        weightssz != m->nr_vertices * 4 * sizeof(float)) {
        err("wrong amount of joints or weights: %zu <> %d, %zu <> %zu\n",
            jointssz, m->nr_vertices * 4, weightssz, m->nr_vertices * 4 * sizeof(float));
        return -1;
    }

    /* incoming ivec4 joints and vec4 weights */
    for (v = 0; v < m->nr_vertices * 4; v++) {
        jmax = max(jmax, joints[v]);
        if (jmax >= JOINTS_MAX)
            return -1;
    }

    m->joints = mem_alloc(sizeof(struct model_joint), .nr = nr_joints, .fatal_fail = 1);
    for (j = 0; j < nr_joints; j++) {
        memcpy(&m->joints[j].invmx, invmxs[j], sizeof(mat4x4));
        darray_init(m->joints[j].children);
    }

    shader_prog_use(m->prog);
    vertex_array_bind(&m->vao);
    shader_setup_attribute(m->prog, ATTR_JOINTS, &m->vjoints,
                           .type       = BUF_ARRAY,
                           .usage      = BUF_STATIC,
                           .comp_type  = DT_BYTE,
                           .comp_count = 4,
                           .data       = joints,
                           .size       = jointssz);
    shader_setup_attribute(m->prog, ATTR_WEIGHTS, &m->weights,
                           .type       = BUF_ARRAY,
                           .usage      = BUF_STATIC,
                           .comp_type  = DT_FLOAT,
                           .comp_count = 4,
                           .data       = weights,
                           .size       = weightssz);
    vertex_array_unbind(&m->vao);
    shader_prog_done(m->prog);

    m->nr_joints = nr_joints;
    return 0;
}

struct model3d *
model3d_new_from_vectors(const char *name, struct shader_prog *p, float *vx, size_t vxsz,
                         unsigned short *idx, size_t idxsz, float *tx, size_t txsz,
                         float *norm, size_t normsz)
{
    struct model3d *m;

    m = ref_new(model3d);
    if (!m)
        return NULL;

    CHECK(m->name = strdup(name));
    m->prog = ref_get(p);
    m->alpha_blend = false;
    m->draw_type = DRAW_TYPE_TRIANGLES;
    model3d_calc_aabb(m, vx, vxsz);
    darray_init(m->anis);

    vertex_array_init(&m->vao);

    shader_prog_use(p);
    shader_setup_attribute(p, ATTR_POSITION, &m->vertex,
                           .type           = BUF_ARRAY,
                           .usage          = BUF_STATIC,
                           .comp_type      = DT_FLOAT,
                           .comp_count     = 3,
                           .data           = vx,
                           .size           = vxsz);
    buffer_init(&m->index[0],
                .type       = BUF_ELEMENT_ARRAY,
                .usage      = BUF_STATIC,
                .comp_type  = DT_SHORT,
                .data       = idx,
                .size       = idxsz);
    m->nr_lods++;

    shader_setup_attribute(p, ATTR_TEX, &m->tex,
                           .type           = BUF_ARRAY,
                           .usage          = BUF_STATIC,
                           .comp_type      = DT_FLOAT,
                           .comp_count     = 2,
                           .data           = tx,
                           .size           = txsz);

    if (normsz)
        shader_setup_attribute(p, ATTR_NORMAL, &m->norm,
                               .type           = BUF_ARRAY,
                               .usage          = BUF_STATIC,
                               .comp_type      = DT_FLOAT,
                               .comp_count     = 3,
                               .data           = norm,
                               .size           = normsz);

    vertex_array_unbind(&m->vao);
    shader_prog_done(p);

    m->cur_lod = -1;
    m->nr_vertices = vxsz / sizeof(*vx) / 3;
    m->nr_faces[0] = idxsz / sizeof(*idx);

    return m;
}

struct model3d *model3d_new_from_mesh(const char *name, struct shader_prog *p, struct mesh *mesh)
{
    unsigned short *lod = NULL;
    struct model3d *m;
    ssize_t nr_idx;
    int level;

    m = model3d_new_from_vectors(name, p,
                                 mesh_vx(mesh), mesh_vx_sz(mesh),
                                 mesh_idx(mesh), mesh_idx_sz(mesh),
                                 mesh_tx(mesh), mesh_tx_sz(mesh),
                                 mesh_norm(mesh), mesh_norm_sz(mesh));
    if (mesh_nr_tangent(mesh))
        model3d_add_tangents(m, mesh_tangent(mesh), mesh_tangent_sz(mesh));

    vertex_array_bind(&m->vao);
    shader_prog_use(m->prog);

    for (level = 0, nr_idx = mesh_nr_idx(mesh); level < LOD_MAX - 1; level++) {
        nr_idx = mesh_idx_to_lod(mesh, level, &lod, nr_idx);
        if (nr_idx < 0)
            break;
        dbg("lod%d for '%s' idx: %zd -> %zd\n", level, m->name, mesh_nr_idx(mesh), nr_idx);
        buffer_init(&m->index[m->nr_lods],
                    .type       = BUF_ELEMENT_ARRAY,
                    .usage      = BUF_STATIC,
                    .comp_type  = DT_SHORT,
                    .data       = lod,
                    .size       = nr_idx * mesh_idx_stride(mesh));
        mem_free(lod);
        m->nr_faces[m->nr_lods] = nr_idx;
        m->nr_lods++;
    }

    shader_prog_done(m->prog);
    vertex_array_unbind(&m->vao);

    return m;
}

static void model3d_set_lod(struct model3d *m, unsigned int lod)
{
    if (lod >= m->nr_lods)
        lod = max(0, m->nr_lods - 1);

    if (lod == m->cur_lod)
        return;

    buffer_bind(&m->index[lod], -1);
    m->cur_lod = lod;
}

static void model3d_prepare(struct model3d *m, struct shader_prog *p)
{
    vertex_array_bind(&m->vao);
    if (m->cur_lod >= 0)
        buffer_bind(&m->index[m->cur_lod], -1);
    shader_plug_attribute(p, ATTR_POSITION, &m->vertex);
    shader_plug_attribute(p, ATTR_NORMAL, &m->norm);
    shader_plug_attribute(p, ATTR_TANGENT, &m->tangent);

    if (m->nr_joints) {
        shader_plug_attribute(p, ATTR_JOINTS, &m->vjoints);
        shader_plug_attribute(p, ATTR_WEIGHTS, &m->weights);
    }
}

void model3dtx_prepare(struct model3dtx *txm, struct shader_prog *p)
{
    struct model3d *m = txm->model;

    model3d_prepare(txm->model, p);

    if (shader_has_var(p, ATTR_TEX) && texture_loaded(txm->texture)) {
        shader_plug_attribute(p, ATTR_TEX, &m->tex);
        shader_plug_texture(p, UNIFORM_MODEL_TEX, txm->texture);
    }

    if (txm->normals)
        shader_plug_texture(p, UNIFORM_NORMAL_MAP, txm->normals);

    shader_plug_texture(p, UNIFORM_EMISSION_MAP, txm->emission);
    shader_plug_texture(p, UNIFORM_SOBEL_TEX, txm->sobel);
}

static void model3dtx_draw(renderer_t *r, struct model3dtx *txm)
{
    struct model3d *m = txm->model;

    /* GL_UNSIGNED_SHORT == typeof *indices */
    renderer_draw(r, m->draw_type, m->nr_faces[m->cur_lod], DT_USHORT);
}

static void model3d_done(struct model3d *m, struct shader_prog *p)
{
    shader_unplug_attribute(p, ATTR_POSITION, &m->vertex);
    shader_unplug_attribute(p, ATTR_NORMAL, &m->norm);
    shader_unplug_attribute(p, ATTR_TANGENT, &m->tangent);

    if (m->nr_joints) {
        shader_unplug_attribute(p, ATTR_JOINTS, &m->vjoints);
        shader_unplug_attribute(p, ATTR_WEIGHTS, &m->weights);
    }

    if (m->cur_lod >= 0)
        buffer_unbind(&m->index[m->cur_lod], -1);
    vertex_array_unbind(&m->vao);
    m->cur_lod = -1;
}

void model3dtx_done(struct model3dtx *txm, struct shader_prog *p)
{
    if (buffer_loaded(&txm->model->tex)) {
        shader_unplug_attribute(p, ATTR_TEX, &txm->model->tex);
        shader_unplug_texture(p, UNIFORM_MODEL_TEX, txm->texture);
    }
    shader_unplug_texture(p, UNIFORM_NORMAL_MAP, txm->normals);
    shader_unplug_texture(p, UNIFORM_EMISSION_MAP, txm->emission);
    shader_unplug_texture(p, UNIFORM_SOBEL_TEX, txm->sobel);

    model3d_done(txm->model, p);
}

struct animation *animation_new(struct model3d *model, const char *name, unsigned int nr_channels)
{
    struct animation *an;

    CHECK(an = darray_add(model->anis));
    an->name = strdup(name);
    an->model = model;
    an->nr_channels = nr_channels;
    an->cur_channel = 0;
    an->channels = mem_alloc(sizeof(struct channel), .nr = an->nr_channels, .fatal_fail = 1);

    return an;
}

void animation_delete(struct animation *an)
{
    struct animation *_an;
    int i, idx = 0;

    darray_for_each(_an, an->model->anis) {
        if (_an == an)
            goto found;
        idx++;
    }

    err("trying to delete an non-existent animation '%s'\n", an->name);
    return;

found:
    for (i = 0; i < an->nr_channels; i++) {
        mem_free(an->channels[i].time);
        mem_free(an->channels[i].data);
    }
    mem_free(an->channels);
    mem_free(an->name);
    darray_delete(an->model->anis, idx);
}

void animation_add_channel(struct animation *an, size_t frames, float *time, float *data,
                           size_t data_stride, unsigned int target, unsigned int path)
{
    if (an->cur_channel == an->nr_channels)
        return;

    an->channels[an->cur_channel].time = memdup(time, sizeof(float) * frames);
    an->channels[an->cur_channel].data = memdup(data, data_stride * frames);
    an->channels[an->cur_channel].nr = frames;
    an->channels[an->cur_channel].stride = data_stride;
    an->channels[an->cur_channel].target = target;
    an->channels[an->cur_channel].path = path;
    an->cur_channel++;

    an->time_end = max(an->time_end, time[frames - 1]/* + time[1] - time[0]*/);
}

void models_render(renderer_t *r, struct mq *mq, struct shader_prog *shader_override,
                   struct light *light, struct camera *camera, struct matrix4f *proj_mx,
                   struct entity3d *focus, int width, int height, int cascade,
                   unsigned long *count)
{
    struct entity3d *e;
    struct shader_prog *prog = NULL;
    struct model3d *model;
    struct model3dtx *txmodel;
    struct view *view = NULL;
    struct subview *subview = NULL;
    unsigned long nr_ents = 0, culled = 0;

    if (camera)
        view = &camera->view;
    else if (light) {
        view = &light->view[0];
        proj_mx = &light->view[0].main.proj_mx;
    }

    if (view) {
        if (cascade >= 0 && cascade < CASCADES_MAX) {
            subview = &view->subview[cascade];
            proj_mx = &subview->proj_mx;
        } else {
            subview = &view->main;
        }
    }

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        model = txmodel->model;
        struct shader_prog *model_prog = shader_override ? shader_override : model->prog;

        model->cur_lod = 0;

        cull_face cull = CULL_FACE_NONE;
        if (model->cull_face)
            cull = shader_override ? CULL_FACE_FRONT : CULL_FACE_BACK;
        renderer_cull_face(r, cull);

        renderer_blend(r, model->alpha_blend, BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA);

        /* TODO: add a separate property for depth test control */
        renderer_depth_test(r, model->depth_testing);

        if (model_prog != prog) {
            if (prog)
                shader_prog_done(prog);

            prog = model_prog;
            shader_prog_use(prog);

            shader_set_var_float(prog, UNIFORM_WIDTH, width);
            shader_set_var_float(prog, UNIFORM_HEIGHT, height);

            if (light) {
                shader_set_var_ptr(prog, UNIFORM_LIGHT_POS, LIGHTS_MAX, light->pos);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_COLOR, LIGHTS_MAX, light->color);
                shader_set_var_ptr(prog, UNIFORM_ATTENUATION, LIGHTS_MAX, light->attenuation);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_DIR, LIGHTS_MAX, light->dir);
                shader_set_var_int(prog, UNIFORM_SHADOW_OUTLINE, light->shadow_outline);
                if (shader_has_var(prog, UNIFORM_SHADOW_MVP)) {
                    mat4x4 mvp[CASCADES_MAX];
                    int i;

                    for (i = 0; i < CASCADES_MAX; i++) {
                        struct subview *light_sv = &light->view[0].subview[i];
                        mat4x4_mul(mvp[i], light_sv->proj_mx.m, light_sv->view_mx.m);
                    }
                    shader_set_var_ptr(prog, UNIFORM_SHADOW_MVP, CASCADES_MAX, mvp);
                }
                if (light->shadow[0][0]) {
#ifdef CONFIG_GLES
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP, light->shadow[0][0]);
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP1, light->shadow[0][1] ? light->shadow[0][1] : white_pixel());
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP2, light->shadow[0][2] ? light->shadow[0][2] : white_pixel());
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP3, light->shadow[0][3] ? light->shadow[0][2] : white_pixel());
                    shader_set_var_int(prog, UNIFORM_USE_MSAA, false);
#else
                    if (shader_has_var(prog, UNIFORM_SHADOW_MAP_MS)) {
                        shader_set_var_int(prog, UNIFORM_USE_MSAA, light->shadow_msaa);
                        shader_plug_textures_multisample(prog, light->shadow_msaa,
                                                         UNIFORM_SHADOW_MAP, UNIFORM_SHADOW_MAP_MS,
                                                         light->shadow[0][0]);
                    } else {
                        shader_set_var_int(prog, UNIFORM_USE_MSAA, false);
                    }
#endif /* CONFIG_GLES */
                }
            }

            if (view)
                shader_set_var_ptr(prog, UNIFORM_CASCADE_DISTANCES, CASCADES_MAX, view->divider);

            if (subview) {
                shader_set_var_ptr(prog, UNIFORM_VIEW, 1, subview->view_mx.cell);
                shader_set_var_ptr(prog, UNIFORM_INVERSE_VIEW, 1, subview->inv_view_mx.cell);
            }

            if (proj_mx)
                shader_set_var_ptr(prog, UNIFORM_PROJ, 1, proj_mx->cell);
        }

        model3dtx_prepare(txmodel, prog);

        shader_set_var_int(prog, UNIFORM_USE_NORMALS, texture_loaded(txmodel->normals));

        shader_set_var_float(prog, UNIFORM_SHINE_DAMPER, txmodel->roughness);
        shader_set_var_float(prog, UNIFORM_REFLECTIVITY, txmodel->metallic);

        list_for_each_entry (e, &txmodel->entities, entry) {
            float hc[] = { 0.7, 0.7, 0.0, 1.0 }, nohc[] = { 0.0, 0.0, 0.0, 0.0 };
            if (!e->visible)
                continue;

            if (!e->skip_culling &&
                view && !view_entity_in_frustum(view, e)) {
                culled++;
                continue;
            }

            if (camera && camera->ch) {
                unsigned int lod;
                vec3 dist;

                vec3_dup(dist, e->pos);

                vec3_sub(dist, dist, camera->ch->entity->pos);
                lod = vec3_len(dist) / 80;
                model3d_set_lod(model, lod);
            }

            renderer_wireframe(r, focus == e);

            shader_set_var_int(prog, UNIFORM_ALBEDO_TEXTURE, !!e->priv);  /* e->priv now points to character */
            shader_set_var_int(prog, UNIFORM_ENTITY_HASH, fletcher32((void *)&e, sizeof(e) / 2));
            shader_set_var_ptr(prog, UNIFORM_IN_COLOR, 1, e->color);
            shader_set_var_int(prog, UNIFORM_COLOR_PASSTHROUGH, e->color_pt);

            if (focus)
                shader_set_var_ptr(prog, UNIFORM_HIGHLIGHT_COLOR, 1, focus == e ? hc : nohc);

            if (model->nr_joints && model->anis.da.nr_el) {
                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 1);
                shader_set_var_ptr(prog, UNIFORM_JOINT_TRANSFORMS, model->nr_joints, e->joint_transforms);
            } else {
                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 0);
            }

            shader_set_var_ptr(prog, UNIFORM_TRANS, 1, e->mx->cell);

            model3dtx_draw(r, txmodel);
            nr_ents++;
        }
        model3dtx_done(txmodel, prog);
    }

    if (count)
        *count = nr_ents;
    if (prog)
        shader_prog_done(prog);
    if (camera && culled)
        ui_debug_printf("culled entities: %lu", culled);
}

/****************************************************************************
 * entity3d
 * instance of the model3d
 ****************************************************************************/

float entity3d_aabb_X(struct entity3d *e)
{
    return model3d_aabb_X(e->txmodel->model) * e->scale;
}

float entity3d_aabb_Y(struct entity3d *e)
{
    return model3d_aabb_Y(e->txmodel->model) * e->scale;
}

float entity3d_aabb_Z(struct entity3d *e)
{
    return model3d_aabb_Z(e->txmodel->model) * e->scale;
}

void entity3d_aabb_update(struct entity3d *e)
{
    struct model3d *m = e->txmodel->model;
    vec4 corners[] = {
        { m->aabb[0], m->aabb[2], m->aabb[4], 1.0 },
        { m->aabb[0], m->aabb[3], m->aabb[4], 1.0 },
        { m->aabb[0], m->aabb[2], m->aabb[5], 1.0 },
        { m->aabb[0], m->aabb[3], m->aabb[5], 1.0 },
        { m->aabb[1], m->aabb[2], m->aabb[4], 1.0 },
        { m->aabb[1], m->aabb[3], m->aabb[4], 1.0 },
        { m->aabb[1], m->aabb[2], m->aabb[5], 1.0 },
        { m->aabb[1], m->aabb[3], m->aabb[5], 1.0 },
    };
    vec4 v;
    int i;

    e->aabb[0] = e->aabb[2] = e->aabb[3] = INFINITY;
    e->aabb[1] = e->aabb[3] = e->aabb[5] = -INFINITY;
    for (i = 0; i < array_size(corners); i++) {
        mat4x4_mul_vec4(v, e->mx->m, corners[i]);
        e->aabb[0] = min(v[0], e->aabb[0]);
        e->aabb[1] = max(v[0], e->aabb[1]);
        e->aabb[2] = min(v[1], e->aabb[2]);
        e->aabb[3] = max(v[1], e->aabb[3]);
        e->aabb[4] = min(v[2], e->aabb[4]);
        e->aabb[5] = max(v[2], e->aabb[5]);
    }
}

void entity3d_aabb_min(struct entity3d *e, vec3 min)
{
    min[0] = e->aabb[0];
    min[1] = e->aabb[2];
    min[2] = e->aabb[4];
}

void entity3d_aabb_max(struct entity3d *e, vec3 max)
{
    max[0] = e->aabb[1];
    max[1] = e->aabb[3];
    max[2] = e->aabb[5];
}

void entity3d_aabb_center(struct entity3d *e, vec3 center)
{
    vec3 minv = { e->txmodel->model->aabb[0], e->txmodel->model->aabb[2], e->txmodel->model->aabb[4] };
    // center[0] = entity3d_aabb_X(e) + e->dx;
    // center[1] = entity3d_aabb_Y(e) + e->dy;
    // center[2] = entity3d_aabb_Z(e) + e->dz;
    model3d_aabb_center(e->txmodel->model, center);
    vec3_add(center, center, minv);
}

void model3d_skeleton_add(struct model3d *model, int joint, int parent)
{}

static void channel_time_to_idx(struct channel *chan, float time, int start, int *prev, int *next)
{
    int i;

    if (time < chan->time[0])
        goto tail;

    if (time < chan->time[start])
        start = 0;

    for (i = start; i < chan->nr && time > chan->time[i]; i++)
        ;
    if (i == chan->nr)
        goto tail;

    *prev = max(i - 1, 0);
    *next = min(*prev + 1, chan->nr - 1);
    return;

tail:
    *prev = chan->nr - 1;
    *next = 0;
}

static void channel_transform(struct entity3d *e, struct channel *chan, float time)
{
    void *p_data, *n_data;
    struct joint *joint = &e->joints[chan->target];
    float p_time, n_time, fac = 0;
    int prev, next;

    channel_time_to_idx(chan, time, joint->off[chan->path], &prev, &next);
    joint->off[chan->path] = min(prev, next);

    p_time = chan->time[prev];
    n_time = chan->time[next];
    if (p_time > n_time)
        fac = time < n_time ? 1 : 0;
    else if (p_time < n_time)
        fac = (time - p_time) / (n_time - p_time);

    p_data = (void *)chan->data + prev * chan->stride;
    n_data = (void *)chan->data + next * chan->stride;

    switch (chan->path) {
    case PATH_TRANSLATION: {
        vec3 *p_pos = p_data, *n_pos = n_data;

        vec3_interp(joint->translation, *p_pos, *n_pos, fac);
        break;
    }
    case PATH_ROTATION: {
        quat *n_rot = n_data, *p_rot = p_data;

        quat_slerp(joint->rotation, *p_rot, *n_rot, fac);
        break;
    }
    case PATH_SCALE: {
        vec3 *n_scale = n_data, *p_scale = p_data;
        vec3_interp(joint->scale, *p_scale, *n_scale, fac);
        break;
    }
    }
}

static void channels_transform(struct entity3d *e, struct animation *an, float time)
{
    int ch;

    for (ch = 0; ch < an->nr_channels; ch++)
        channel_transform(e, &an->channels[ch], time);
}

static void one_joint_transform(struct entity3d *e, int joint, int parent)
{
    struct model3d *model = e->txmodel->model;
    mat4x4 *parentjt = NULL, R, *invglobal;
    struct joint *j = &e->joints[joint];
    mat4x4 *jt, T;
    int *child;

    /* inverse bind matrix */
    invglobal = &model->joints[joint].invmx;

    jt = &j->global;
    mat4x4_identity(*jt);

    if (parent >= 0)
        parentjt = &e->joints[parent].global;
    else
        parentjt = &model->root_pose;
    if (parentjt)
        mat4x4_mul(*jt, *parentjt, *jt);

    mat4x4_translate(T,
                     j->translation[0],
                     j->translation[1],
                     j->translation[2]);
    mat4x4_mul(*jt, *jt, T);
    mat4x4_from_quat(R, j->rotation);
    mat4x4_mul(*jt, *jt, R);
    mat4x4_scale_aniso(*jt, *jt,
                       j->scale[0],
                       j->scale[1],
                       j->scale[2]);

    /*
     * jt = parentjt * T * R * S
     * joint_transform = invglobal * jt
     */
    mat4x4_mul(e->joint_transforms[joint], *jt, *invglobal);

    darray_for_each(child, model->joints[joint].children)
        one_joint_transform(e, *child, joint);
}

void animation_start(struct entity3d *e, struct scene *scene, int ani)
{
    struct model3d *model = e->txmodel->model;
    struct animation *an;
    struct channel *chan;
    int ch;

    if (!model->anis.da.nr_el)
        return;

    if (ani >= model->anis.da.nr_el)
        ani %= model->anis.da.nr_el;
    an = &model->anis.x[ani];
    for (ch = 0; ch < an->nr_channels; ch++) {
        chan = &an->channels[ch];
        e->joints[chan->target].off[chan->path] = 0;
    }
    e->ani_time = clap_get_current_time(scene->clap_ctx);
}

int animation_by_name(struct model3d *m, const char *name)
{
    int i;

    for (i = 0; i < m->anis.da.nr_el; i++)
        if (!strcmp(name, m->anis.x[i].name))
            return i;
    return -1;
}

static struct queued_animation *ani_current(struct entity3d *e)
{
    if (e->animation >= e->aniq.da.nr_el)
        return NULL;
    return &e->aniq.x[e->animation];
}

static void animation_end(struct queued_animation *qa, struct scene *s)
{
    void (*end)(struct scene *, void *) = qa->end;

    if (!end)
        return;

    qa->end = NULL;
    end(s, qa->end_priv);
}

static void animation_next(struct entity3d *e, struct scene *s)
{
    struct queued_animation *qa;

    if (!e->aniq.da.nr_el || e->animation < 0) {
        struct model3d *model = e->txmodel->model;
        struct animation *an;

        animation_push_by_name(e, s, "idle", true, true);
        /* randomize phase, should probably be in instantiate instead */
        qa = ani_current(e);
        an = &model->anis.x[qa->animation];
        e->ani_time = (long)s->frames_total - an->time_end * drand48();
        return;
    }
    qa = ani_current(e);
    if (!qa->repeat) {
        animation_end(qa, s);
        if (e->ani_cleared) {
            e->ani_cleared = false;
            return;
        }
        e->animation = (e->animation + 1) % e->aniq.da.nr_el;
        qa = ani_current(e);
    }
    animation_start(e, s, qa->animation);
}

void animation_set_end_callback(struct entity3d *e, void (*end)(struct scene *, void *), void *priv)
{
    int nr_qas = darray_count(e->aniq);

    if (!nr_qas)
        return;

    e->aniq.x[nr_qas - 1].end = end;
    e->aniq.x[nr_qas - 1].end_priv = priv;
}

void animation_set_speed(struct entity3d *e, float speed)
{
    struct queued_animation *qa = ani_current(e);

    if (!qa)
        return;

    qa->speed = speed;
}

void animation_push_by_name(struct entity3d *e, struct scene *s, const char *name,
                            bool clear, bool repeat)
{
    struct queued_animation *qa;
    int id = animation_by_name(e->txmodel->model, name);

    if (id < 0)
        id = 0;

    if (clear) {
        struct queued_animation _qa;

        qa = ani_current(e);
        if (qa)
            memcpy(&_qa, qa, sizeof(_qa));

        darray_clearout(e->aniq);

        if (qa)
            animation_end(&_qa, s);
    }
    qa = darray_add(e->aniq);
    qa->animation = id;
    qa->repeat = repeat;
    qa->speed = 1.0;
    if (clear) {
        animation_start(e, s, id);
        e->animation = 0;
        e->ani_cleared = true;
    }
}

static void animated_update(struct entity3d *e, struct scene *s)
{
    double time = clap_get_current_time(s->clap_ctx);
    struct model3d *model = e->txmodel->model;
    struct queued_animation *qa;
    struct animation *an;

    if (e->animation < 0)
        animation_next(e, s);
    qa = ani_current(e);
    an = &model->anis.x[qa->animation];
    channels_transform(e, an, (time - e->ani_time) * qa->speed);
    one_joint_transform(e, 0, -1);

    if ((time - e->ani_time) * qa->speed >= an->time_end)
        animation_next(e, s);
}

/*
 * If entity's position/rotation/scale (TRS) haven't been updated, this avoids
 * needlessly updating its model matrix (entity3d::mx).
 */
static bool needs_update(struct entity3d *e)
{
    if (e->updated) {
        e->updated = 0;
        return true;
    }
    return false;
}

static int default_update(struct entity3d *e, void *data)
{
    struct scene *scene = data;

    if (needs_update(e)) {
        mat4x4_identity(e->mx->m);
        mat4x4_translate_in_place(e->mx->m, e->pos[0], e->pos[1], e->pos[2]);
        mat4x4_rotate_X(e->mx->m, e->mx->m, e->rx);
        mat4x4_rotate_Y(e->mx->m, e->mx->m, e->ry);
        mat4x4_rotate_Z(e->mx->m, e->mx->m, e->rz);
        mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);

        entity3d_aabb_update(e);

        if (scene && e->light_idx >= 0) {
            float pos[3];
            vec3_add(pos, e->pos, e->light_off);
            light_set_pos(&scene->light, e->light_idx, pos);
        }
    }

    if (!scene)
        return 0;

    if (entity_animated(e))
        animated_update(e, scene);
    if (scene->debug_draws_enabled && e->phys_body)
        phys_debug_draw(scene, e->phys_body);

    return 0;
}

void entity3d_reset(struct entity3d *e)
{
    default_update(e, NULL);
}

static cerr entity3d_make(struct ref *ref)
{
    struct entity3d *e = container_of(ref, struct entity3d, ref);

    darray_init(e->aniq);
    e->animation = -1;
    e->light_idx = -1;
    e->scale     = 1.0;
    e->visible   = 1;
    e->update    = default_update;
    e->mx        = mx_new();
    if (!e->mx)
        return CERR_NOMEM;

    return CERR_OK;
}

static void entity3d_drop(struct ref *ref)
{
    struct entity3d *e = container_of(ref, struct entity3d, ref);
    trace("dropping entity3d\n");
    list_del(&e->entry);
    ref_put(e->txmodel);

    darray_clearout(e->aniq);
    if (e->phys_body) {
        phys_body_done(e->phys_body);
        e->phys_body = NULL;
    }
    mem_free(e->joints);
    mem_free(e->joint_transforms);
    mem_free(e->mx);
}

DECLARE_REFCLASS2(entity3d);

struct entity3d *entity3d_new(struct model3dtx *txm)
{
    struct model3d *model = txm->model;
    struct entity3d *e;

    /*
     * XXX this is ef'ed up
     * The reason is that mq_release() iterates txms' entities list
     * and when the last entity drops, the txm would disappear,
     * making it impossible to continue. Fixing that.
     */
    // ref_shared(txm);
    e = ref_new(entity3d);
    if (!e)
        return NULL;

    e->txmodel = ref_get(txm);
    entity3d_aabb_update(e);
    if (model->anis.da.nr_el) {
        e->joints = mem_alloc(sizeof(*e->joints), .nr = model->nr_joints, .fatal_fail = 1);
        e->joint_transforms = mem_alloc(sizeof(mat4x4), .nr = model->nr_joints, .fatal_fail = 1);
    }

    return e;
}

/* XXX: static inline? via macro? */
void entity3d_put(struct entity3d *e)
{
    ref_put(e);
}

void entity3d_update(struct entity3d *e, void *data)
{
    if (e->update)
        e->update(e, data);
}

void entity3d_add_physics(struct entity3d *e, struct phys *phys, double mass, int class,
                          int type, double geom_off, double geom_radius, double geom_length)
{
    e->phys_body = phys_body_new(phys, e, class, geom_radius, geom_off, type, mass);
}

void entity3d_visible(struct entity3d *e, unsigned int visible)
{
    e->visible = visible;
}

void entity3d_rotate_X(struct entity3d *e, float rx)
{
    e->rx = rx;
    e->updated++;
}

void entity3d_rotate_Y(struct entity3d *e, float ry)
{
    e->ry = ry;
    e->updated++;
}

void entity3d_rotate_Z(struct entity3d *e, float rz)
{
    e->rz = rz;
    e->updated++;
}

void entity3d_scale(struct entity3d *e, float scale)
{
    e->scale = scale;
    e->updated++;
}

void entity3d_position(struct entity3d *e, vec3 pos)
{
    e->updated++;
    vec3_dup(e->pos, pos);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, e->pos);
}

void entity3d_move(struct entity3d *e, vec3 off)
{
    e->updated++;
    vec3_add(e->pos, e->pos, off);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, e->pos);
}

void model3dtx_add_entity(struct model3dtx *txm, struct entity3d *e)
{
    list_append(&txm->entities, &e->entry);
}

struct entity3d *instantiate_entity(struct model3dtx *txm, struct instantiator *instor,
                                    bool randomize_yrot, float randomize_scale, struct scene *scene)
{
    struct entity3d *e = entity3d_new(txm);
    entity3d_position(e, (vec3){ instor->dx, instor->dy, instor->dz });
    if (randomize_yrot)
        entity3d_rotate_Y(e, drand48() * 360);
    if (randomize_scale)
        entity3d_scale(e, 1 + randomize_scale * (1 - drand48() * 2));
    default_update(e, scene);
    model3dtx_add_entity(txm, e);
    return e;
}

struct debug_draw {
    struct ref      ref;
    struct entity3d *entity;
    struct list     entry;
};

static void debug_draw_drop(struct ref *ref)
{
    struct debug_draw *dd = container_of(ref, struct debug_draw, ref);

    ref_put(dd->entity);
}
DECLARE_REFCLASS(debug_draw);

struct debug_draw *__debug_draw_new(struct scene *scene, float *vx, size_t vxsz,
                                    unsigned short *idx, size_t idxsz, float *tx, mat4x4 *rot)
{
    struct shader_prog *p;
    struct debug_draw *dd;
    struct model3dtx *txm;
    struct model3d *m;

    p = shader_prog_find(&scene->shaders, "debug");
    CHECK(dd = ref_new(debug_draw));
    CHECK(m = model3d_new_from_vectors("debug", p, vx, vxsz, idx, idxsz, tx, vxsz / 3 * 2, NULL, 0));
    m->depth_testing = false;
    m->draw_type = DRAW_TYPE_LINES;
    ref_put(p);

    CHECK(txm = ref_new(model3dtx));
    txm->model = m;
    list_init(&txm->entities);
    mq_add_model(&scene->debug_mq, txm);
    CHECK(dd->entity = entity3d_new(txm));
    model3dtx_add_entity(txm, dd->entity);
    entity3d_visible(dd->entity, 1);
    dd->entity->update = NULL;
    dd->entity->color_pt = COLOR_PT_ALL;
    dd->entity->color[0] = 1.0;
    dd->entity->color[3] = 1.0;
    if (rot)
        memcpy(dd->entity->mx->m, rot, sizeof(mat4x4));
    else
        mat4x4_identity(dd->entity->mx->m);
    entity3d_aabb_update(dd->entity);
    list_append(&scene->debug_draws, &dd->entry);

    return dd;
}

static struct debug_draw *__debug_draw_line(struct scene *scene, vec3 a, vec3 b, mat4x4 *rot)
{
    float vx[] = { a[0], a[1], a[2], b[0], b[1], b[2] };
    unsigned short idx[] = { 0, 1 };
    return __debug_draw_new(scene, vx, sizeof(vx), idx, sizeof(idx), NULL, rot);
}

void debug_draw_line(struct scene *scene, vec3 a, vec3 b, mat4x4 *rot)
{
    if (scene->debug_draws_enabled)
        (void)__debug_draw_line(scene, a, b, rot);
}

void debug_draw_clearout(struct scene *scene)
{
    while (!list_empty(&scene->debug_draws)) {
        struct debug_draw *dd = list_first_entry(&scene->debug_draws, struct debug_draw, entry);

        list_del(&dd->entry);
        ref_put(dd);
    }
}

void mq_init(struct mq *mq, void *priv)
{
    list_init(&mq->txmodels);
    mq->priv = priv;
}

void mq_release(struct mq *mq)
{
    struct model3dtx *txmodel;
    struct entity3d *ent;

    while (!list_empty(&mq->txmodels)) {
        bool done = false;

        txmodel = list_first_entry(&mq->txmodels, struct model3dtx, entry);

        do {
            if (list_empty(&txmodel->entities))
                break;

            ent = list_first_entry(&txmodel->entities, struct entity3d, entry);
            if (ent == list_last_entry(&txmodel->entities, struct entity3d, entry)) {
                done = true;
                ref_only(txmodel);
            }

            if (ent->destroy)
                ent->destroy(ent);
            else
                ref_put(ent);
        } while (!done);
    }
}

void mq_for_each(struct mq *mq, void (*cb)(struct entity3d *, void *), void *data)
{
    struct model3dtx *txmodel;
    struct entity3d *ent, *itent;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            cb(ent, data);
        }
    }
}

void mq_update(struct mq *mq)
{
    mq_for_each(mq, entity3d_update, mq->priv);
}

struct model3dtx *mq_model_first(struct mq *mq)
{
    return list_first_entry(&mq->txmodels, struct model3dtx, entry);
}

struct model3dtx *mq_model_last(struct mq *mq)
{
    return list_last_entry(&mq->txmodels, struct model3dtx, entry);
}

void mq_add_model(struct mq *mq, struct model3dtx *txmodel)
{
    txmodel = ref_pass(txmodel);
    list_append(&mq->txmodels, &txmodel->entry);
}

void mq_add_model_tail(struct mq *mq, struct model3dtx *txmodel)
{
    txmodel = ref_pass(txmodel);
    list_prepend(&mq->txmodels, &txmodel->entry);
}

struct model3dtx *mq_nonempty_txm_next(struct mq *mq, struct model3dtx *txm, bool fwd)
{
    struct model3dtx *first_txm = mq_model_first(mq);
    struct model3dtx *last_txm = mq_model_last(mq);
    struct model3dtx *next_txm;

    if (list_empty(&mq->txmodels))
        return NULL;

    if (!txm)
        txm = fwd ? last_txm : first_txm;
    next_txm = txm;

    do {
        if (!fwd && next_txm == first_txm)
            next_txm = last_txm;
        else if (fwd && next_txm == last_txm)
            next_txm = first_txm;
        else
            next_txm = fwd ?
                list_next_entry(next_txm, entry) :
                list_prev_entry(next_txm, entry);
    } while (list_empty(&next_txm->entities) && next_txm != txm);

    return list_empty(&next_txm->entities) ? NULL : next_txm;
}
