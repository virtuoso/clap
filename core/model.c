// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "shader_constants.h"
#include "clap.h"
#include "interp.h"
#include "render.h"
#include "util.h"
#include "object.h"
#include "mesh.h"
#include "model.h"
#include "noise.h"
#include "primitives.h"
#include "character.h"
#include "ui.h"
#include "pngloader.h"
#include "physics.h"
#include "shader.h"
#include "scene.h"
#include "sound.h"

/****************************************************************************
 * model3d
 * the actual rendered model
 ****************************************************************************/

static void model3d_lods_upload(model3d *m, struct mesh *mesh, struct mesh_lods *lods)
{
    unsigned int level;

    for (level = 0; level < lods->nr; level++) {
        size_t nr_idx = lods->nr_idx[level];

        cerr err = buffer_init(&m->index[m->nr_lods],
                               .renderer   = shader_prog_renderer(m->prog),
                               .type       = BUF_ELEMENT_ARRAY,
                               .usage      = BUF_STATIC,
                               .comp_type  = DT_USHORT,
                               .data       = lods->idx[level],
                               .size       = nr_idx * mesh_idx_stride(mesh));

        if (IS_CERR(err))
            continue;

        buffer_set_name(&m->index[m->nr_lods], "%s:index%d", m->name, m->nr_lods);

        dbg("lod%u for '%s' idx: %zd -> %zd\n", level, m->name, mesh_nr_idx(mesh), nr_idx);
        m->nr_faces[m->nr_lods] = nr_idx;
        m->lod_errors[m->nr_lods] = lods->error[level];
        if (m->lod_errors[m->nr_lods] > 0.0)
            m->lod_max++;
        m->nr_lods++;
    }
}

/*
 * Split a skinned model into its rigid pieces. They come from the coarsest
 * level: a collision shape wants the outline, not the detail.
 */
static void model3d_parts_from_lods(model3d *m, struct mesh *mesh, struct mesh_lods *lods)
{
    const unsigned short *idx = lods->nr ? lods->idx[lods->nr - 1] : nullptr;
    size_t nr_idx = lods->nr ? lods->nr_idx[lods->nr - 1] : 0;

    CERR_RET(mesh_parts_by_joint(mesh, idx, sizeof(*idx), nr_idx, &m->parts), warn("'%s': no parts\n", m->name));
}

static int model3d_validate_lod(model3d *m, int lod)
{
    return clamp(lod, m->lod_min, m->lod_max);
}

static cerr model3d_make(struct ref *ref, void *_opts)
{
    rc_init_opts(model3d) *opts = _opts;

    if (!opts->prog || !opts->mesh)
        return CERR_INVALID_ARGUMENTS;

    if (!mesh_nr_vx(opts->mesh) || !mesh_nr_idx(opts->mesh))
        return CERR_INVALID_ARGUMENTS;

    auto r = shader_prog_renderer(opts->prog);
    model3d *m = container_of(ref, model3d, ref);

    m->depth_testing = true;
    m->cull_face     = true;
    m->draw_type     = DRAW_TYPE_TRIANGLES;

    CHECK(m->name = strdup(opts->name));
    m->prog = ref_get(opts->prog);
    m->shader_base = opts->shader_base;
    m->variants = opts->variants;
    m->alpha_blend = false;
    m->skip_aabb = opts->skip_aabb;
    if (!m->skip_aabb) {
        memcpy(m->aabb, opts->mesh->aabb, sizeof(m->aabb));
        if (opts->fix_origin)
            vertex_array_fix_origin(
                mesh_vx(opts->mesh),
                mesh_vx_sz(opts->mesh),
                mesh_vx_stride(opts->mesh),
                m->aabb
            );
    }
    /*
     * A position-only welded copy so the collision/nav trimesh is
     * connected; fall back to a raw copy if the layout is unexpected
     */
    cerr coll_err = mesh_make_collision(opts->mesh, &m->collision_vx, &m->collision_vxsz,
                                        &m->collision_idx, &m->collision_idxsz);
    if (IS_CERR(coll_err)) {
        m->collision_vx = memdup(mesh_vx(opts->mesh), mesh_vx_sz(opts->mesh));
        m->collision_vxsz = mesh_vx_sz(opts->mesh);
        m->collision_idx = memdup(mesh_idx(opts->mesh), mesh_idx_sz(opts->mesh));
        m->collision_idxsz = mesh_idx_sz(opts->mesh);
    }
    darray_init(m->anis);
    darray_init(m->parts);
    anim_lib_init(&m->anim_lib, m);
    sfx_container_init(&m->sfxc);

    cerr err = CERR_OK;

    CERR_RET(shader_prog_use(opts->prog, false), { err = __cerr; goto init_done; });

    CERR_RET(vertex_array_init(&m->vao, r), { err = __cerr; goto shader_done; });
    CERR_RET(
        shader_setup_attributes(opts->prog, m->attr, opts->mesh),
        { err = __cerr; goto vao_done; }
    );

    CERR_RET(
        buffer_init(
            &m->index[0],
            .renderer   = r,
            .type       = BUF_ELEMENT_ARRAY,
            .usage      = BUF_STATIC,
            .comp_type  = DT_USHORT,
            .data       = mesh_idx(opts->mesh),
            .size       = mesh_idx_sz(opts->mesh)
        ),
        { err = __cerr; goto attrs_done; }
    );

    buffer_set_name(&m->index[m->nr_lods], "%s:index%d", m->name, m->nr_lods);

    m->nr_lods++;

    struct mesh_lods own, *lods = opts->lods;
    if (!lods) {
        mesh_lods_build(opts->mesh, &own);
        lods = &own;
    }
    model3d_lods_upload(m, opts->mesh, lods);
    if (opts->parts)    model3d_parts_from_lods(m, opts->mesh, lods);
    if (lods == &own)   mesh_lods_done(&own);

    vertex_array_unbind(&m->vao);
    shader_prog_done(opts->prog, false);

    m->nr_vertices = mesh_nr_vx(opts->mesh);
    m->nr_faces[0] = mesh_nr_idx(opts->mesh);

    return CERR_OK;

attrs_done:
    for (enum shader_vars v = 0; v < ATTR_MAX; v++)
        buffer_deinit(&m->attr[v]);
vao_done:
    vertex_array_unbind(&m->vao);
    vertex_array_done(&m->vao);
shader_done:
    shader_prog_done(opts->prog, false);
init_done:
    ref_put(m->prog);

    free(m->name);
    free(m->collision_vx);
    free(m->collision_idx);

    return err;
}

static void model3d_drop(struct ref *ref)
{
    model3d *m = container_of(ref, model3d, ref);
    int i;

    /* delete gl buffers */
    for (i = 0; i < m->nr_lods; i++)
        buffer_deinit(&m->index[i]);
    for (enum shader_vars v = 0; v < ATTR_MAX; v++)
        buffer_deinit(&m->attr[v]);
    vertex_array_done(&m->vao);
    ref_put(m->prog);
    if (m->shadow_prog) ref_put(m->shadow_prog);
    trace("dropping model '%s'\n", m->name);

    anim_lib_done(&m->anim_lib);
    while (darray_count(m->anis))
        animation_delete(&m->anis.x[0]);
    darray_clearout(m->anis);
    for (i = 0; i < m->nr_joints; i++) {
        darray_clearout(m->joints[i].children);
        mem_free(m->joints[i].name);
    }
    sfx_container_clearout(&m->sfxc);
    mesh_parts_done(&m->parts);
    mem_free(m->joints);
    mem_free(m->collision_vx);
    mem_free(m->collision_idx);
    mem_free(m->degenerate_tri);
    mem_free(m->name);
}

DEFINE_REFCLASS2(model3d);

DEFINE_CLEANUP(model3d, if (*p) ref_put(*p))

static cerr load_texture_buffer(struct shader_prog *p, void *buffer, int width, int height,
                                texture_format color_type, enum shader_vars var, texture_t *tex)
{
    if (!buffer)                    return CERR_INVALID_ARGUMENTS;
    if (!shader_has_var(p, var))    return CERR_OK;

    CERR_RET(
        texture_init(
            tex,
            .renderer     = shader_prog_renderer(p),
            .target       = shader_get_texture_slot(p, var),
            .wrap         = TEX_WRAP_REPEAT,
            .min_filter   = TEX_FLT_NEAREST,
            .mag_filter   = TEX_FLT_NEAREST
        ),
        return __cerr
    );

    return texture_load(tex, color_type, width, height, buffer);
}

#define TEXIDX(_tex)    ((_tex) - ATTR_MAX)

cresp(texture_t) model3dtx_texture(model3dtx *txm, enum shader_vars var)
{
    if (var < ATTR_MAX || var >= UNIFORM_TEX_MAX)   return cresp_error(texture_t, CERR_NOT_FOUND);
    return cresp_val(texture_t, txm->textures[var - ATTR_MAX]);
}

cresp(texture_t) model3dtx_loaded_texture(model3dtx *txm, enum shader_vars var)
{
    auto tex = CRES_RET_T(model3dtx_texture(txm, var), texture_t);
    if (!tex || !texture_loaded(tex))   return cresp_error(texture_t, CERR_TEXTURE_NOT_LOADED);
    return cresp_val(texture_t, tex);
}

shader_variants_t model3dtx_shader_variants(model3dtx *txm)
{
    model3d *model = txm->model;
    /*
     * The attribute-layout half is frozen at model creation (attr[] and
     * the VAO are laid out against it, and the shadow prog matches it);
     * the frag-only axes compose live, mirroring models_render() driving
     * the matching uniforms: models_render() flags a divergence for
     * pipeline_shaders_resolve() to pick a material edit up
     */
    shader_variants_t variants =
        model->variants & (SHADER_NORMALS_BIT | SHADER_SKINNING_BIT);

    if (txm->mat.use_noise_normals != NOISE_NORMALS_NONE || txm->mat.use_noise_emission)
        variants |= SHADER_NOISE_MATERIALS_BIT;
    if (model->alpha_blend) variants |= SHADER_ALPHA_DISCARD_BIT;

    return variants;
}

static cerr model3dtx_add_texture_from_buffer(model3dtx *txm, enum shader_vars var, void *input,
                                              int width, int height, texture_format color_format)
{
    struct shader_prog *prog = txm->model->prog;
    int slot;
    cerr err;

    slot = shader_get_texture_slot(prog, var);
    if (slot < 0)
        return CERR_INVALID_ARGUMENTS;

    auto tex = CRES_RET_CERR(model3dtx_texture(txm, var));

    err = load_texture_buffer(prog, input, width, height, color_format, var, tex);
    dbg("loaded texture%d %d %dx%d\n", slot, (int)texture_id(tex), width, height);

    return err;
}

static cerr_check model3dtx_add_texture_from_png_buffer(model3dtx *txm, enum shader_vars var, void *input, size_t length)
{
    int width, height, has_alpha;
    unsigned char *buffer;

    buffer = decode_png(input, length, &width, &height, &has_alpha);
    texture_format color_format;

    switch (var) {
        case UNIFORM_MODEL_TEX:
        case UNIFORM_EMISSION_MAP:
            color_format = has_alpha ? TEX_FMT_RGBA8_SRGB : TEX_FMT_RGB8_SRGB;
            break;
        default:
            color_format = has_alpha ? TEX_FMT_RGBA8 : TEX_FMT_RGB8;
            break;
    }

    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, color_format);
    mem_free(buffer);

    return err;
}

static cerr model3dtx_add_texture_at(model3dtx *txm, enum shader_vars var, const char *name)
{
    int width = 0, height = 0, has_alpha = 0;
    unsigned char *buffer = fetch_png(name, &width, &height, &has_alpha);

    texture_format color_format = has_alpha ? TEX_FMT_RGBA8 : TEX_FMT_RGB8;
    cerr err = model3dtx_add_texture_from_buffer(txm, var, buffer, width, height, color_format);
    mem_free(buffer);

    return err;
}

static bool model3dtx_tex_is_ext(model3dtx *txm, enum shader_vars var)
{
    switch (var) {
        case UNIFORM_MODEL_TEX:     return txm->textures[TEXIDX(var)] != &txm->_texture;
        case UNIFORM_NORMAL_MAP:    return txm->textures[TEXIDX(var)] != &txm->_normals;
        case UNIFORM_EMISSION_MAP:  return txm->textures[TEXIDX(var)] != &txm->_emission;
        case UNIFORM_NOISE3D_TEX:   return true; /* always externally supplied, not owned by the txm */
        default:                    break;
    }
    return false;
}

static void model3dtx_drop(struct ref *ref)
{
    model3dtx *txm = container_of(ref, model3dtx, ref);
    const char *name = txm->model->name;

    trace("dropping model3dtx [%s]\n", name);
    list_del(&txm->entry);

    for (size_t i = 0; i < array_size(txm->textures); i++) {
        if (!txm->textures[i])  continue;

        /* put the references it holds, deinit its own, leave the borrowed ones alone */
        if (txm->texture_held[i])                           texture_done(txm->textures[i]);
        else if (!model3dtx_tex_is_ext(txm, ATTR_MAX + i))  texture_deinit(txm->textures[i]);
    }
    ref_put(txm->model);
}

static cerr model3dtx_make(struct ref *ref, void *_opts)
{
    rc_init_opts(model3dtx) *opts = _opts;

    if (!opts->model)
        return CERR_INVALID_ARGUMENTS;

    model3dtx *txm  = container_of(ref, model3dtx, ref);
    txm->textures[TEXIDX(UNIFORM_MODEL_TEX)]    = &txm->_texture;
    txm->textures[TEXIDX(UNIFORM_NORMAL_MAP)]   = &txm->_normals;
    txm->textures[TEXIDX(UNIFORM_EMISSION_MAP)] = &txm->_emission;
    list_init(&txm->entities);
    list_init(&txm->entry);

    txm->model = ref_get(opts->model);

    cerr err = CERR_INVALID_ARGUMENTS;

    if (opts->tex) {
        if (opts->texture_buffer || opts->texture_file_name)
            goto drop_txm;

        model3dtx_set_texture(txm, UNIFORM_MODEL_TEX, opts->tex);
    } else if (opts->texture_buffer) {
        if (opts->buffers_png)
            err = model3dtx_add_texture_from_png_buffer(txm, UNIFORM_MODEL_TEX, opts->texture_buffer,
                                                        opts->texture_size);
        else
            err = model3dtx_add_texture_from_buffer(txm, UNIFORM_MODEL_TEX, opts->texture_buffer,
                                                    opts->texture_width, opts->texture_height,
                                                    opts->texture_color_format);
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
                                                    opts->normal_width, opts->normal_height, TEX_FMT_RGB8);
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
                                                    opts->emission_width, opts->emission_height, TEX_FMT_RGB8);
        if (IS_CERR(err))
            goto drop_txm;
    } else if (opts->emission_file_name) {
        err = model3dtx_add_texture_at(txm, UNIFORM_EMISSION_MAP, opts->emission_file_name);

        if (IS_CERR(err))
            goto drop_txm;
    }

    txm->mat.roughness = opts->roughness;
    txm->mat.metallic = opts->metallic;
    txm->mat.uv_factor = 1.0f;

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
    int slot = shader_get_texture_slot(prog, var);

    if (slot < 0) {
        if (tex)
            dbg_once("program '%s' doesn't have texture %s or it's not a texture\n",
                     shader_name(prog), shader_get_var_name(var));
        return;
    }

    /*
     * Take the new reference first: @tex may be the texture it replaces.
     * Put the old one only if it's held, without asking it: a borrowed
     * texture's owner may have freed it already.
     */
    texture_t *old = txm->textures[TEXIDX(var)];
    bool old_held = txm->texture_held[TEXIDX(var)];

    txm->textures[TEXIDX(var)] = texture_get(tex);
    txm->texture_held[TEXIDX(var)] = texture_is_counted(tex);
    if (old_held)   texture_done(old);
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
    return fabs(m->aabb[1][0] - m->aabb[0][0]);
}

float model3d_aabb_Y(model3d *m)
{
    return fabs(m->aabb[1][1] - m->aabb[0][1]);
}

float model3d_aabb_Z(model3d *m)
{
    return fabs(m->aabb[1][2] - m->aabb[0][2]);
}

cerr model3d_set_degenerate_tris(model3d *m, const int *tris, size_t nr)
{
    mem_free(m->degenerate_tri);
    m->degenerate_tri = NULL;
    m->nr_degenerate_tri = 0;

    if (!nr)                    return CERR_OK;

    m->degenerate_tri = mem_alloc(sizeof(*m->degenerate_tri), .nr = nr);
    if (!m->degenerate_tri)     return CERR_NOMEM;

    memcpy(m->degenerate_tri, tris, nr * sizeof(*m->degenerate_tri));
    m->nr_degenerate_tri = nr;

    return CERR_OK;
}

void model3d_parts_skip(model3d *m, const char *prefix)
{
    size_t len = strlen(prefix);

    for (size_t i = darray_count(m->parts); i-- > 0;) {
        struct mesh_part *part = &m->parts.x[i];

        if (strncmp(m->joints[part->joint].name, prefix, len))  continue;

        mesh_part_done(part);
        darray_delete(m->parts, i);
    }
}

cerr model3d_part_box(model3d *m, int joint)
{
    struct mesh_part *part = CRES_RET_CERR(mesh_part_find(&m->parts, joint));

    vec3 aabb[2];
    vertex_array_aabb_calc(aabb, (float *)part->vx.x, darray_count(part->vx) * sizeof(vec3), 0);

    /* the unit cube stretched over the bounds */
    struct mesh *cube = &cube_mesh;
    struct mesh_part box = { .joint = part->joint };

    darray_init(box.vx);
    darray_init(box.idx);
    if (!darray_resize(box.vx, mesh_nr_vx(cube)) || !darray_resize(box.idx, mesh_nr_idx(cube))) {
        mesh_part_done(&box);
        return CERR_NOMEM;
    }

    for (size_t i = 0; i < mesh_nr_vx(cube); i++)
        for (int k = 0; k < 3; k++) box.vx.x[i][k] = aabb[0][k] + mesh_vx(cube)[i * 3 + k] * (aabb[1][k] - aabb[0][k]);
    for (size_t i = 0; i < mesh_nr_idx(cube); i++)  box.idx.x[i] = mesh_idx(cube)[i];

    mesh_part_done(part);
    *part = box;

    return CERR_OK;
}

static void model3d_aabb_center(model3d *m, vec3 center)
{
    aabb_center(m->aabb, center);
}

#ifndef CONFIG_FINAL
static void entity3d_aabb_draw(struct scene *scene, entity3d *e, bool entity, bool model)
{
    if (entity) {
        struct message dm = {
            .type   = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw){
                .color      = { 1.0, 1.0, 0.0, 1.0 },
                .thickness  = 1.0,
                .shape      = DEBUG_DRAW_AABB,
            }
        };
        entity3d_aabb_min(e, dm.debug_draw.v0);
        entity3d_aabb_max(e, dm.debug_draw.v1);
        message_send(scene->clap_ctx, &dm);
    }

    if (model) {
        model3d *m = e->txmodel->model;
        vec4 v0 = { m->aabb[0][0], m->aabb[0][1], m->aabb[0][2], 1.0 };
        vec4 v1 = { m->aabb[1][0], m->aabb[1][1], m->aabb[1][2], 1.0 };
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
        message_send(scene->clap_ctx, &dm);
    }
}

/*
 * Paint the collision triangles that the physics backend dropped as
 * degenerate (see model3d_set_degenerate_tris()): the edges in the entity's
 * world transform, plus a disc at the centroid, because a triangle this
 * small is a couple of pixels of outline at best.
 */
static void entity3d_degenerate_draw(struct scene *scene, entity3d *e)
{
    model3d *m = e->txmodel->model;

    for (size_t t = 0; t < m->nr_degenerate_tri; t++) {
        vec4 vx[3];
        vec3 centroid = {};

        for (int v = 0; v < 3; v++) {
            unsigned short idx = m->collision_idx[m->degenerate_tri[t] * 3 + v];
            const float *pos = &m->collision_vx[idx * 3];

            vec4_setup(vx[v], pos[0], pos[1], pos[2], 1.0);
            mat4x4_mul_vec4_post(vx[v], e->mx, vx[v]);
            vec3_add(centroid, centroid, vx[v]);
        }

        vec3_scale(centroid, centroid, 1.0 / 3.0);

        for (int v = 0; v < 3; v++) {
            struct message dm = {
                .type           = MT_DEBUG_DRAW,
                .debug_draw     = (struct message_debug_draw){
                    .color      = { 1.0, 1.0, 0.0, 1.0 },
                    .thickness  = 2.0,
                    .shape      = DEBUG_DRAW_LINE,
                    .v0         = { vx[v][0], vx[v][1], vx[v][2] },
                    .v1         = { vx[(v + 1) % 3][0], vx[(v + 1) % 3][1], vx[(v + 1) % 3][2] },
                }
            };
            message_send(scene->clap_ctx, &dm);
        }

        struct message dm = {
            .type           = MT_DEBUG_DRAW,
            .debug_draw     = (struct message_debug_draw){
                .color      = { 1.0, 1.0, 0.0, 1.0 },
                .shape      = DEBUG_DRAW_DISC,
                .radius     = 3.0,
                .v0         = { centroid[0], centroid[1], centroid[2] },
            }
        };
        message_send(scene->clap_ctx, &dm);
    }
}

static void entity3d_debug(struct scene *scene, entity3d *e)
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
    message_send(scene->clap_ctx, &dm);
}
#else
static inline void entity3d_aabb_draw(struct scene *scene, entity3d *e, bool entity, bool model) {}
static inline void entity3d_degenerate_draw(struct scene *scene, entity3d *e) {}
static inline void entity3d_debug(struct scene *scene, entity3d *e) {}
#endif /* CONFIG_FINAL */

int model3d_add_skinning(model3d *m, size_t nr_joints, mat4x4 *invmxs)
{
    for (int i = 0; i < JOINT_TYPE_MAX; i++)
        m->joint_types[i] = -1;

    m->joints = mem_alloc(sizeof(struct model_joint), .nr = nr_joints, .fatal_fail = 1);
    for (int j = 0; j < nr_joints; j++) {
        memcpy(&m->joints[j].invmx, invmxs[j], sizeof(mat4x4));
        mat4x4_invert(m->joints[j].bind, invmxs[j]);
        m->joints[j].parent = -1;
        darray_init(m->joints[j].children);
    }

    m->nr_joints = nr_joints;
    return 0;
}

cres(joint_type) model3d_get_joint(model3d *m, joint_type type)
{
    if (type >= JOINT_TYPE_MAX)
        return cres_error(joint_type, CERR_INVALID_ARGUMENTS);

    return m->joint_types[type] >= 0
        ? cres_val(joint_type, m->joint_types[type])
        : cres_error(joint_type, CERR_NOT_FOUND);
}

static const char *joint_type_str[JOINT_TYPE_MAX] = {
    [JOINT_HEAD]        = "head",
    [JOINT_FOOT_LEFT]   = "foot_left",
    [JOINT_FOOT_RIGHT]  = "foot_right",
    [JOINT_HAND_LEFT]   = "hand_left",
    [JOINT_HAND_RIGHT]  = "hand_right",
};

cresp(const_char) joint_type_name(joint_type type)
{
    if (type <= JOINT_NONE || type >= JOINT_TYPE_MAX)
        return cresp_error(const_char, CERR_INVALID_ARGUMENTS);

    return cresp_val(const_char, joint_type_str[type]);
}

cres(joint_type) joint_by_type_name(const char *type)
{
    for (joint_type i = JOINT_NONE; i < JOINT_TYPE_MAX; i++) {
        if (!joint_type_str[i])
            continue;

        if (!strcmp(joint_type_str[i], type))
            return cres_val(joint_type, i);
    }

    return cres_error(joint_type, CERR_NOT_FOUND);
}

/*
 * String-keyed wrapper over model3d_get_joint(): looks up a semantic
 * joint type by its JSON name ("head", "foot_left", ...) and returns
 * the matching index into model->joints. Used by the scene loader for
 * "armature"/"attach_joint" values.
 */
cres(joint_type) model3d_joint_by_type(model3d *m, const char *type)
{
    cres(joint_type) jt = joint_by_type_name(type);
    if (IS_CERR(jt))    return cres_error_cerr(joint_type, jt);

    return model3d_get_joint(m, jt.val);
}

cres(int) model3d_joint_by_name(model3d *m, const char *name)
{
    for (unsigned int i = 0; i < m->nr_joints; i++)
        if (m->joints[i].name && !strcmp(m->joints[i].name, name))  return cres_val(int, i);

    return cres_error(int, CERR_NOT_FOUND);
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
    shader_plug_attributes(p, m->attr);
}

static void model3dtx_prepare(model3dtx *txm, struct shader_prog *p)
{
    model3d_prepare(txm->model, p);

    for (enum shader_vars v = ATTR_MAX; v < UNIFORM_TEX_MAX; v++) {
        auto tex_res = model3dtx_loaded_texture(txm, v);
        if (IS_CERR(tex_res)) {
            if (shader_has_var(p, v)) {
                /*
                 * If the shader expects a texture that the model does not have,
                 * plug a 1-pixel texture in its place. For 3D textures: noise3d
                 * plug is a grey 1x1x1 pixel texture (becomes (0,0,0) after
                 * de-normalization), LUT is a black 1x1x1 pixel.
                 */
                texture_t *plug = black_pixel();

                switch (v) {
                    case UNIFORM_NOISE3D_TEX:   plug = grey_3d_pixel();     break;
                    case UNIFORM_LUT_TEX:       plug = black_3d_pixel();    break;
                    case UNIFORM_LIGHT_MAP:     plug = black_uint_pixel();  break;
                    default:                    break;
                }

                shader_plug_texture(p, v, plug);
            }
            continue;
        }
        shader_plug_texture(p, v, tex_res.val);
    }
}

static cerr_check model3dtx_draw(renderer_t *r, model3dtx *txm, unsigned int lod, unsigned int nr_instances)
{
    model3d *m = txm->model;

    /* GL_UNSIGNED_SHORT == typeof *indices */
    return renderer_draw(r, m->draw_type, m->nr_faces[lod], DT_USHORT, nr_instances);
}

static void model3d_done(model3d *m, struct shader_prog *p)
{
    shader_unplug_attributes(p, m->attr);

    vertex_array_unbind(&m->vao);
}

static void model3dtx_done(model3dtx *txm, struct shader_prog *p)
{
    for (enum shader_vars v = ATTR_MAX; v < UNIFORM_TEX_MAX; v++) {
        auto tex_res = model3dtx_loaded_texture(txm, v);
        if (IS_CERR(tex_res)) {
            if (shader_has_var(p, v))
                shader_unplug_texture(p, v, black_pixel());
            continue;
        }
        shader_unplug_texture(p, v, tex_res.val);
    }

    model3d_done(txm->model, p);
}

struct channel {
    float           *time;
    float           *data;
    unsigned int    nr;
    unsigned int    stride;
    unsigned int    target;
    enum chan_path  path;
};

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

/**
 * subview_texel_size() - the size of one shadow map texel through a subview
 * @sv: the subview whose projection renders the shadow map
 * @w:  shadow map width in texels
 * @h:  shadow map height in texels
 *
 * Through an orthographic projection the x/y scales are 2 / extent. The
 * light boxes are axis-aligned fits of a rotated camera frustum, so their
 * x and y extents differ, worst around diagonal yaws where one axis
 * inflates by up to sqrt(2): the coarser axis is the one that matters.
 * Through a perspective projection (a spotlight's) the x/y scales are
 * 1 / tan(fov / 2), and the same expression is the texel's size per unit
 * of depth.
 * Return: the texel's world size, or its size per unit of depth
 */
static float subview_texel_size(const struct subview *sv, unsigned int w, unsigned int h)
{
    return max(2.0 / sv->proj_mx[0][0] / (float)w, 2.0 / sv->proj_mx[1][1] / (float)h);
}

cerr _models_render(renderer_t *r, struct mq *mq, const models_render_options *opts)
{
    struct camera *camera = opts->camera;
    struct light *light = opts->light;
    struct subview *subview = NULL;
    float near_plane, far_plane;
    struct view *view = NULL;
    mat4x4 *inv_proj = NULL;
    mat4x4 *proj = NULL;

    if (camera) {
        view = &camera->view;
        proj = &camera->view.main.proj_mx;
        inv_proj = &camera->view.main.inv_proj_mx;

        /* Rasterize camera passes with the jittered projection */
        if (opts->taa) {
            proj = &opts->taa->proj_jittered;
            inv_proj = &opts->taa->inv_proj_jittered;
        }
    } else if (light) {
        view = &light->view[0];
        proj = &light->view[0].main.proj_mx;
        inv_proj = &light->view[0].main.inv_proj_mx;
    }

    if (view) {
        if (opts->cascade >= 0 && opts->cascade < (int)opts->nr_cascades) {
            subview = &view->subview[opts->cascade];
            proj = &subview->proj_mx;
            inv_proj = &subview->inv_proj_mx;
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

    /*
     * Shadow casters smaller than a few texels of this cascade's map
     * don't resolve to a stable shadow -- they alias and flicker.
     * render_options::shadow_caster_min_size is that threshold in
     * texels: converted through the cascade's world texel size it
     * self-tunes across map resolutions, cascade splits and aspect
     * ratios. The useful settings are far above bare resolvability
     * (tens of texels): distant small-object shadows are perceptual
     * clutter long before they stop resolving, which is why only the
     * coarse cascades cull. Arrayed shadow maps render all cascades
     * in one pass (cascade == -1) and can't use this.
     */
    float caster_min_size = 0.0;
    if (ropts && ropts->shadow_caster_min_size && opts->shader_override &&
        subview && opts->cascade > 1 && opts->width && opts->height)
        caster_min_size = ropts->shadow_caster_min_size *
                          subview_texel_size(subview, opts->width, opts->height);

    /* camera passes only: the shadow progs only carry the frozen attribute-layout bits */
    shader_variants_t global_variants = opts->variants_stale && ropts ? ropts_shader_variants(ropts) : 0;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        model3d *model = txmodel->model;
        if (model->skip_shadow && opts->shader_override)
            continue;

        if (opts->variants_stale && model->shader_base &&
            (model3dtx_shader_variants(txmodel) | global_variants) != model->variants)
            *opts->variants_stale = true;

        /*
         * In a shadow pass (@shader_override), a variant-managed model
         * draws with its own shadow prog: it shares @prog's attribute
         * set, which is what the model's vertex buffer was laid out
         * with. The pass override only covers models without one.
         */
        struct shader_prog *model_prog = opts->shader_override ?
            (model->shadow_prog ? : opts->shader_override) : model->prog;

        int lod = -1;

        cull_face cull = CULL_FACE_NONE;
        if (model->cull_face)
            cull = opts->shader_override ? CULL_FACE_FRONT : CULL_FACE_BACK;
        renderer_cull_face(r, cull);

        /*
         * A shadow map has nothing to blend: the depth-only ones have no
         * color, and the VSM moments come out of a two-component output,
         * which leaves alpha, the blend factor, undefined
         */
        renderer_blend(r, model->alpha_blend && !opts->shader_override, BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA);

        /* TODO: add a separate property for depth test control */
        // renderer_depth_test(r, model->depth_testing);

        if (model_prog != prog) {
            if (prog)
                shader_prog_done(prog, true);

            prog = model_prog;
            CERR_RET_CERR(shader_prog_use(prog, true));

            if (ropts) {
                shader_set_var_int(prog, UNIFORM_SHADOW_VSM, ropts->shadow_vsm);
                shader_set_var_float(prog, UNIFORM_SHADOW_BLEED, ropts->shadow_bleed);
                shader_set_var_int(prog, UNIFORM_SHADOW_OUTLINE, ropts->shadow_outline);
                shader_set_var_int(prog, UNIFORM_NR_CASCADES, opts->nr_cascades);
                shader_set_var_float(prog, UNIFORM_SHADOW_OUTLINE_THRESHOLD, ropts->shadow_outline_threshold);
                shader_set_var_int(prog, UNIFORM_USE_MSAA, ropts->shadow_msaa);
                /* TAA replaces SMAA; the builder skips the weights pass */
                shader_set_var_int(prog, UNIFORM_USE_EDGE_AA,
                                   ropts->edge_antialiasing && !ropts->taa);
                shader_set_var_int(prog, UNIFORM_USE_EDGES, ropts->edges);
                shader_set_var_int(prog, UNIFORM_USE_TAA, ropts->taa);
                shader_set_var_int(prog, UNIFORM_LAPLACE_KERNEL, ropts->laplace_kernel);
                shader_set_var_int(prog, UNIFORM_USE_HDR, ropts->hdr);
                shader_set_var_int(prog, UNIFORM_USE_SSAO, ropts->ssao);
                shader_set_var_int(prog, UNIFORM_HDR_OUTPUT, ropts->hdr_output);
                shader_set_var_float(prog, UNIFORM_SSAO_RADIUS, ropts->ssao_radius);
                shader_set_var_float(prog, UNIFORM_SSAO_WEIGHT, ropts->ssao_weight);
                shader_set_var_float(prog, UNIFORM_BLOOM_EXPOSURE, ropts->bloom_exposure);
                shader_set_var_float(prog, UNIFORM_BLOOM_DISTANCE, ropts->bloom_distance);
                shader_set_var_float(prog, UNIFORM_LIGHTING_EXPOSURE, ropts->lighting_exposure);
                if (ropts->hdr_output) {
                    shader_set_var_int(prog, UNIFORM_HDR_TRANSFER, ropts->hdr_transfer);
                    shader_set_var_float(prog, UNIFORM_HDR_WHITE_NITS, ropts->hdr_white_nits);
                    shader_set_var_float(prog, UNIFORM_HDR_PEAK_NITS, ropts->hdr_peak_nits);
                    shader_set_var_float(prog, UNIFORM_HDR_COMPRESS_KNEE, ropts->hdr_compress_knee);
                    shader_set_var_float(prog, UNIFORM_HDR_KNEE_SOFTNESS, ropts->hdr_knee_softness);
                }
                shader_set_var_float(prog, UNIFORM_BLOOM_OPERATOR, ropts->bloom_operator);
                shader_set_var_float(prog, UNIFORM_LIGHTING_OPERATOR, ropts->lighting_operator);
                shader_set_var_float(prog, UNIFORM_CONTRAST, ropts->contrast);
                shader_set_var_float(prog, UNIFORM_FOG_NEAR, ropts->fog_near);
                shader_set_var_float(prog, UNIFORM_FOG_FAR, ropts->fog_far);
                shader_set_var_int(prog, UNIFORM_USE_FOG, ropts->fog);
                shader_set_var_float(prog, UNIFORM_FOG_MAX, ropts->fog_max);
                shader_set_var_int(prog, UNIFORM_FOG_CURVE, ropts->fog_curve);
                shader_set_var_ptr(prog, UNIFORM_FOG_COLOR, 1, ropts->fog_color);
                shader_set_var_int(prog, UNIFORM_FILM_GRAIN, ropts->film_grain);
                shader_set_var_float(prog, UNIFORM_FILM_GRAIN_FACTOR, ropts->film_grain_factor);
                shader_set_var_float(prog, UNIFORM_FILM_GRAIN_POWER, ropts->film_grain_power);
                shader_set_var_int(prog, UNIFORM_FRAME_COUNT, (int)ropts->frame_count);
                shader_set_var_float(prog, UNIFORM_UPTIME, ropts->uptime);
                shader_set_var_int(prog, UNIFORM_VOLUMETRIC_FOG, ropts->fog_volumetric);
                shader_set_var_int(prog, UNIFORM_VOLUMETRIC_DEBUG, ropts->fog_volumetric_debug);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_INTENSITY, ropts->volumetric_intensity);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_G, ropts->volumetric_g);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_DENSITY, ropts->volumetric_density);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_AMP, ropts->volumetric_amp);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_SCALE, ropts->volumetric_scale);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_LOCAL_INTENSITY, ropts->volumetric_local_intensity);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_WIND, ropts->volumetric_wind);
                shader_set_var_float(prog, UNIFORM_VOLUMETRIC_FALLOFF, ropts->volumetric_falloff);
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
                shader_set_var_ptr(prog, UNIFORM_LIGHT_CUTOFF, LIGHTS_MAX, light->cutoff);
                shader_set_var_int(prog, UNIFORM_NR_LIGHTS, light->nr_lights);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_AMBIENT, 1, light->ambient);
                shader_set_var_ptr(prog, UNIFORM_SHADOW_TINT, 1, light->shadow_tint);
                shader_set_var_ptr(prog, UNIFORM_LIGHT_VOLUMETRIC, LIGHTS_MAX, light->volumetric);
                if (shader_has_var(prog, UNIFORM_SHADOW_MVP)) {
                    float shadow_texel[CASCADES_MAX];
                    float light_far[CASCADES_MAX];
                    mat4x4 mvp[CASCADES_MAX];
                    unsigned int map_w = 0, map_h = 0;
                    int i;

                    if (light->shadow[0][0])    texture_get_dimesnions(light->shadow[0][0], &map_w, &map_h);

                    for (i = 0; i < (int)opts->nr_cascades; i++) {
                        struct subview *light_sv = &light->view[0].subview[i];
                        mat4x4_mul(mvp[i], light_sv->proj_mx, light_sv->view_mx);
                        light_far[i] = light_sv->far_plane;
                        shadow_texel[i] = map_w ? subview_texel_size(light_sv, map_w, map_h) : 0.0;
                    }
                    shader_set_var_ptr(prog, UNIFORM_SHADOW_MVP, opts->nr_cascades, mvp);
                    shader_set_var_ptr(prog, UNIFORM_LIGHT_FAR, opts->nr_cascades, light_far);
                    shader_set_var_ptr(prog, UNIFORM_SHADOW_TEXEL, opts->nr_cascades, shadow_texel);
                }
            }

            if (view)
                shader_set_var_ptr(prog, UNIFORM_CASCADE_DISTANCES, opts->nr_cascades, view->divider);

            shader_set_var_float(prog, UNIFORM_NEAR_PLANE, near_plane);
            shader_set_var_float(prog, UNIFORM_FAR_PLANE, far_plane);

            if (subview) {
                shader_set_var_ptr(prog, UNIFORM_VIEW, 1, subview->view_mx);
                shader_set_var_ptr(prog, UNIFORM_INVERSE_VIEW, 1, subview->inv_view_mx);
            }

            if (proj && inv_proj) {
                shader_set_var_ptr(prog, UNIFORM_PROJ, 1, proj);
                shader_set_var_ptr(prog, UNIFORM_INVERSE_PROJ, 1, inv_proj);
            }

            if (opts->taa) {
                shader_set_var_ptr(prog, UNIFORM_PREV_PROJ_VIEW, 1, opts->taa->prev_proj_view);
                shader_set_var_ptr(prog, UNIFORM_JITTER, 1, opts->taa->jitter);
                shader_set_var_int(prog, UNIFORM_TAA_RESET, opts->taa->reset);
                if (ropts)  shader_set_var_float(prog, UNIFORM_TAA_FEEDBACK, ropts->taa_feedback);
            }
        }

        if (light)
            model3dtx_set_texture(txmodel, UNIFORM_LIGHT_MAP, &light->grid.tex);

        /* Set temporary shadow maps */
        if (light && light->shadow[0][0]) {
#ifndef CONFIG_SHADOW_MAP_ARRAY
            model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP, light->shadow[0][0]);
            model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP1, light->shadow[0][1] ? light->shadow[0][1] : white_pixel());
            model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP2, light->shadow[0][2] ? light->shadow[0][2] : white_pixel());
            model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP3, light->shadow[0][3] ? light->shadow[0][3] : white_pixel());
            shader_set_var_int(prog, UNIFORM_USE_MSAA, false);
#else
            if (shader_has_var(prog, UNIFORM_SHADOW_MAP_MS)) {
                if (ropts ? ropts->shadow_msaa : false) {
                    model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP, white_pixel());
                    model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP_MS, light->shadow[0][0]);
                } else {
                    model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP, light->shadow[0][0]);
                    model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP_MS, white_pixel());
                }
            } else {
                shader_set_var_int(prog, UNIFORM_USE_MSAA, false);
            }
#endif /* CONFIG_SHADOW_MAP_ARRAY */
        }

        model3dtx_prepare(txmodel, prog);

        auto normals_res = model3dtx_loaded_texture(txmodel, UNIFORM_NORMAL_MAP);
        shader_set_var_int(prog, UNIFORM_USE_NORMALS, !IS_CERR(normals_res));

#ifndef CONFIG_FINAL
        if (ropts && model->shader_base && !txmodel->variants_logged) {
            shader_variants_t variants = model3dtx_shader_variants(txmodel) |
                                         ropts_shader_variants(ropts);
            cresp(const_char) vres = shader_get_name(model->shader_base, variants);

            /* a mismatch is a stale resolution: pipeline_shaders_resolve() swaps the prog */
            dbg("model '%s' variants %#llx -> '%s', prog '%s'\n", txmodel_name(txmodel),
                (unsigned long long)variants, IS_CERR(vres) ? model->shader_base : vres.val,
                shader_name(model->prog));
            txmodel->variants_logged = true;
        }
#endif /* !CONFIG_FINAL */

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

        shader_set_var_float(prog, UNIFORM_UV_FACTOR, txmodel->mat.uv_factor);
        shader_set_var_int(prog, UNIFORM_USE_NOISE_NORMALS, txmodel->mat.use_noise_normals);
        if (txmodel->mat.use_noise_normals != NOISE_NORMALS_NONE) {
            shader_set_var_float(prog, UNIFORM_NOISE_NORMALS_AMP, txmodel->mat.noise_normals_amp);
            shader_set_var_float(prog, UNIFORM_NOISE_NORMALS_SCALE, txmodel->mat.noise_normals_scale);
        }

        shader_set_var_int(prog, UNIFORM_USE_NOISE_EMISSION, txmodel->mat.use_noise_emission);
        shader_set_var_ptr(prog, UNIFORM_NOISE_EMISSION_COLOR, 1, txmodel->mat.noise_emission_color);
        if (txmodel->mat.use_noise_normals != NOISE_NORMALS_NONE || txmodel->mat.use_noise_emission)
            shader_set_var_ptr(prog, UNIFORM_NOISE_DRIFT, 1, txmodel->mat.noise_drift);

        shader_set_var_int(prog, UNIFORM_USE_3D_FOG, txmodel->mat.use_3d_fog);
        if (txmodel->mat.use_3d_fog) {
            shader_set_var_float(prog, UNIFORM_FOG_3D_AMP, txmodel->mat.fog_3d_amp);
            shader_set_var_float(prog, UNIFORM_FOG_3D_SCALE, txmodel->mat.fog_3d_scale);
        }

        unsigned int nr_characters = 0;
        entity3d *e;

        list_for_each_entry (e, &txmodel->entities, entry) {
            if (!entity3d_matches(e, ENTITY3D_ALIVE))
                continue;

            if (unlikely(ropts && ropts->aabb_draws_enabled && !model->skip_aabb))
                entity3d_aabb_draw(mq->priv, e, true, true);

            if (unlikely(ropts && ropts->degenerate_draws_enabled))
                entity3d_degenerate_draw(mq->priv, e);

            if (!entity3d_matches(e, ENTITY3D_VISIBLE))
                continue;

            if (caster_min_size &&
                entity3d_aabb_avg_edge(e) < caster_min_size) {
                culled++;
                continue;
            }

            if (!entity3d_matches(e, ENTITY3D_SKIP_CULLING) &&
                subview && !view_entity_in_frustum(subview, e)) {
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

            uint32_t edge_mode = 0;
            if (e->outline_exclude) edge_mode |= EDGE_EXCLUDE;
            if (entity3d_matches(e, ENTITY3D_IS_CHARACTER) && mq->nr_characters) {
                nr_characters++;
                edge_mode |= (nr_characters & EDGE_SOLID_MASK) << EDGE_SOLID_OFFSET;
            }
            shader_set_var_int(prog, UNIFORM_EDGE_MODE, edge_mode);
            shader_set_var_ptr(prog, UNIFORM_IN_COLOR, 1, e->color);
            shader_set_var_int(prog, UNIFORM_COLOR_PASSTHROUGH, e->color_pt);

            if (model->nr_joints) {
                entity3d *pose = entity3d_matches(e, ENTITY3D_PARENT_POSE) ? e->parent : e;

                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 1);
                shader_set_var_ptr(prog, UNIFORM_JOINT_TRANSFORMS, model->nr_joints, pose->joint_transforms);
            } else {
                shader_set_var_int(prog, UNIFORM_USE_SKINNING, 0);
            }

            shader_set_var_ptr(prog, UNIFORM_TRS, 1, e->mx);
            shader_set_var_ptr(prog, UNIFORM_INVERSE_TRS, 1, e->inverse_mx);
            if (opts->taa) {
                shader_set_var_ptr(prog, UNIFORM_PREV_TRS, 1, e->prev_mx);
                shader_set_var_int(prog, UNIFORM_USE_VELOCITY, true);
                /*
                 * The model pass is the only consumer of prev_trs and
                 * the only camera pass drawing scene entities, so this
                 * doubles as the once-per-frame snapshot -- no separate
                 * O(entities) end-of-frame sweep. An entity that moves
                 * while frustum-culled re-enters the view with the prev_mx
                 * of its last drawn frame: one frame of stale velocity,
                 * absorbed like a spawn's
                 */
                mat4x4_dup(e->prev_mx, e->mx);
            }

            if (opts->ssao_state)
                ssao_upload(opts->ssao_state, prog, opts->width, opts->height);

            unsigned int nr_instances = 1;
            if (entity3d_matches(e, ENTITY3D_IS_PARTICLE)) {
                particle_system *ps = e->priv;
                particle_system_upload(ps, prog);
                nr_instances = particle_system_count(ps);
            }

            shader_var_blocks_update(prog);
            CERR_RET(
                model3dtx_draw(r, txmodel, e->cur_lod, nr_instances),
                {
                    err_cerr(__cerr, "failed to draw %s/%s\n", txmodel->model->name, entity_name(e));
                    /* for now, just skip to the next model */
                    break;
                }
            );
            nr_ents++;
        }

        if (lod >= 0)
            buffer_unbind(&model->index[lod], -1);

        model3dtx_done(txmodel, prog);

        if (light) {
            model3dtx_set_texture(txmodel, UNIFORM_LIGHT_MAP, NULL);
            /* Remove the temporary shadow maps */
            if (light->shadow[0][0]) {
#ifndef CONFIG_SHADOW_MAP_ARRAY
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP, NULL);
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP1, NULL);
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP2, NULL);
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP3, NULL);
#else
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP, NULL);
                model3dtx_set_texture(txmodel, UNIFORM_SHADOW_MAP_MS, NULL);
#endif /* CONFIG_SHADOW_MAP_ARRAY */
            }
        }

        nr_txms++;
    }

    if (opts->entity_count)
        *opts->entity_count = nr_ents;
    if (opts->txm_count)
        *opts->txm_count = nr_txms;
    if (opts->culled_count)
        *opts->culled_count = culled;
    if (prog)
        shader_prog_done(prog, true);

    return CERR_OK;
}

/****************************************************************************
 * entity3d
 * instance of the model3d
 ****************************************************************************/

bool entity3d_matches(entity3d *e, entity3d_flags flags)
{
    return !!(e->flags & flags);
}

bool entity3d_is(entity3d *e, entity3d_query *query)
{
    if ((e->flags & query->all_of) != query->all_of)    return false;
    if (!!(e->flags & query->none_of))                  return false;
    if (query->any_of && !(e->flags & query->any_of))   return false;

    return true;
}

cerr entity3d_set(entity3d *e, entity3d_flags flags, void *priv)
{
    entity3d_flags type = flags & ENTITY3D_TYPE_MASK;

    if (type) {
        /* Mutually exclusive type flags: only one allowed at a time */
        if (e->flags & ENTITY3D_TYPE_MASK & ~type)
            return CERR_INVALID_OPERATION;
        e->priv = priv;
    }

    e->flags |= flags;

    return CERR_OK;
}

void entity3d_clear(entity3d *e, entity3d_flags flags)
{
    if (flags & ENTITY3D_TYPE_MASK)
        e->priv = NULL;
    e->flags &= ~flags;
}

cresp(character) entity3d_character(entity3d *e)
{
    if (!entity3d_matches(e, ENTITY3D_IS_CHARACTER))
        return cresp_error(character, CERR_INVALID_OPERATION);
    return cresp_val(character, e->priv);
}

cresp(ui_element) entity3d_ui_element(entity3d *e)
{
    if (!entity3d_matches(e, ENTITY3D_IS_UI))
        return cresp_error(ui_element, CERR_INVALID_OPERATION);
    return cresp_val(ui_element, e->priv);
}

cresp(particle_system) entity3d_particle_system(entity3d *e)
{
    if (!entity3d_matches(e, ENTITY3D_IS_PARTICLE))
        return cresp_error(particle_system, CERR_INVALID_OPERATION);
    return cresp_val(particle_system, e->priv);
}

cres(size_t) mq_for_each_query(struct mq *mq, entity3d_query *query, entity3d_callback cb, void *data)
{
    model3dtx *txmodel;
    entity3d *ent, *itent;
    size_t count = 0;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            if (!entity3d_is(ent, query))
                continue;

            cb(ent, data);
            count++;
        }
    }

    return count ? cres_val(size_t, count) : cres_error(size_t, CERR_NOT_FOUND);
}

cresp(entity3d) mq_find_first(struct mq *mq, entity3d_query *query)
{
    model3dtx *txmodel;
    entity3d *ent;

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        list_for_each_entry(ent, &txmodel->entities, entry) {
            if (entity3d_is(ent, query))
                return cresp_val(entity3d, ent);
        }
    }

    return cresp_error(entity3d, CERR_NOT_FOUND);
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
        { m->aabb[0][0], m->aabb[0][1], m->aabb[0][2], 1.0 },
        { m->aabb[0][0], m->aabb[1][1], m->aabb[0][2], 1.0 },
        { m->aabb[0][0], m->aabb[0][1], m->aabb[1][2], 1.0 },
        { m->aabb[0][0], m->aabb[1][1], m->aabb[1][2], 1.0 },
        { m->aabb[1][0], m->aabb[0][1], m->aabb[0][2], 1.0 },
        { m->aabb[1][0], m->aabb[1][1], m->aabb[0][2], 1.0 },
        { m->aabb[1][0], m->aabb[0][1], m->aabb[1][2], 1.0 },
        { m->aabb[1][0], m->aabb[1][1], m->aabb[1][2], 1.0 },
    };
    vec4 v;
    int i;

    e->aabb[0][0] = e->aabb[0][1] = e->aabb[0][2] = INFINITY;
    e->aabb[1][0] = e->aabb[1][1] = e->aabb[1][2] = -INFINITY;
    for (i = 0; i < array_size(corners); i++) {
        mat4x4_mul_vec4_post(v, e->mx, corners[i]);
        e->aabb[0][0] = min(v[0], e->aabb[0][0]);
        e->aabb[1][0] = max(v[0], e->aabb[1][0]);
        e->aabb[0][1] = min(v[1], e->aabb[0][1]);
        e->aabb[1][1] = max(v[1], e->aabb[1][1]);
        e->aabb[0][2] = min(v[2], e->aabb[0][2]);
        e->aabb[1][2] = max(v[2], e->aabb[1][2]);

    }

    aabb_center(e->aabb, e->aabb_center);
}

void entity3d_aabb_min(entity3d *e, vec3 min)
{
    min[0] = e->aabb[0][0];
    min[1] = e->aabb[0][1];
    min[2] = e->aabb[0][2];
}

void entity3d_aabb_max(entity3d *e, vec3 max)
{
    max[0] = e->aabb[1][0];
    max[1] = e->aabb[1][1];
    max[2] = e->aabb[1][2];
}

void entity3d_aabb_center(entity3d *e, vec3 center)
{
    vec3 minv;
    vec3_dup(minv, e->txmodel->model->aabb[0]);
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

static void channel_transform(struct joint *joints, struct channel *chan, float time)
{
    void *p_data, *n_data;
    struct joint *joint = &joints[chan->target];
    float p_time, n_time, fac = 0;
    int prev, next;

    /*
     * Don't crash on broken animations (channel referenced a non-existent
     * joint, see warning in gltf_instantiate_one()).
     */
    if (!chan->nr || !chan->data || !chan->time)
        return;

    channel_time_to_idx(chan, time, joint->off[chan->path], &prev, &next);

    size_t p_off, n_off;
    if (mul_overflow(prev, chan->stride, &p_off))   { warn_once("animation index overflow: %d\n", prev); return; }
    if (mul_overflow(next, chan->stride, &n_off))   { warn_once("animation index overflow: %d\n", next); return; }

    joint->off[chan->path] = min(prev, next);

    p_time = chan->time[prev];
    n_time = chan->time[next];
    if (p_time > n_time)
        fac = time < n_time ? 1 : 0;
    else if (p_time < n_time)
        fac = (time - p_time) / (n_time - p_time);

    p_data = (void *)chan->data + p_off;
    n_data = (void *)chan->data + n_off;

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
    default:
    }
}

void animation_apply(struct animation *an, double time, struct joint *joints)
{
    int ch;

    for (ch = 0; ch < an->nr_channels; ch++)
        channel_transform(joints, &an->channels[ch], (float)time);
}

void animation_reset_cursors(struct animation *an, struct joint *joints)
{
    for (unsigned int ch = 0; ch < an->nr_channels; ch++)
        joints[an->channels[ch].target].off[an->channels[ch].path] = 0;
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

    /* translate to model space */
    mat4x4 trs;
    mat4x4_mul(trs, e->joint_transforms[joint], model->joints[joint].bind);

    /* model space position */
    vec4 mpos;
    mat4x4_mul_vec4_post(mpos, trs, (vec4) { 0.0f, 0.0f, 0.0f, 1.0f });

    /* world space position */
    mat4x4_mul_vec4_post(j->pos, e->mx, mpos);

    darray_for_each(child, model->joints[joint].children)
        one_joint_transform(e, *child, joint);
}

void model3d_joint_bind_local(model3d *m, int joint, int parent, vec3 t, quat r, vec3 s)
{
    mat4x4 *parent_bind = parent >= 0 ? &m->joints[parent].bind : &m->root_pose;
    mat4x4 inv_parent, local;

    mat4x4_invert(inv_parent, *parent_bind);
    mat4x4_mul(local, inv_parent, m->joints[joint].bind);
    mat4x4_decompose(local, t, r, s);
}

/**
 * one_joint_bind_pose() - pose a joint and its subtree at the bind pose
 * @e:      entity3d object
 * @joint:  the joint
 * @parent: its parent joint, -1 for the skeleton root
 */
static void one_joint_bind_pose(entity3d *e, int joint, int parent)
{
    model3d *model = e->txmodel->model;
    struct joint *j = &e->joints[joint];
    int *child;

    model3d_joint_bind_local(model, joint, parent, j->translation, j->rotation, j->scale);

    darray_for_each(child, model->joints[joint].children)
        one_joint_bind_pose(e, *child, joint);
}

void entity3d_pose_reset(entity3d *e)
{
    if (!e->joints)
        return;

    one_joint_bind_pose(e, e->txmodel->model->root_joint, -1);
}

void entity3d_pose_update(entity3d *e)
{
    if (!e->joints)
        return;

    one_joint_transform(e, e->txmodel->model->root_joint, -1);
    e->pose_seq = e->seq;
}

/* global = parent's global * T * R * S, the same product one_joint_transform() forms */
void entity3d_joint_global(entity3d *e, int joint, mat4x4 global)
{
    model3d *model = e->txmodel->model;

    if (joint < 0 || (unsigned int)joint >= model->nr_joints) {
        memcpy(global, model->root_pose, sizeof(mat4x4));
        return;
    }

    struct joint *j = &e->joints[joint];
    mat4x4 T, R;

    entity3d_joint_global(e, model->joints[joint].parent, global);
    mat4x4_translate(T, j->translation[0], j->translation[1], j->translation[2]);
    mat4x4_mul(global, global, T);
    mat4x4_from_quat(R, j->rotation);
    mat4x4_mul(global, global, R);
    mat4x4_scale_aniso(global, global, j->scale[0], j->scale[1], j->scale[2]);
}

void entity3d_point_in_parent(entity3d *e, int joint, const vec3 point, vec3 local)
{
    vec4 p = { point[0], point[1], point[2], 1.0f }, l;
    mat4x4 parent, inv;

    entity3d_joint_global(e, e->txmodel->model->joints[joint].parent, parent);
    mat4x4_invert(inv, parent);
    mat4x4_mul_vec4(l, inv, p);
    vec3_dup(local, l);
}

int animation_by_name(model3d *m, const char *name)
{
    int i;

    for (i = 0; i < m->anis.da.nr_el; i++)
        if (!strcmp(name, m->anis.x[i].name))   return i;
    return -1;
}

static bool parent_transform_apply(entity3d *e)
{
    auto parent = e->parent;

    if (!parent)    return false;

    bool xform_dirty = transform_is_updated(&e->xform);

    /*
     * Jointless attachment can skip recalculation when neither our
     * own xform nor the parent's world matrix have changed since the
     * last update — tracked via seq/parent_seq. Joint attachments
     * have to recalculate every frame because the joint may be
     * animating underneath us.
     */
    if (e->parent_joint == JOINT_TYPE_MAX &&
        e->parent_seq == parent->seq && !xform_dirty)
        return true;

    e->parent_seq = parent->seq;
    if (xform_dirty)
        transform_clear_updated(&e->xform);
    e->seq++;

    mat4x4 local;
    mat4x4_identity(local);
    transform_translate_mat4x4(&e->xform, local);
    transform_rotate_mat4x4(&e->xform, local);
    mat4x4_scale_aniso(local, local, e->scale, e->scale, e->scale);

    if (e->parent_joint == JOINT_TYPE_MAX) {
        mat4x4_mul(e->mx, parent->mx, local);
    } else {
        /*
         * Joint-attached: the joint transform is in skinning
         * (pre-bind) space, so multiply by the joint's bind pose to
         * get a plain joint → model-space matrix, then chain through
         * the parent's world mx.
         */
        model3d *pmodel = parent->txmodel->model;

        mat4x4 joint_mx;
        mat4x4_mul(joint_mx,
                   parent->joint_transforms[e->parent_joint],
                   pmodel->joints[e->parent_joint].bind);
        mat4x4_mul(e->mx, joint_mx, local);
        mat4x4_mul(e->mx, parent->mx, e->mx);
    }

    mat4x4_invert(e->inverse_mx, e->mx);
    entity3d_aabb_update(e);

    return true;
}

static int default_update(entity3d *e, void *data)
{
    struct scene *scene = data;

    /*
     * Dynamic non-character bodies are driven by physics simulation
     * (phys_step() runs before entity updates). Sync their position
     * and rotation from ODE before rebuilding the transform matrix.
     * Characters do this in character_update() instead.
     */
    if (entity3d_is(e,
            &E3DQ(
                .all_of     = ENTITY3D_HAS_PHYSICS | ENTITY3D_PHYS_IS_BODY,
                .none_of    = ENTITY3D_IS_CHARACTER
            )
        ))
        phys_body_update(e);

    /* the procedural rigs place the body as they pose it: before its matrix */
    if (scene && !entity3d_matches(e, ENTITY3D_PARENT_POSE))    animator_update(&e->animator, scene);

    if (!parent_transform_apply(e) && transform_is_updated(&e->xform)) {
        transform_clear_updated(&e->xform);
        e->seq++;
        mat4x4_identity(e->mx);
        transform_translate_mat4x4(&e->xform, e->mx);
        transform_rotate_mat4x4(&e->xform, e->mx);
        transform_clear_updated(&e->xform);

        mat4x4_scale_aniso(e->mx, e->mx, e->scale, e->scale, e->scale);
        mat4x4_invert(e->inverse_mx, e->mx);

        entity3d_aabb_update(e);

        /*
         * Push entity rotation to the physics body for characters and static
         * colliders. Dynamic bodies  get their rotation from the simulation
         * instead (see phys_body_update()).
         */
        if (entity3d_is(e, &E3DQ(.all_of = ENTITY3D_HAS_PHYSICS | ENTITY3D_IS_CHARACTER)) ||
            entity3d_is(e, &E3DQ(.all_of = ENTITY3D_HAS_PHYSICS, .none_of = ENTITY3D_PHYS_IS_BODY)))
            phys_body_rotate_xform(e->phys_body, &e->xform);

        /* the entity's lights follow it with its transform for this frame */
        if (e->light && e->light_idx >= 0)  light_fire_update(e->light, e->light_idx);
    }

    if (!scene)
        return 0;

    /*
     * Find the biggest bounding volume that contains camera or the character
     */
    struct camera *cam = scene->camera;
    if ((aabb_point_is_inside(e->aabb, transform_pos(&cam->xform, NULL)) ||
         (scene->control && aabb_point_is_inside(e->aabb, transform_pos(&scene->control->xform, NULL)))) &&
         e != scene->control) {
        float volume = entity3d_aabb_X(e) * entity3d_aabb_Y(e) * entity3d_aabb_Z(e);

        if (!cam->bv || volume > cam->bv_volume) {
            cam->bv = e;
            cam->bv_volume = volume;
        }
    }

    /*
     * Nothing posed the skeleton this frame, but the entity moved: its
     * joints' world positions (the camera's head target, the armature
     * draw) follow it all the same.
     */
    if (e->joints && e->pose_seq != e->seq && !entity3d_matches(e, ENTITY3D_PARENT_POSE))   entity3d_pose_update(e);
    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS))
        phys_debug_draw(scene, e->phys_body);
    if (darray_count(e->limbs)) {
        struct phys_body **limb;

        phys_limbs_sync(e);
        darray_for_each(limb, e->limbs)
            phys_debug_draw(scene, *limb);
    }
    if (clap_get_render_options(scene->clap_ctx)->overlay_draws_enabled)
        entity3d_debug(scene, e);

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

    transform_init(&e->xform);
    mat4x4_identity(e->inverse_mx);
    mat4x4_identity(e->prev_mx);
    animator_init(&e->animator, e);
    darray_init(e->limbs);
    e->light     = NULL;
    e->light_idx = -1;
    e->force_lod = -1;
    e->scale     = 1.0;
    e->update    = default_update;
    e->parent_joint = JOINT_TYPE_MAX;

    model3d *model = opts->txmodel->model;
    e->txmodel = ref_get(opts->txmodel);
    entity3d_aabb_update(e);

    entity3d_flags init_flags = ENTITY3D_ALIVE | ENTITY3D_VISIBLE;
    if (model->nr_joints) {
        e->joints = mem_alloc(sizeof(*e->joints), .nr = model->nr_joints, .fatal_fail = 1);
        e->joint_transforms = mem_alloc(sizeof(mat4x4), .nr = model->nr_joints, .fatal_fail = 1);
        init_flags |= ENTITY3D_HAS_ARMATURE;

        /*
         * Identity skinning matrices are the bind pose, so entities of
         * skinned models render correctly before (or without) any
         * animation or procedural pose touching the joints.
         */
        for (unsigned int j = 0; j < model->nr_joints; j++) mat4x4_identity(e->joint_transforms[j]);

        /*
         * The local transforms start out as the bind pose too: a
         * procedural pose may run before (or without) any clip that
         * writes every joint. The world positions come with the first
         * update, once the entity is placed.
         */
        entity3d_pose_reset(e);
        e->pose_seq = e->seq - 1;
    }
    if (model->anis.da.nr_el)
        init_flags |= ENTITY3D_IS_ANIMATED;

    list_append(&e->txmodel->entities, &e->entry);
    e->flags |= init_flags;

    return CERR_OK;
}

static void entity3d_drop(struct ref *ref)
{
    entity3d *e = container_of(ref, entity3d, ref);
    trace("dropping entity3d\n");
    if (e->light && e->light_idx >= 0)
        light_put(e->light, e->light_idx);
    list_del(&e->entry);
    ref_put(e->txmodel);

    animator_done(&e->animator);
    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS)) {
        phys_body_done(e->phys_body);
        e->phys_body = NULL;
        entity3d_clear(e, ENTITY3D_HAS_PHYSICS | ENTITY3D_PHYS_IS_BODY);
    }

    struct phys_body **limb;
    darray_for_each(limb, e->limbs)
        phys_body_done(*limb);
    darray_clearout(e->limbs);
    mem_free(e->joints);
    mem_free(e->joint_transforms);
    mem_free(e->name);
}

DEFINE_REFCLASS2(entity3d);

void entity3d_delete(entity3d *e)
{
    entity3d_clear(e, ENTITY3D_ALIVE);
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
    e->phys_body = phys_body_new(phys, e, class, geom_radius, geom_off, geom_length, type, mass);
    if (e->phys_body) {
        entity3d_set(e, ENTITY3D_HAS_PHYSICS, NULL);
        if (phys_body_has_body(e->phys_body))
            entity3d_set(e, ENTITY3D_PHYS_IS_BODY, NULL);
    }
}

void entity3d_visible(entity3d *e, unsigned int visible)
{
    if (visible)
        entity3d_set(e, ENTITY3D_VISIBLE, NULL);
    else
        entity3d_clear(e, ENTITY3D_VISIBLE);
}

void entity3d_rotate(entity3d *e, float rx, float ry, float rz)
{
    transform_set_angles(&e->xform, (float[3]){ rx, ry, rz }, false);
}

void entity3d_scale(entity3d *e, float scale)
{
    e->scale = scale;
    /* XXX: scale -> transform */
    transform_set_updated(&e->xform);
}

void entity3d_position(entity3d *e, vec3 pos)
{
    transform_set_pos(&e->xform, pos);
    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS))
        phys_body_set_position(e->phys_body, pos);
}

void entity3d_world_to_model(entity3d *e, const vec3 world, vec3 model)
{
    vec3_sub(model, world, transform_pos(&e->xform, nullptr));
    transform_unrotate_vec3(&e->xform, model);
    if (e->scale != 0.0f && e->scale != 1.0f)   vec3_scale(model, model, 1.0f / e->scale);
}

void entity3d_move(entity3d *e, vec3 off)
{
    const float *pos = transform_move(&e->xform, off);
    if (entity3d_matches(e, ENTITY3D_HAS_PHYSICS))
        phys_body_set_position(e->phys_body, pos);
}

#define COLOR_PT_ALPHA_ALL  (COLOR_PT_SET_ALPHA | COLOR_PT_REPLACE_ALPHA | COLOR_PT_BLEND_ALPHA)
#define COLOR_PT_RGB_ALL    (COLOR_PT_SET_RGB | COLOR_PT_REPLACE_RGB)
void entity3d_color(entity3d *e, int color_pt, vec4 color)
{
    if (color_pt == COLOR_PT_NONE)  { e->color_pt = color_pt; return; }

    int alpha_mask = color_pt & COLOR_PT_ALPHA_ALL;
    int rgb_mask = color_pt & COLOR_PT_RGB_ALL;

    /* only one alpha and one RGB action are allowed */
    if (alpha_mask & (alpha_mask - 1) || rgb_mask & (rgb_mask - 1)) {
        err("invalid color_pt setting: %02x\n", color_pt);
        return;
    }

    vec4_dup(e->color, color);
    e->color_pt = color_pt;
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
    mq->nr_characters = 0;
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

void mq_for_each_matching(struct mq *mq, entity3d_flags flags, entity3d_callback cb, void *data)
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

struct mq_entity_match {
    const char  *name;
    entity3d    *e;
};

static void mq_entity_match(entity3d *e, void *data)
{
    struct mq_entity_match *mem = data;

    if (!strcmp(entity_name(e), mem->name))
        mem->e = e;
}

cresp(entity3d) mq_find_entity(struct mq *mq, const char *name)
{
    struct mq_entity_match mem = { .name = name };

    mq_for_each(mq, mq_entity_match, &mem);
    if (!mem.e)
        return cresp_error(entity3d, CERR_NOT_FOUND);

    return cresp_val(entity3d, mem.e);
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
