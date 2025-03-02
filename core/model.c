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

static void model3d_lods_from_mesh(model3d *m, struct mesh *mesh)
{
    unsigned short *lod = NULL;
    ssize_t nr_idx;
    int level;

    for (level = 0, nr_idx = mesh_nr_idx(mesh); level < LOD_MAX - 1; level++) {
        nr_idx = mesh_idx_to_lod(mesh, level, &lod, nr_idx);
        if (nr_idx < 0)
            break;
        cerr err = buffer_init(&m->index[m->nr_lods],
                               .type       = BUF_ELEMENT_ARRAY,
                               .usage      = BUF_STATIC,
                               .comp_type  = DT_SHORT,
                               .data       = lod,
                               .size       = nr_idx * mesh_idx_stride(mesh));
        mem_free(lod);

        if (IS_CERR(err))
            continue;

        dbg("lod%d for '%s' idx: %zd -> %zd\n", level, m->name, mesh_nr_idx(mesh), nr_idx);
        m->nr_faces[m->nr_lods] = nr_idx;
        m->nr_lods++;
    }
}

static void model3d_calc_aabb(model3d *m, float *vx, size_t vxsz);

static cerr model3d_make(struct ref *ref, void *_opts)
{
    rc_init_opts(model3d) *opts = _opts;

    if (!opts->prog)
        return CERR_INVALID_ARGUMENTS;

    float *vx, *tx, *norm;
    unsigned short *idx;
    size_t vxsz, txsz, normsz, idxsz;

    if (!opts->mesh) {
        if (!opts->idx || !opts->idxsz || !opts->vx || !opts->vxsz)
            return CERR_INVALID_ARGUMENTS;

        vx      = opts->vx;
        vxsz    = opts->vxsz;
        idx     = opts->idx;
        idxsz   = opts->idxsz;
        tx      = opts->tx;
        txsz    = opts->txsz;
        norm    = opts->norm;
        normsz  = opts->normsz;
    } else {
        if (opts->idx || opts->idxsz || opts->vx || opts->vxsz ||
            opts->tx || opts->txsz || opts->norm || opts->normsz)
            return CERR_INVALID_ARGUMENTS;

        vx      = mesh_vx(opts->mesh);
        vxsz    = mesh_vx_sz(opts->mesh);
        idx     = mesh_idx(opts->mesh);
        idxsz   = mesh_idx_sz(opts->mesh);
        tx      = mesh_tx(opts->mesh);
        txsz    = mesh_tx_sz(opts->mesh);
        norm    = mesh_norm(opts->mesh);
        normsz  = mesh_norm_sz(opts->mesh);
    }

    model3d *m = container_of(ref, model3d, ref);

    m->depth_testing = true;
    m->cull_face     = true;
    m->draw_type     = DRAW_TYPE_TRIANGLES;

    CHECK(m->name = strdup(opts->name));
    m->prog = ref_get(opts->prog);
    m->alpha_blend = false;
    model3d_calc_aabb(m, vx, vxsz);
    darray_init(m->anis);

    shader_prog_use(opts->prog);

    cerr err = vertex_array_init(&m->vao);
    if (IS_CERR(err))
        goto unbind;

    err = shader_setup_attribute(opts->prog, ATTR_POSITION, &m->vertex,
                                 .type           = BUF_ARRAY,
                                 .usage          = BUF_STATIC,
                                 .comp_type      = DT_FLOAT,
                                 .comp_count     = 3,
                                 .data           = vx,
                                 .size           = vxsz);
    if (IS_CERR(err))
        goto vao_done;

    err = buffer_init(&m->index[0],
                      .type       = BUF_ELEMENT_ARRAY,
                      .usage      = BUF_STATIC,
                      .comp_type  = DT_SHORT,
                      .data       = idx,
                      .size       = idxsz);
    if (IS_CERR(err))
        goto pos_done;

    m->nr_lods++;

    if (tx) {
        err = shader_setup_attribute(opts->prog, ATTR_TEX, &m->tex,
                                     .type           = BUF_ARRAY,
                                     .usage          = BUF_STATIC,
                                     .comp_type      = DT_FLOAT,
                                     .comp_count     = 2,
                                     .data           = tx,
                                     .size           = txsz);
        if (IS_CERR(err))
            goto tex_done;
    }

    if (normsz) {
        err = shader_setup_attribute(opts->prog, ATTR_NORMAL, &m->norm,
                                     .type           = BUF_ARRAY,
                                     .usage          = BUF_STATIC,
                                     .comp_type      = DT_FLOAT,
                                     .comp_count     = 3,
                                     .data           = norm,
                                     .size           = normsz);
        if (IS_CERR(err))
            goto norm_done;
    }

    if (opts->mesh && mesh_nr_tangent(opts->mesh)) {
        err = shader_setup_attribute(opts->prog, ATTR_TANGENT, &m->tangent,
            .type       = BUF_ARRAY,
            .usage      = BUF_STATIC,
            .comp_type  = DT_FLOAT,
            .data       = mesh_tangent(opts->mesh),
            .size       = mesh_tangent_sz(opts->mesh));
        if (IS_CERR(err))
            goto tangent_done;
    }

    if (opts->mesh)
        model3d_lods_from_mesh(m, opts->mesh);

    vertex_array_unbind(&m->vao);
    shader_prog_done(opts->prog);

    m->cur_lod = -1;
    m->nr_vertices = vxsz / sizeof(*vx) / 3;
    m->nr_faces[0] = idxsz / sizeof(*idx);

    return CERR_OK;

tangent_done:
    buffer_deinit(&m->tangent);
norm_done:
    buffer_deinit(&m->norm);
tex_done:
    buffer_deinit(&m->tex);
pos_done:
    buffer_deinit(&m->vertex);
vao_done:
    vertex_array_done(&m->vao);

unbind:
    vertex_array_unbind(&m->vao);
    shader_prog_done(opts->prog);

    return err;
}

static void model3d_drop(struct ref *ref)
{
    model3d *m = container_of(ref, model3d, ref);
    int i;

    /* delete gl buffers */
    buffer_deinit(&m->vertex);
    for (i = 0; i < m->nr_lods; i++)
        buffer_deinit(&m->index[i]);
    buffer_deinit(&m->tangent);
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

DEFINE_REFCLASS2(model3d);

DEFINE_CLEANUP(model3d, if (*p) ref_put(*p))

static cerr load_gl_texture_buffer(struct shader_prog *p, void *buffer, int width, int height,
                                   int has_alpha, enum shader_vars var, texture_t *tex)
{
    texture_format color_type = has_alpha ? TEX_FMT_RGBA : TEX_FMT_RGB;
    if (!buffer)
        return CERR_INVALID_ARGUMENTS;

    if (!shader_has_var(p, var))
        return CERR_OK;

    cerr err = texture_init(tex,
                            .target       = shader_get_texture_slot(p, var),
                            .wrap         = TEX_WRAP_REPEAT,
                            .min_filter   = TEX_FLT_NEAREST,
                            .mag_filter   = TEX_FLT_NEAREST);
    if (IS_CERR(err))
        return err;

    err = texture_load(tex, color_type, width, height, buffer);
    if (IS_CERR(err))
        return err;

    shader_set_texture(p, var);

    return CERR_OK;
}

static cerr model3dtx_add_texture_from_buffer(model3dtx *txm, enum shader_vars var, void *input,
                                              int width, int height, int has_alpha)
{
    texture_t *targets[] = { txm->texture, txm->normals, txm->emission, txm->sobel };
    struct shader_prog *prog = txm->model->prog;
    int slot;
    cerr err;

    slot = shader_get_texture_slot(prog, var);
    if (slot < 0)
        return CERR_INVALID_ARGUMENTS;

    shader_prog_use(prog);
    err = load_gl_texture_buffer(prog, input, width, height, has_alpha, var,
                                 targets[slot]);
    shader_prog_done(prog);
    dbg("loaded texture%d %d %dx%d\n", slot, texture_id(txm->texture), width, height);

    return err;
}

static cerr_check model3dtx_add_texture_from_png_buffer(model3dtx *txm, enum shader_vars var, void *input, size_t length)
{
    int width, height, has_alpha;
    unsigned char *buffer;

    buffer = decode_png(input, length, &width, &height, &has_alpha);
    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, has_alpha);
    mem_free(buffer);

    return err;
}

static cerr model3dtx_add_texture_at(model3dtx *txm, enum shader_vars var, const char *name)
{
    int width = 0, height = 0, has_alpha = 0;
    unsigned char *buffer = fetch_png(name, &width, &height, &has_alpha);

    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, has_alpha);
    mem_free(buffer);

    return err;
}

static void model3dtx_add_fake_emission(model3dtx *txm)
{
    model3d *model = txm->model;
    uint8_t fake_emission[4] = { 0, 0, 0, 255 };

    shader_prog_use(model->prog);
    cerr err = load_gl_texture_buffer(model->prog, fake_emission, 1, 1, true, UNIFORM_EMISSION_MAP,
                                      txm->emission);
    shader_prog_done(model->prog);

    warn_on(IS_CERR(err), "%s failed: %d\n", __func__, CERR_CODE(err));
}

static void model3dtx_add_fake_sobel(model3dtx *txm)
{
    model3d *model = txm->model;
    uint8_t fake_sobel[4] = { 255, 255, 255, 255 };

    shader_prog_use(model->prog);
    cerr err = load_gl_texture_buffer(model->prog, fake_sobel, 1, 1, true, UNIFORM_SOBEL_TEX,
                                      txm->sobel);
    shader_prog_done(model->prog);

    warn_on(IS_CERR(err), "%s failed: %d\n", __func__, CERR_CODE(err));
}

static bool model3dtx_tex_is_ext(model3dtx *txm)
{
    return txm->texture != &txm->_texture;
}

static void model3dtx_drop(struct ref *ref)
{
    model3dtx *txm = container_of(ref, model3dtx, ref);
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

static cerr model3dtx_make(struct ref *ref, void *_opts)
{
    rc_init_opts(model3dtx) *opts = _opts;

    if (!opts->model)
        return CERR_INVALID_ARGUMENTS;

    model3dtx *txm  = container_of(ref, model3dtx, ref);
    txm->texture    = &txm->_texture;
    txm->normals    = &txm->_normals;
    txm->emission   = &txm->_emission;
    txm->sobel      = &txm->_sobel;
    list_init(&txm->entities);
    list_init(&txm->entry);

    txm->model = ref_get(opts->model);

    cerr err = CERR_INVALID_ARGUMENTS;

    if (opts->tex) {
        if (opts->texture_buffer || opts->texture_file_name)
            goto drop_txm;

        txm->texture = opts->tex;
    } else if (opts->texture_buffer) {
        if (opts->buffers_png)
            err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_MODEL_TEX, opts->texture_buffer,
                                                        opts->texture_size);
        else
            err = model3dtx_add_texture_from_buffer(txm, UNIFORM_MODEL_TEX, opts->texture_buffer,
                                                    opts->texture_width, opts->texture_height,
                                                    opts->texture_has_alpha);
        if (IS_CERR(err))
            goto drop_txm;
    } else if (opts->texture_file_name) {
        err = model3dtx_add_texture_at(txm, UNIFORM_MODEL_TEX, opts->texture_file_name);
        if (IS_CERR(err))
            goto drop_txm;
    }

    if (opts->normal_buffer) {
        if (opts->normal_file_name)
            goto drop_txm;

        if (opts->buffers_png)
            err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_NORMAL_MAP, opts->normal_buffer,
                                                        opts->normal_size);
        else
            err = model3dtx_add_texture_from_buffer(txm, UNIFORM_NORMAL_MAP, opts->normal_buffer,
                                                    opts->normal_width, opts->normal_height, false);
        if (IS_CERR(err))
            goto drop_txm;
    } else if (opts->normal_file_name) {
        err = model3dtx_add_texture_at(txm, UNIFORM_NORMAL_MAP, opts->normal_file_name);
        if (IS_CERR(err))
            goto drop_txm;
    }

    if (opts->emission_buffer) {
        if (opts->emission_file_name)
            goto drop_txm;

        if (opts->buffers_png)
            err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_EMISSION_MAP, opts->emission_buffer,
                                                        opts->emission_size);
        else
            err = model3dtx_add_texture_from_buffer(txm, UNIFORM_EMISSION_MAP, opts->emission_buffer,
                                                    opts->emission_width, opts->emission_height, false);
        if (IS_CERR(err))
            goto drop_txm;
    } else if (opts->emission_file_name) {
        err = model3dtx_add_texture_at(txm, UNIFORM_EMISSION_MAP, opts->emission_file_name);

        if (IS_CERR(err))
            goto drop_txm;
    } else {
        model3dtx_add_fake_emission(txm);
    }

    model3dtx_add_fake_sobel(txm);

    txm->roughness = 0.65;
    txm->metallic = 0.45;

    return CERR_OK;

drop_txm:
    model3dtx_drop(ref);

    return err;
}

DEFINE_REFCLASS2(model3dtx);

DEFINE_CLEANUP(model3dtx, if (*p) ref_put(*p))

void model3dtx_set_texture(model3dtx *txm, enum shader_vars var, texture_t *tex)
{
    struct shader_prog *prog = txm->model->prog;
    texture_t **targets[] = { &txm->texture, &txm->normals, &txm->emission, &txm->sobel };
    int slot = shader_get_texture_slot(prog, var);

    if (slot < 0) {
        dbg("program '%s' doesn't have texture %s or it's not a texture\n", shader_name(prog),
            shader_get_var_name(var));
        return;
    }

    if (slot >= array_size(targets))
        return;

    *targets[slot] = tex;
}

cres(int) model3d_set_name(model3d *m, const char *fmt, ...)
{
    va_list ap;

    mem_free(m->name);
    va_start(ap, fmt);
    cres(int) res = mem_vasprintf(&m->name, fmt, ap);
    va_end(ap);

    return res;
}

static void model3d_calc_aabb(model3d *m, float *vx, size_t vxsz)
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

float model3d_aabb_X(model3d *m)
{
    return fabs(m->aabb[1] - m->aabb[0]);
}

float model3d_aabb_Y(model3d *m)
{
    return fabs(m->aabb[3] - m->aabb[2]);
}

float model3d_aabb_Z(model3d *m)
{
    return fabs(m->aabb[5] - m->aabb[4]);
}

void model3d_aabb_center(model3d *m, vec3 center)
{
    vec3 minv = { m->aabb[0], m->aabb[2], m->aabb[4] };
    vec3 maxv = { m->aabb[1], m->aabb[3], m->aabb[5] };

    vec3_sub(center, maxv, minv);
    vec3_scale(center, center, 0.5);
}

int model3d_add_skinning(model3d *m, unsigned char *joints, size_t jointssz,
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

static void model3d_set_lod(model3d *m, unsigned int lod)
{
    if (lod >= m->nr_lods)
        lod = max(0, m->nr_lods - 1);

    if (lod == m->cur_lod)
        return;

    buffer_bind(&m->index[lod], -1);
    m->cur_lod = lod;
}

static void model3d_prepare(model3d *m, struct shader_prog *p)
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

void model3dtx_prepare(model3dtx *txm, struct shader_prog *p)
{
    model3d *m = txm->model;

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

static void model3dtx_draw(renderer_t *r, model3dtx *txm)
{
    model3d *m = txm->model;

    /* GL_UNSIGNED_SHORT == typeof *indices */
    renderer_draw(r, m->draw_type, m->nr_faces[m->cur_lod], DT_USHORT);
}

static void model3d_done(model3d *m, struct shader_prog *p)
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

void model3dtx_done(model3dtx *txm, struct shader_prog *p)
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

struct animation *animation_new(model3d *model, const char *name, unsigned int nr_channels)
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
                   entity3d *focus, int width, int height, int cascade,
                   unsigned long *count)
{
    entity3d *e;
    struct shader_prog *prog = NULL;
    model3d *model;
    model3dtx *txmodel;
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

            shader_var_blocks_update(prog);
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

float entity3d_aabb_X(entity3d *e)
{
    return model3d_aabb_X(e->txmodel->model) * e->scale;
}

float entity3d_aabb_Y(entity3d *e)
{
    return model3d_aabb_Y(e->txmodel->model) * e->scale;
}

float entity3d_aabb_Z(entity3d *e)
{
    return model3d_aabb_Z(e->txmodel->model) * e->scale;
}

void entity3d_aabb_update(entity3d *e)
{
    model3d *m = e->txmodel->model;
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

void entity3d_aabb_min(entity3d *e, vec3 min)
{
    min[0] = e->aabb[0];
    min[1] = e->aabb[2];
    min[2] = e->aabb[4];
}

void entity3d_aabb_max(entity3d *e, vec3 max)
{
    max[0] = e->aabb[1];
    max[1] = e->aabb[3];
    max[2] = e->aabb[5];
}

void entity3d_aabb_center(entity3d *e, vec3 center)
{
    vec3 minv = { e->txmodel->model->aabb[0], e->txmodel->model->aabb[2], e->txmodel->model->aabb[4] };
    // center[0] = entity3d_aabb_X(e) + e->dx;
    // center[1] = entity3d_aabb_Y(e) + e->dy;
    // center[2] = entity3d_aabb_Z(e) + e->dz;
    model3d_aabb_center(e->txmodel->model, center);
    vec3_add(center, center, minv);
}

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

static void channel_transform(entity3d *e, struct channel *chan, float time)
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

static void channels_transform(entity3d *e, struct animation *an, float time)
{
    int ch;

    for (ch = 0; ch < an->nr_channels; ch++)
        channel_transform(e, &an->channels[ch], time);
}

static void one_joint_transform(entity3d *e, int joint, int parent)
{
    model3d *model = e->txmodel->model;
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

void animation_start(entity3d *e, struct scene *scene, int ani)
{
    model3d *model = e->txmodel->model;
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

int animation_by_name(model3d *m, const char *name)
{
    int i;

    for (i = 0; i < m->anis.da.nr_el; i++)
        if (!strcmp(name, m->anis.x[i].name))
            return i;
    return -1;
}

static struct queued_animation *ani_current(entity3d *e)
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

static void animation_next(entity3d *e, struct scene *s)
{
    struct queued_animation *qa;

    if (!e->aniq.da.nr_el || e->animation < 0) {
        model3d *model = e->txmodel->model;
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

void animation_set_end_callback(entity3d *e, void (*end)(struct scene *, void *), void *priv)
{
    int nr_qas = darray_count(e->aniq);

    if (!nr_qas)
        return;

    e->aniq.x[nr_qas - 1].end = end;
    e->aniq.x[nr_qas - 1].end_priv = priv;
}

void animation_set_speed(entity3d *e, float speed)
{
    struct queued_animation *qa = ani_current(e);

    if (!qa)
        return;

    qa->speed = speed;
}

void animation_push_by_name(entity3d *e, struct scene *s, const char *name,
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

static void animated_update(entity3d *e, struct scene *s)
{
    double time = clap_get_current_time(s->clap_ctx);
    model3d *model = e->txmodel->model;
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
static bool needs_update(entity3d *e)
{
    if (e->updated) {
        e->updated = 0;
        return true;
    }
    return false;
}

static int default_update(entity3d *e, void *data)
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

void entity3d_reset(entity3d *e)
{
    default_update(e, NULL);
}

static cerr entity3d_make(struct ref *ref, void *_opts)
{
    rc_init_opts(entity3d) *opts = _opts;
    entity3d *e = container_of(ref, entity3d, ref);

    darray_init(e->aniq);
    e->animation = -1;
    e->light_idx = -1;
    e->scale     = 1.0;
    e->visible   = 1;
    e->update    = default_update;
    e->mx        = mx_new();
    if (!e->mx)
        return CERR_NOMEM;

    model3d *model = opts->txmodel->model;
    e->txmodel = ref_get(opts->txmodel);
    entity3d_aabb_update(e);
    if (model->anis.da.nr_el) {
        e->joints = mem_alloc(sizeof(*e->joints), .nr = model->nr_joints, .fatal_fail = 1);
        e->joint_transforms = mem_alloc(sizeof(mat4x4), .nr = model->nr_joints, .fatal_fail = 1);
    }

    return CERR_OK;
}

static void entity3d_drop(struct ref *ref)
{
    entity3d *e = container_of(ref, entity3d, ref);
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

DEFINE_REFCLASS2(entity3d);

/* XXX: static inline? via macro? */
void entity3d_put(entity3d *e)
{
    ref_put(e);
}

void entity3d_update(entity3d *e, void *data)
{
    if (e->update)
        e->update(e, data);
}

void entity3d_add_physics(entity3d *e, struct phys *phys, double mass, int class,
                          int type, double geom_off, double geom_radius, double geom_length)
{
    e->phys_body = phys_body_new(phys, e, class, geom_radius, geom_off, type, mass);
}

void entity3d_visible(entity3d *e, unsigned int visible)
{
    e->visible = visible;
}

void entity3d_rotate_X(entity3d *e, float rx)
{
    e->rx = rx;
    e->updated++;
}

void entity3d_rotate_Y(entity3d *e, float ry)
{
    e->ry = ry;
    e->updated++;
}

void entity3d_rotate_Z(entity3d *e, float rz)
{
    e->rz = rz;
    e->updated++;
}

void entity3d_scale(entity3d *e, float scale)
{
    e->scale = scale;
    e->updated++;
}

void entity3d_position(entity3d *e, vec3 pos)
{
    e->updated++;
    vec3_dup(e->pos, pos);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, e->pos);
}

void entity3d_move(entity3d *e, vec3 off)
{
    e->updated++;
    vec3_add(e->pos, e->pos, off);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, e->pos);
}

void entity3d_color(entity3d *e, int color_pt, vec4 color)
{
    switch (color_pt) {
        case COLOR_PT_ALPHA:
        case COLOR_PT_ALL:
            vec4_dup(e->color, color);
        case COLOR_PT_NONE:
            e->color_pt = color_pt;
            break;
        default:
            break;
    }
}

void model3dtx_add_entity(model3dtx *txm, entity3d *e)
{
    list_append(&txm->entities, &e->entry);
}

entity3d *instantiate_entity(model3dtx *txm, struct instantiator *instor,
                             bool randomize_yrot, float randomize_scale, struct scene *scene)
{
    entity3d *e = ref_new(entity3d, .txmodel = txm);
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
    entity3d        *entity;
    struct list     entry;
};

static void debug_draw_drop(struct ref *ref)
{
    struct debug_draw *dd = container_of(ref, struct debug_draw, ref);

    ref_put(dd->entity);
}
cresp_struct_ret(debug_draw);
DEFINE_REFCLASS(debug_draw);
DEFINE_REFCLASS_INIT_OPTIONS(debug_draw);

struct debug_draw *__debug_draw_new(struct scene *scene, float *vx, size_t vxsz,
                                    unsigned short *idx, size_t idxsz, float *tx, mat4x4 *rot)
{
    struct shader_prog *p;
    struct debug_draw *dd;
    model3dtx *txm;
    model3d *m;

    p = shader_prog_find(&scene->shaders, "debug");
    CHECK(dd = ref_new(debug_draw));
    cresp(model3d) res = ref_new_checked(model3d,
                                         .name  = "debug",
                                         .prog  = ref_pass(p),
                                         .vx    = vx,
                                         .vxsz  = vxsz,
                                         .idx   = idx,
                                         .idxsz = idxsz,
                                         .tx    = tx,
                                         .txsz  = vxsz / 3 * 2);
    if (IS_CERR(res)) {
        err_cerr(res, "can't create debug model3d\n");
        return NULL;
    }

    m = res.val;
    m->depth_testing = false;
    m->draw_type = DRAW_TYPE_LINES;

    CHECK(txm = ref_new(model3dtx, .model = ref_pass(m)));
    list_init(&txm->entities);
    mq_add_model(&scene->debug_mq, txm);
    CHECK(dd->entity = ref_new(entity3d, .txmodel = txm));
    model3dtx_add_entity(txm, dd->entity);
    entity3d_visible(dd->entity, 1);
    dd->entity->update = NULL;
    entity3d_color(dd->entity, COLOR_PT_ALL, (vec4){ 1, 0, 0, 1 });
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
    model3dtx *txmodel;
    entity3d *ent;

    while (!list_empty(&mq->txmodels)) {
        bool done = false;

        txmodel = list_first_entry(&mq->txmodels, model3dtx, entry);

        do {
            if (list_empty(&txmodel->entities))
                break;

            ent = list_first_entry(&txmodel->entities, entity3d, entry);
            if (ent == list_last_entry(&txmodel->entities, entity3d, entry)) {
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

void mq_for_each(struct mq *mq, void (*cb)(entity3d *, void *), void *data)
{
    model3dtx *txmodel;
    entity3d *ent, *itent;

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

model3dtx *mq_model_first(struct mq *mq)
{
    return list_first_entry(&mq->txmodels, model3dtx, entry);
}

model3dtx *mq_model_last(struct mq *mq)
{
    return list_last_entry(&mq->txmodels, model3dtx, entry);
}

void mq_add_model(struct mq *mq, model3dtx *txmodel)
{
    txmodel = ref_pass(txmodel);
    list_append(&mq->txmodels, &txmodel->entry);
}

void mq_add_model_tail(struct mq *mq, model3dtx *txmodel)
{
    txmodel = ref_pass(txmodel);
    list_prepend(&mq->txmodels, &txmodel->entry);
}

model3dtx *mq_nonempty_txm_next(struct mq *mq, model3dtx *txm, bool fwd)
{
    model3dtx *first_txm = mq_model_first(mq);
    model3dtx *last_txm = mq_model_last(mq);
    model3dtx *next_txm;

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
