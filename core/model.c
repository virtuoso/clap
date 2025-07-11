// SPDX-License-Identifier: Apache-2.0
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
#include "util.h"
#include "object.h"
#include "mesh.h"
#include "model.h"
#include "pngloader.h"
#include "physics.h"
#include "shader.h"
#include "scene.h"
#include "sound.h"
#include "ui-debug.h"

/****************************************************************************
 * model3d
 * the actual rendered model
 ****************************************************************************/

static void model3d_lods_from_mesh(model3d *m, struct mesh *mesh)
{
    unsigned short *lod = NULL;
    size_t nr_idx, prev_nr_idx = mesh_nr_idx(mesh);
    int level;

    for (level = 0; level < LOD_MAX - 1; level++) {
        nr_idx = mesh_idx_to_lod(mesh, level, &lod, &m->lod_errors[m->nr_lods]);
        /* Sloppy LODs are not useful at the moment, don't waste space on them */
        if (nr_idx >= prev_nr_idx || m->lod_errors[m->nr_lods] < 0.0) {
            mem_free(lod);
            break;
        }

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
        if (m->lod_errors[m->nr_lods] > 0.0)
            m->lod_max++;
        m->nr_lods++;
    }
}

static int model3d_validate_lod(model3d *m, int lod)
{
    return clamp(lod, m->lod_min, m->lod_max);
}

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
    m->skip_aabb = opts->skip_aabb;
    if (opts->mesh) {
        memcpy(m->aabb, opts->mesh->aabb, sizeof(m->aabb));
    } else if (!m->skip_aabb) {
        vertex_array_aabb_calc(m->aabb, vx, vxsz);
        if (opts->fix_origin)
            vertex_array_fix_origin(vx, vxsz, m->aabb);
    }
    m->collision_vx = memdup(vx, vxsz);
    m->collision_vxsz = vxsz;
    m->collision_idx = memdup(idx, idxsz);
    m->collision_idxsz = idxsz;
    darray_init(m->anis);
    sfx_container_init(&m->sfxc);

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
    ref_put(m->prog);

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
    sfx_container_clearout(&m->sfxc);
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
    texture_format color_type = has_alpha ? TEX_FMT_RGBA8 : TEX_FMT_RGB8;
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
    texture_t *targets[] = { txm->texture, txm->normals, txm->emission, txm->sobel, txm->shadow, txm->lut };
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
    if (txm->shadow)
        texture_deinit(txm->shadow);
    if (txm->lut)
        texture_deinit(txm->lut);
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
    txm->shadow     = &txm->_shadow;
    txm->lut        = &txm->_lut;
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
    }

    txm->mat.roughness = opts->roughness;
    txm->mat.metallic = opts->metallic;

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
    texture_t **targets[] = { &txm->texture, &txm->normals, &txm->emission, &txm->sobel, &txm->shadow, &txm->lut };
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
    aabb_center(m->aabb, center);
}

#ifndef CONFIG_FINAL
static void entity3d_aabb_draw(entity3d *e, bool entity, bool model)
{
    if (entity) {
        struct message dm = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw){
                .color      = { 1.0, 1.0, 0.0, 1.0 },
                .thickness  = 1.0,
                .shape      = DEBUG_DRAW_AABB,
                .v0         = { e->aabb[0], e->aabb[2], e->aabb[4] },
                .v1         = { e->aabb[1], e->aabb[3], e->aabb[5] },
            }
        };
        message_send(&dm);
    }

    if (model) {
        model3d *m = e->txmodel->model;
        vec4 v0 = { m->aabb[0], m->aabb[2], m->aabb[4], 1.0 };
        vec4 v1 = { m->aabb[1], m->aabb[3], m->aabb[5], 1.0 };
        mat4x4_mul_vec4_post(v0, e->mx, v0);
        mat4x4_mul_vec4_post(v1, e->mx, v1);

        struct message dm = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw){
                .color      = { 1.0, 0.0, 0.0, 1.0 },
                .thickness  = 1.0,
                .shape      = DEBUG_DRAW_AABB,
                .v0         = { v0[0], v0[1], v0[2] },
                .v1         = { v1[0], v1[1], v1[2] },
            }
        };
        message_send(&dm);
    }
}

static void entity3d_debug(entity3d *e)
{
    struct message dm = {
        .type       = MT_DEBUG_DRAW,
        .debug_draw = (struct message_debug_draw) {
            .shape  = DEBUG_DRAW_TEXT,
            .color  = { 1.0, 1.0, 0.0, 1.0 },
        }
    };
    vec3_dup(dm.debug_draw.v0, e->aabb_center);

    const float *pos = transform_pos(&e->xform, NULL);
    float angles[3];
    transform_rotation(&e->xform, angles, true);
    CRES_RET(
        mem_asprintf(
            &dm.debug_draw.text,
            "%s\npos: %.02f,%.02f,%.02f\nrx: %.02f ry: %.02f rz: %.02f\nscale: %.02f\nLOD %d",
            entity_name(e),
            pos[0], pos[1], pos[2],
            angles[0], angles[1], angles[2],
            e->scale,
            e->cur_lod
        ),
        return
    );
    message_send(&dm);
}
#else
static inline void entity3d_aabb_draw(entity3d *e, bool entity, bool model) {}
static inline void entity3d_debug(entity3d *e) {}
#endif /* CONFIG_FINAL */

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

void entity3d_set_lod(entity3d *e, int lod, bool force)
{
    model3d *m = e->txmodel->model;

    if (force) {
        if (lod < 0) {
            e->force_lod = -1;
            lod = 0;
        } else {
            e->force_lod = lod = clamp(lod, 0, m->nr_lods - 1);
        }
    } else if (e->force_lod >= 0) {
        lod = model3d_validate_lod(m, e->force_lod);
    }

    e->cur_lod = model3d_validate_lod(m, lod);
}

static void model3d_prepare(model3d *m, struct shader_prog *p)
{
    vertex_array_bind(&m->vao);
    shader_plug_attribute(p, ATTR_POSITION, &m->vertex);
    shader_plug_attribute(p, ATTR_NORMAL, &m->norm);
    shader_plug_attribute(p, ATTR_TANGENT, &m->tangent);

    if (m->nr_joints) {
        shader_plug_attribute(p, ATTR_JOINTS, &m->vjoints);
        shader_plug_attribute(p, ATTR_WEIGHTS, &m->weights);
    }
}

static void model3dtx_prepare(model3dtx *txm, struct shader_prog *p)
{
    model3d *m = txm->model;

    model3d_prepare(txm->model, p);

    shader_plug_attribute(p, ATTR_TEX, &m->tex);

    shader_plug_texture(p, UNIFORM_MODEL_TEX, txm->texture);
    shader_plug_texture(p, UNIFORM_NORMAL_MAP, txm->normals);
    shader_plug_texture(p, UNIFORM_EMISSION_MAP, txm->emission);
    shader_plug_texture(p, UNIFORM_SOBEL_TEX, txm->sobel);
    shader_plug_texture(p, UNIFORM_SHADOW_MAP, txm->shadow);
    shader_plug_texture(p, UNIFORM_LUT_TEX, txm->lut);
}

static void model3dtx_draw(renderer_t *r, model3dtx *txm, unsigned int lod, unsigned int nr_instances)
{
    model3d *m = txm->model;

    /* GL_UNSIGNED_SHORT == typeof *indices */
    renderer_draw(r, m->draw_type, m->nr_faces[lod], DT_USHORT, nr_instances);
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

    vertex_array_unbind(&m->vao);
}

static void model3dtx_done(model3dtx *txm, struct shader_prog *p)
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

void _models_render(renderer_t *r, struct mq *mq, const models_render_options *opts)
{
    struct camera *camera = opts->camera;
    struct light *light = opts->light;
    struct subview *subview = NULL;
    float near_plane, far_plane;
    struct view *view = NULL;
    mat4x4 *proj = NULL;

    if (camera) {
        view = &camera->view;
        proj = &camera->view.main.proj_mx;
    } else if (light) {
        view = &light->view[0];
        proj = &light->view[0].main.proj_mx;
    }

    if (view) {
        if (opts->cascade >= 0 && opts->cascade < CASCADES_MAX) {
            subview = &view->subview[opts->cascade];
            proj = &subview->proj_mx;
        } else {
            subview = &view->main;
        }
        near_plane = subview->near_plane;
        far_plane = subview->far_plane;
    }

    if (opts->near_plane)
        near_plane = opts->near_plane;
    if (opts->far_plane)
        far_plane = opts->far_plane;

    unsigned long nr_ents = 0, nr_txms = 0, culled = 0;
    render_options *ropts = opts->render_options;
    struct shader_prog *prog = NULL;
    model3dtx *txmodel;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        model3d *model = txmodel->model;
        if (model->skip_shadow && opts->shader_override)
            continue;

        struct shader_prog *model_prog = opts->shader_override ? : model->prog;

        int lod = -1;

        cull_face cull = CULL_FACE_NONE;
        if (model->cull_face)
            cull = opts->shader_override ? CULL_FACE_FRONT : CULL_FACE_BACK;
        renderer_cull_face(r, cull);

        renderer_blend(r, model->alpha_blend, BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA);

        /* TODO: add a separate property for depth test control */
        renderer_depth_test(r, model->depth_testing);

        if (model_prog != prog) {
            if (prog)
                shader_prog_done(prog);

            prog = model_prog;
            shader_prog_use(prog);

            if (ropts) {
                shader_set_var_int(prog, UNIFORM_SHADOW_VSM, ropts->shadow_vsm);
                shader_set_var_int(prog, UNIFORM_SHADOW_OUTLINE, ropts->shadow_outline);
                shader_set_var_float(prog, UNIFORM_SHADOW_OUTLINE_THRESHOLD, ropts->shadow_outline_threshold);
                shader_set_var_int(prog, UNIFORM_USE_MSAA, ropts->shadow_msaa);
                shader_set_var_int(prog, UNIFORM_LAPLACE_KERNEL, ropts->laplace_kernel);
                shader_set_var_int(prog, UNIFORM_USE_HDR, ropts->hdr);
                shader_set_var_int(prog, UNIFORM_USE_SSAO, ropts->ssao);
                shader_set_var_float(prog, UNIFORM_SSAO_RADIUS, ropts->ssao_radius);
                shader_set_var_float(prog, UNIFORM_SSAO_WEIGHT, ropts->ssao_weight);
                shader_set_var_float(prog, UNIFORM_BLOOM_EXPOSURE, ropts->bloom_exposure);
                shader_set_var_float(prog, UNIFORM_BLOOM_OPERATOR, ropts->bloom_operator);
                shader_set_var_float(prog, UNIFORM_LIGHTING_EXPOSURE, ropts->lighting_exposure);
                shader_set_var_float(prog, UNIFORM_LIGHTING_OPERATOR, ropts->lighting_operator);
                shader_set_var_float(prog, UNIFORM_CONTRAST, ropts->contrast);
                shader_set_var_float(prog, UNIFORM_FOG_NEAR, ropts->fog_near);
                shader_set_var_float(prog, UNIFORM_FOG_FAR, ropts->fog_far);
                shader_set_var_ptr(prog, UNIFORM_FOG_COLOR, 1, ropts->fog_color);
            }

            if (opts->width)
                shader_set_var_float(prog, UNIFORM_WIDTH, (float)opts->width);
            if (opts->height)
                shader_set_var_float(prog, UNIFORM_HEIGHT, (float)opts->height);

            if (light) {
                shader_set_var_ptr(prog, UNIFORM_LIGHT_POS, LIGHTS_MAX, light->pos);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_COLOR, LIGHTS_MAX, light->color);
                shader_set_var_ptr(prog, UNIFORM_ATTENUATION, LIGHTS_MAX, light->attenuation);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_DIR, LIGHTS_MAX, light->dir);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_DIRECTIONAL, LIGHTS_MAX, light->is_dir);
                shader_set_var_int(prog, UNIFORM_NR_LIGHTS, light->nr_lights);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_AMBIENT, 1, light->ambient);
                shader_set_var_ptr(prog, UNIFORM_SHADOW_TINT, 1, light->shadow_tint);
                if (shader_has_var(prog, UNIFORM_SHADOW_MVP)) {
                    mat4x4 mvp[CASCADES_MAX];
                    int i;

                    for (i = 0; i < CASCADES_MAX; i++) {
                        struct subview *light_sv = &light->view[0].subview[i];
                        mat4x4_mul(mvp[i], light_sv->proj_mx, light_sv->view_mx);
                    }
                    shader_set_var_ptr(prog, UNIFORM_SHADOW_MVP, CASCADES_MAX, mvp);
                }
                if (light->shadow[0][0]) {
#ifdef CONFIG_GLES
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP, light->shadow[0][0]);
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP1, light->shadow[0][1] ? light->shadow[0][1] : white_pixel());
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP2, light->shadow[0][2] ? light->shadow[0][2] : white_pixel());
                    shader_plug_texture(prog, UNIFORM_SHADOW_MAP3, light->shadow[0][3] ? light->shadow[0][3] : white_pixel());
                    shader_set_var_int(prog, UNIFORM_USE_MSAA, false);
#else
                    if (shader_has_var(prog, UNIFORM_SHADOW_MAP_MS)) {
                        shader_plug_textures_multisample(prog, ropts ? ropts->shadow_msaa : false,
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

            shader_set_var_float(prog, UNIFORM_NEAR_PLANE, near_plane);
            shader_set_var_float(prog, UNIFORM_FAR_PLANE, far_plane);

            if (subview) {
                shader_set_var_ptr(prog, UNIFORM_VIEW, 1, subview->view_mx);
                shader_set_var_ptr(prog, UNIFORM_INVERSE_VIEW, 1, subview->inv_view_mx);
            }

            if (proj)
                shader_set_var_ptr(prog, UNIFORM_PROJ, 1, proj);
        }

        model3dtx_prepare(txmodel, prog);

        shader_set_var_int(prog, UNIFORM_USE_NORMALS, texture_loaded(txmodel->normals));

        shader_set_var_float(prog, UNIFORM_ROUGHNESS, txmodel->mat.roughness);
        shader_set_var_int(prog, UNIFORM_ROUGHNESS_OCT, txmodel->mat.roughness_oct);
        if (txmodel->mat.roughness_oct > 0) {
            shader_set_var_float(prog, UNIFORM_ROUGHNESS_SCALE, txmodel->mat.roughness_scale);
            shader_set_var_float(prog, UNIFORM_ROUGHNESS_CEIL, txmodel->mat.roughness_ceil);
            shader_set_var_float(prog, UNIFORM_ROUGHNESS_AMP, txmodel->mat.roughness_amp);
        }

        shader_set_var_float(prog, UNIFORM_METALLIC, txmodel->mat.metallic);
        shader_set_var_int(prog, UNIFORM_METALLIC_OCT, txmodel->mat.metallic_oct);
        shader_set_var_int(prog, UNIFORM_METALLIC_MODE, txmodel->mat.metallic_mode);
        shader_set_var_int(prog, UNIFORM_SHARED_SCALE, txmodel->mat.shared_scale);
        if (txmodel->mat.metallic_oct > 0) {
            shader_set_var_float(prog, UNIFORM_METALLIC_SCALE, txmodel->mat.metallic_scale);
            shader_set_var_float(prog, UNIFORM_METALLIC_CEIL, txmodel->mat.metallic_ceil);
            shader_set_var_float(prog, UNIFORM_METALLIC_AMP, txmodel->mat.metallic_amp);
        }

        unsigned int nr_characters = 0;
        entity3d *e;

        list_for_each_entry (e, &txmodel->entities, entry) {
            if (!entity3d_matches(e, ENTITY3D_ALIVE))
                continue;

            if (unlikely(ropts && ropts->aabb_draws_enabled && !model->skip_aabb))
                entity3d_aabb_draw(e, true, true);

            if (!e->visible)
                continue;

            if (!e->skip_culling &&
                view && !view_entity_in_frustum(view, e)) {
                culled++;
                continue;
            }

            if (camera) {
                if (e->force_lod >= 0) {
                    e->cur_lod = e->force_lod;
                } else {
                    const float *pos = transform_pos(&camera->xform, NULL);

                    /* only apply LOD when the camera is outside the AABB */
                    if (!aabb_point_is_inside(e->aabb, pos)) {
                        vec3 dist;

                        vec3_sub(dist, e->aabb_center, pos);
                        float side = entity3d_aabb_avg_edge(e);
                        /* microoptimization: squaring things is faster than sqrt() */
                        float scale = fabsf(vec3_mul_inner(dist, dist) - side * side) / 3600.0;
                        entity3d_set_lod(e, (int)scale, false);
                    }
                }
            }
            if (e->cur_lod != lod) {
                if (lod >= 0)
                    buffer_unbind(&model->index[lod], -1);
                buffer_bind(&model->index[e->cur_lod], -1);
                lod = e->cur_lod;
            }

            if (fabsf(e->bloom_intensity) > 1e-3)
                shader_set_var_float(prog, UNIFORM_BLOOM_INTENSITY, e->bloom_intensity);
            else if (ropts)
                shader_set_var_float(prog, UNIFORM_BLOOM_INTENSITY, ropts->bloom_intensity);

            if (fabsf(e->bloom_threshold) > 1e-3)
                shader_set_var_float(prog, UNIFORM_BLOOM_THRESHOLD, e->bloom_threshold);
            else if (ropts)
                shader_set_var_float(prog, UNIFORM_BLOOM_THRESHOLD, ropts->bloom_threshold);

            shader_set_var_int(prog, UNIFORM_OUTLINE_EXCLUDE, e->outline_exclude);
            if (e->priv && mq->nr_characters) {  /* e->priv now points to character */
                nr_characters++;
                shader_set_var_int(prog, UNIFORM_SOBEL_SOLID, 1);
                shader_set_var_float(prog, UNIFORM_SOBEL_SOLID_ID, (float)nr_characters / (float)mq->nr_characters);
            } else {
                shader_set_var_int(prog, UNIFORM_SOBEL_SOLID, 0);
            }
            shader_set_var_ptr(prog, UNIFORM_IN_COLOR, 1, e->color);
            shader_set_var_int(prog, UNIFORM_COLOR_PASSTHROUGH, e->color_pt);

            if (model->nr_joints && model->anis.da.nr_el) {
                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 1);
                shader_set_var_ptr(prog, UNIFORM_JOINT_TRANSFORMS, model->nr_joints, e->joint_transforms);
            } else {
                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 0);
            }

            shader_set_var_ptr(prog, UNIFORM_TRANS, 1, e->mx);

            if (opts->ssao_state)
                ssao_upload(opts->ssao_state, prog, opts->width, opts->height);

            unsigned int nr_instances = 1;
            if (e->particles) {
                particle_system_upload(e->particles, prog);
                nr_instances = particle_system_count(e->particles);
            }

            shader_var_blocks_update(prog);
            model3dtx_draw(r, txmodel, e->cur_lod, nr_instances);
            nr_ents++;
        }

        if (lod >= 0)
            buffer_unbind(&model->index[lod], -1);

        model3dtx_done(txmodel, prog);
        nr_txms++;
    }

    if (opts->entity_count)
        *opts->entity_count = nr_ents;
    if (opts->txm_count)
        *opts->txm_count = nr_txms;
    if (opts->culled_count)
        *opts->culled_count = culled;
    if (prog)
        shader_prog_done(prog);
}

/****************************************************************************
 * entity3d
 * instance of the model3d
 ****************************************************************************/

bool entity3d_matches(entity3d *e, entity3d_flags flags)
{
    return !!(e->flags & flags);
}

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

    if (m->skip_aabb)
        return;

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

    e->aabb[0] = e->aabb[2] = e->aabb[4] = INFINITY;
    e->aabb[1] = e->aabb[3] = e->aabb[5] = -INFINITY;
    for (i = 0; i < array_size(corners); i++) {
        mat4x4_mul_vec4_post(v, e->mx, corners[i]);
        e->aabb[0] = min(v[0], e->aabb[0]);
        e->aabb[1] = max(v[0], e->aabb[1]);
        e->aabb[2] = min(v[1], e->aabb[2]);
        e->aabb[3] = max(v[1], e->aabb[3]);
        e->aabb[4] = min(v[2], e->aabb[4]);
        e->aabb[5] = max(v[2], e->aabb[5]);
    }

    aabb_center(e->aabb, e->aabb_center);
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

float entity3d_aabb_avg_edge(entity3d *e)
{
    return cbrtf(entity3d_aabb_X(e) * entity3d_aabb_Y(e) * entity3d_aabb_Z(e));
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

    /*
     * Don't crash on broken animations (channel referenced a non-existent
     * joint, see warning in gltf_instantiate_one()).
     */
    if (!chan->nr || !chan->data || !chan->time)
        return;

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

        if (!animation_push_by_name(e, s, "idle", true, true))
            return;

        /* randomize phase, should probably be in instantiate instead */
        qa = ani_current(e);
        an = &model->anis.x[qa->animation];
        e->ani_time = clap_get_current_time(s->clap_ctx) +  an->time_end * drand48();
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
    qa->sfx_state = 0;
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

void animation_set_frame_callback(entity3d *e, frame_fn cb)
{
    int nr_qas = darray_count(e->aniq);

    if (!nr_qas)
        return;

    e->aniq.x[nr_qas - 1].frame_cb = cb;
}

void animation_set_speed(entity3d *e, struct scene *s, float speed)
{
    struct queued_animation *qa = ani_current(e);

    if (!qa)
        return;

    double time = clap_get_current_time(s->clap_ctx);
    struct animation *an = &e->txmodel->model->anis.x[qa->animation];

    qa->speed = speed;

    double frame_time = (time - e->ani_time) * qa->speed;
    if (frame_time >= an->time_end) {
        e->ani_time = time;
        qa->sfx_state = 0;
    }
}

bool animation_push_by_name(entity3d *e, struct scene *s, const char *name,
                            bool clear, bool repeat)
{
    struct queued_animation *qa;

    if (clear) {
        struct queued_animation _qa;

        qa = ani_current(e);
        if (qa)
            memcpy(&_qa, qa, sizeof(_qa));

        darray_clearout(e->aniq);

        if (qa)
            animation_end(&_qa, s);
    }

    int id = animation_by_name(e->txmodel->model, name);
    if (id < 0) {
        if (clear)
            e->animation = -1;
        return false;
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

    return true;
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
    if (!qa)
        return;

    an = &model->anis.x[qa->animation];

    double frame_time = (time - e->ani_time) * qa->speed;
    if (frame_time == 0.0)
        qa->sfx_state = 0;

    channels_transform(e, an, frame_time);
    one_joint_transform(e, 0, -1);

    if (qa->frame_cb)
        qa->frame_cb(qa, e, s, frame_time / an->time_end);
    if (an->frame_sfx)
        an->frame_sfx(qa, e, s, frame_time / an->time_end);

    if (frame_time >= an->time_end)
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
        mat4x4_identity(e->mx);
        transform_translate_mat4x4(&e->xform, e->mx);
        transform_rotate_mat4x4(&e->xform, e->mx);
        transform_clear_updated(&e->xform);

        mat4x4 tr_no_scale;
        mat4x4_dup(tr_no_scale, e->mx);

        mat4x4_scale_aniso(e->mx, e->mx, e->scale, e->scale, e->scale);

        entity3d_aabb_update(e);

        if (e->phys_body)
            phys_body_rotate_mat4x4(e->phys_body, tr_no_scale);

        if (scene && e->light_idx >= 0) {
            vec3 pos;
            transform_pos(&e->xform, pos);
            vec3_add(pos, pos, e->light_off);
            light_set_pos(&scene->light, e->light_idx, pos);
        }
    }

    if (!scene)
        return 0;

    /*
     * Find the biggest bounding volume that contains camera or the character
     */
    struct camera *cam = scene->camera;
    if ((aabb_point_is_inside(e->aabb, transform_pos(&cam->xform, NULL)) ||
         aabb_point_is_inside(e->aabb, transform_pos(&scene->control->xform, NULL))) &&
         e != scene->control) {
        float volume = entity3d_aabb_X(e) * entity3d_aabb_Y(e) * entity3d_aabb_Z(e);

        if (!cam->bv || volume > cam->bv_volume) {
            cam->bv = e;
            cam->bv_volume = volume;
        }
    }

    if (entity_animated(e))
        animated_update(e, scene);
    if (e->phys_body)
        phys_debug_draw(scene, e->phys_body);
    if (clap_get_render_options(scene->clap_ctx)->overlay_draws_enabled)
        entity3d_debug(e);

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
    e->force_lod = -1;
    e->scale     = 1.0;
    e->visible   = 1;
    e->update    = default_update;

    model3d *model = opts->txmodel->model;
    e->txmodel = ref_get(opts->txmodel);
    entity3d_aabb_update(e);
    if (model->anis.da.nr_el) {
        e->joints = mem_alloc(sizeof(*e->joints), .nr = model->nr_joints, .fatal_fail = 1);
        e->joint_transforms = mem_alloc(sizeof(mat4x4), .nr = model->nr_joints, .fatal_fail = 1);
    }

    list_append(&e->txmodel->entities, &e->entry);
    e->flags |= ENTITY3D_ALIVE;

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
    mem_free(e->name);
}

DEFINE_REFCLASS2(entity3d);

void entity3d_delete(entity3d *e)
{
    e->flags &= ENTITY3D_DEAD;
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

void entity3d_rotate(entity3d *e, float rx, float ry, float rz)
{
    transform_set_angles(&e->xform, (float[3]){ rx, ry, rz }, false);
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
    transform_set_pos(&e->xform, pos);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, pos);
}

void entity3d_move(entity3d *e, vec3 off)
{
    e->updated++;
    const float *pos = transform_move(&e->xform, off);
    if (e->phys_body)
        phys_body_set_position(e->phys_body, pos);
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

entity3d *instantiate_entity(model3dtx *txm, struct instantiator *instor,
                             bool randomize_yrot, float randomize_scale, struct scene *scene)
{
    entity3d *e = ref_new(entity3d, .txmodel = txm);
    entity3d_position(e, (vec3){ instor->dx, instor->dy, instor->dz });
    if (randomize_yrot)
        entity3d_rotate(e, 0, drand48() * M_PI * 2, 0);
    if (randomize_scale)
        entity3d_scale(e, 1 + randomize_scale * (1 - drand48() * 2));
    default_update(e, scene);
    return e;
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

void mq_for_each_matching(struct mq *mq, entity3d_flags flags, void (*cb)(entity3d *, void *), void *data)
{
    model3dtx *txmodel;
    entity3d *ent, *itent;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            if (entity3d_matches(ent, flags))
                cb(ent, data);
        }
    }
}

void mq_for_each(struct mq *mq, void (*cb)(entity3d *, void *), void *data)
{
    mq_for_each_matching(mq, ENTITY3D_ANY, cb, data);
}

void mq_update(struct mq *mq)
{
    mq_for_each_matching(mq, ENTITY3D_ALIVE, entity3d_update, mq->priv);
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
