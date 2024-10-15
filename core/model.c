// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE
#define DEBUG
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include "librarian.h"
#include "common.h"
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

static void model3d_drop(struct ref *ref)
{
    struct model3d *m = container_of(ref, struct model3d, ref);
    struct animation *an;
    struct joint *joint;
    int i;

    glDeleteBuffers(1, &m->vertex_obj);
    for (i = 0; i < m->nr_lods; i++)
        glDeleteBuffers(1, &m->index_obj[i]);
    if (m->norm_obj)
        glDeleteBuffers(1, &m->norm_obj);
    if (m->tex_obj)
        glDeleteBuffers(1, &m->tex_obj);
    if (m->nr_joints) {
        GL(glDeleteBuffers(1, &m->joints_obj));
        GL(glDeleteBuffers(1, &m->weights_obj));
    }
    if (gl_does_vao())
        glDeleteVertexArrays(1, &m->vao);
    /* delete gl buffers */
    ref_put(m->prog);
    trace("dropping model '%s'\n", m->name);
    darray_for_each(an, &m->anis) {
        for (i = 0; i < an->nr_channels; i++) {
            free(an->channels[i].time);
            free(an->channels[i].data);
        }
        free(an->channels);
        free(an->name);
    }
    darray_clearout(&m->anis.da);
    for (i = 0; i < m->nr_joints; i++) {
        darray_clearout(&m->joints[i].children.da);
        free(m->joints[i].name);
    }
    free(m->joints);
    free(m->collision_vx);
    free(m->collision_idx);
    free(m->name);
}

DECLARE_REFCLASS(model3d);

static int load_gl_texture_buffer(struct shader_prog *p, void *buffer, int width, int height,
                                  int has_alpha, GLuint target, GLint loc, texture_t *tex)
{
    GLuint color_type = has_alpha ? GL_RGBA : GL_RGB;
    if (!buffer)
        return -EINVAL;

    if (loc < 0)
        return -EINVAL;

    //hexdump(buffer, 16);
    texture_init_target(tex, target);
    texture_filters(tex, GL_REPEAT, GL_NEAREST);
    GL(glUniform1i(loc, target - GL_TEXTURE0));

    // Bind it
    texture_load(tex, color_type, width, height, buffer);

    return 0;
}

/* XXX: actually, make it take the decoded texture, not the png */
static int model3d_add_texture_from_buffer(struct model3dtx *txm, GLuint target, void *input, size_t length)
{
    struct shader_prog *prog = txm->model->prog;
    texture_t *targets[] = { txm->texture, txm->normals, txm->emission };
    GLint locs[] = { prog->texture_map, prog->normal_map, prog->emission_map };
    int width, height, has_alpha, ret;
    unsigned char *buffer;

    // dbg("## shader '%s' texture_map: %d normal_map: %d\n", prog->name, prog->texture_map, prog->normal_map);
    buffer = decode_png(input, length, &width, &height, &has_alpha);
    shader_prog_use(prog);
    ret = load_gl_texture_buffer(prog, buffer, width, height, has_alpha, target,
                                 locs[target - GL_TEXTURE0],
                                 targets[target - GL_TEXTURE0]);
    shader_prog_done(prog);
    free(buffer);
    dbg("loaded texture%d %d %dx%d\n", target - GL_TEXTURE0, texture_id(txm->texture), width, height);

    return ret;
}

static int model3d_add_texture_at(struct model3dtx *txm, GLuint target, const char *name)
{
    int width = 0, height = 0, has_alpha = 0, ret;
    unsigned char *buffer = fetch_png(name, &width, &height, &has_alpha);
    struct shader_prog *prog = txm->model->prog;
    texture_t *targets[] = { txm->texture, txm->normals };
    GLint locs[] = { prog->texture_map, prog->normal_map };

    shader_prog_use(prog);
    ret = load_gl_texture_buffer(prog, buffer, width, height, has_alpha, target,
                                 locs[target - GL_TEXTURE0],
                                 targets[target - GL_TEXTURE0]);
    shader_prog_done(prog);
    free(buffer);

    dbg("loaded texture%d %d %dx%d\n", target - GL_TEXTURE0, texture_id(txm->texture), width, height);

    return ret;
}

static int model3d_add_texture(struct model3dtx *txm, const char *name)
{
    return model3d_add_texture_at(txm, GL_TEXTURE0, name);
}

static int model3dtx_make(struct ref *ref)
{
    struct model3dtx *txm = container_of(ref, struct model3dtx, ref);
    txm->texture = &txm->_texture;
    txm->normals = &txm->_normals;
    txm->emission = &txm->_emission;
    txm->sobel = &txm->_sobel;
    list_init(&txm->entities);
    list_init(&txm->entry);
    return 0;
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
    if (model3d_add_texture(txm, tex)) {
        ref_put_last(txm);
        return NULL;
    }

    if (norm)
        model3d_add_texture_at(txm, GL_TEXTURE1, norm);
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
    load_gl_texture_buffer(model->prog, fake_emission, 1, 1, true, GL_TEXTURE2, model->prog->emission_map,
                           txm->emission);
    shader_prog_done(model->prog);
}

static void model3dtx_add_fake_sobel(struct model3dtx *txm)
{
    struct model3d *model = txm->model;
    float fake_sobel[4] = { 1.0, 1.0, 1.0, 1.0 };

    shader_prog_use(model->prog);
    load_gl_texture_buffer(model->prog, fake_sobel, 1, 1, true, GL_TEXTURE3, model->prog->sobel_tex,
                           txm->sobel);
    shader_prog_done(model->prog);
}

struct model3dtx *model3dtx_new_from_buffer(struct model3d *model, void *buffer, size_t length)
{
    if (!buffer || !length)
        goto err;

    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);
    model3d_add_texture_from_buffer(txm, GL_TEXTURE0, buffer, length);
    model3dtx_add_fake_emission(txm);
    model3dtx_add_fake_sobel(txm);

    return txm;

err:
    ref_put_passed(model);
    return NULL;
}

struct model3dtx *model3dtx_new_from_buffers(struct model3d *model, void *tex, size_t texsz, void *norm, size_t normsz)
{
    if (!tex || !texsz || !norm || !normsz)
        goto err;

    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);
    model3d_add_texture_from_buffer(txm, GL_TEXTURE0, tex, texsz);
    model3d_add_texture_from_buffer(txm, GL_TEXTURE1, norm, normsz);
    model3dtx_add_fake_emission(txm);
    model3dtx_add_fake_sobel(txm);

    return txm;

err:
    ref_put_passed(model);
    return NULL;
}

struct model3dtx *model3dtx_new_from_buffers2(struct model3d *model, void *tex, size_t texsz, void *norm, size_t normsz,
                                              void *em, size_t emsz)
{
    if (!tex || !texsz /*|| !norm || !normsz*/ || !em || !emsz)
        goto err;

    struct model3dtx *txm = ref_new(model3dtx);

    if (!txm)
        goto err;

    txm->model = ref_get(model);
    model3d_add_texture_from_buffer(txm, GL_TEXTURE0, tex, texsz);
    if (norm && normsz)
        model3d_add_texture_from_buffer(txm, GL_TEXTURE1, norm, normsz);
    model3d_add_texture_from_buffer(txm, GL_TEXTURE2, em, emsz);

    return txm;

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
    txm->external_tex = true;

    return txm;

err:
    ref_put_passed(model);
    return NULL;
}

void model3dtx_set_texture(struct model3dtx *txm, int target, texture_t *tex)
{
    struct shader_prog *prog = txm->model->prog;
    texture_t **targets[] = { &txm->texture, &txm->normals, &txm->emission, &txm->sobel };
    GLint locs[] = { prog->texture_map, prog->normal_map, prog->emission_map, prog->sobel_tex };

    if (target >= array_size(targets))
        return;

    if (locs[target] < 0) {
        dbg("program '%s' doesn't have loc %d\n", prog->name, target);
        return;
    }

    *targets[target] = tex;
}

void model3dtx_set_texture_from(struct model3dtx *txm, int to, struct model3dtx *src, int from)
{
    texture_t *targets[] = { src->texture, src->normals, src->emission, src->sobel };

    model3dtx_set_texture(txm, to, targets[from]);
}

static void load_gl_buffer(GLint loc, void *data, GLuint type, size_t sz, GLuint *obj,
                           GLint nr_coords, GLenum target)
{
    GL(glGenBuffers(1, obj));
    GL(glBindBuffer(target, *obj));
    GL(glBufferData(target, sz, data, GL_STATIC_DRAW));
    
    /*
     *   <attr number>
     *   <number of elements in a vector>
     *   <type>
     *   <is normalized?>
     *   <stride> for interleaving
     *   <offset>
     */
    if (loc >= 0) {
        // glEnableVertexAttribArray(loc);
        GL(glVertexAttribPointer(loc, nr_coords, type, GL_FALSE, 0, (void *)0));
    }
    
    GL(glBindBuffer(target, 0));
}

void model3d_set_name(struct model3d *m, const char *fmt, ...)
{
    va_list ap;

    free(m->name);
    va_start(ap, fmt);
    CHECK(vasprintf(&m->name, fmt, ap));
    va_end(ap);
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

static void model3d_prepare(struct model3d *m);
static void model3d_done(struct model3d *m);

void model3d_add_tangents(struct model3d *m, float *tg, size_t tgsz)
{
    if (m->prog->tangent == -1) {
        dbg("no tangent input in program '%s'\n", m->prog->name);
        return;
    }

    shader_prog_use(m->prog);
    model3d_prepare(m);
    load_gl_buffer(m->prog->tangent, tg, GL_FLOAT, tgsz, &m->tangent_obj, 4, GL_ARRAY_BUFFER);
    model3d_done(m);
    shader_prog_done(m->prog);
}

int model3d_add_skinning(struct model3d *m, unsigned char *joints, size_t jointssz,
                         float *weights, size_t weightssz, size_t nr_joints, mat4x4 *invmxs)
{
    int v, j, jmax = 0;

    if (jointssz != m->nr_vertices * 4 ||
        weightssz != m->nr_vertices * 4 * sizeof(float)) {
        err("wrong amount of joints or weights: %zu <> %d, %zu <> %lu\n",
            jointssz, m->nr_vertices * 4, weightssz, m->nr_vertices * 4 * sizeof(float));
        return -1;
    }

    /* incoming ivec4 joints and vec4 weights */
    for (v = 0; v < m->nr_vertices * 4; v++) {
        jmax = max(jmax, joints[v]);
        if (jmax >= 100)
            return -1;
    }
    dbg("## max joints: %d\n", jmax);

    CHECK(m->joints = calloc(nr_joints, sizeof(struct model_joint)));
    for (j = 0; j < nr_joints; j++) {
        memcpy(&m->joints[j].invmx, invmxs[j], sizeof(mat4x4));
        darray_init(&m->joints[j].children);
    }

    shader_prog_use(m->prog);
    if (gl_does_vao())
        glBindVertexArray(m->vao);
    load_gl_buffer(m->prog->joints, joints, GL_UNSIGNED_BYTE, m->nr_vertices * 4,
                   &m->joints_obj, 4, GL_ARRAY_BUFFER);
    load_gl_buffer(m->prog->weights, weights, GL_FLOAT, m->nr_vertices * 4 * sizeof(float),
                   &m->weights_obj, 4, GL_ARRAY_BUFFER);
    if (gl_does_vao())
        glBindVertexArray(0);
    shader_prog_done(m->prog);

    m->nr_joints = nr_joints;
    return 0;
}

struct model3d *
model3d_new_from_vectors(const char *name, struct shader_prog *p, GLfloat *vx, size_t vxsz,
                         GLushort *idx, size_t idxsz, GLfloat *tx, size_t txsz,
                         GLfloat *norm, size_t normsz)
{
    struct model3d *m;

    m = ref_new(model3d);
    if (!m)
        return NULL;

    CHECK(m->name = strdup(name));
    m->prog = ref_get(p);
    m->cull_face = true;
    m->alpha_blend = false;
    m->draw_type = GL_TRIANGLES;
    model3d_calc_aabb(m, vx, vxsz);
    darray_init(&m->anis);

    if (gl_does_vao()) {
        GL(glGenVertexArrays(1, &m->vao));
        GL(glBindVertexArray(m->vao));
    }

    shader_prog_use(p);
    load_gl_buffer(m->prog->pos, vx, GL_FLOAT, vxsz, &m->vertex_obj, 3, GL_ARRAY_BUFFER);
    load_gl_buffer(-1, idx, GL_UNSIGNED_SHORT, idxsz, &m->index_obj[0], 0, GL_ELEMENT_ARRAY_BUFFER);
    m->nr_lods++;

    if (txsz)
        load_gl_buffer(m->prog->tex, tx, GL_FLOAT, txsz, &m->tex_obj, 2, GL_ARRAY_BUFFER);

    if (normsz)
        load_gl_buffer(m->prog->norm, norm, GL_FLOAT, normsz, &m->norm_obj, 3, GL_ARRAY_BUFFER);
    shader_prog_done(p);

    m->cur_lod = -1;
    m->nr_vertices = vxsz / sizeof(*vx) / 3; /* XXX: could be GLuint? */
    m->nr_faces[0] = idxsz / sizeof(*idx); /* XXX: could be GLuint? */
    /*dbg("created model '%s' vobj: %d iobj: %d nr_vertices: %d\n",
        m->name, m->vertex_obj, m->index_obj, m->nr_vertices);*/

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

    if (gl_does_vao())
        GL(glBindVertexArray(m->vao));
    shader_prog_use(m->prog);

    for (level = 0, nr_idx = mesh_nr_idx(mesh); level < LOD_MAX - 1; level++) {
        nr_idx = mesh_idx_to_lod(mesh, level, &lod, nr_idx);
        if (nr_idx < 0)
            break;
        dbg("lod%d for '%s' idx: %zd -> %zd\n", level, m->name, mesh_nr_idx(mesh), nr_idx);
        load_gl_buffer(-1, lod, GL_UNSIGNED_SHORT, nr_idx * mesh_idx_stride(mesh),
                       &m->index_obj[m->nr_lods], 0, GL_ELEMENT_ARRAY_BUFFER);
        free(lod);
        m->nr_faces[m->nr_lods] = nr_idx;
        m->nr_lods++;
    }

    shader_prog_done(m->prog);
    if (gl_does_vao())
        GL(glBindVertexArray(0));

    return m;
}

static void model3d_set_lod(struct model3d *m, unsigned int lod)
{
    if (lod >= m->nr_lods)
        lod = max(0, m->nr_lods - 1);

    if (lod == m->cur_lod)
        return;

    if (m->cur_lod < 0)
        GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->index_obj[lod]));
    m->cur_lod = lod;
}

static void model3d_prepare(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    if (gl_does_vao())
        GL(glBindVertexArray(m->vao));
    if (m->cur_lod >= 0)
        GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->index_obj[m->cur_lod]));
    GL(glBindBuffer(GL_ARRAY_BUFFER, m->vertex_obj));
    GL(glVertexAttribPointer(p->pos, 3, GL_FLOAT, GL_FALSE, 0, (void *)0));
    GL(glEnableVertexAttribArray(p->pos));

    if (m->norm_obj && p->norm >= 0) {
        GL(glBindBuffer(GL_ARRAY_BUFFER, m->norm_obj));
        GL(glVertexAttribPointer(p->norm, 3, GL_FLOAT, GL_FALSE, 0, (void *)0));
        GL(glEnableVertexAttribArray(p->norm));
    }
    if (m->tangent_obj && p->tangent >= 0) {
        GL(glBindBuffer(GL_ARRAY_BUFFER, m->tangent_obj));
        GL(glVertexAttribPointer(p->tangent, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
        GL(glEnableVertexAttribArray(p->tangent));
    }
    if (p->joints >= 0 && m->nr_joints && m->joints_obj >= 0) {
        GL(glBindBuffer(GL_ARRAY_BUFFER, m->joints_obj));
        GL(glVertexAttribPointer(p->joints, 4, GL_BYTE, GL_FALSE, 0, (void *)0));
        GL(glEnableVertexAttribArray(p->joints));
    }
    if (p->weights >= 0 && m->nr_joints && m->weights_obj >= 0) {
        GL(glBindBuffer(GL_ARRAY_BUFFER, m->weights_obj));
        GL(glVertexAttribPointer(p->weights, 4, GL_FLOAT, GL_FALSE, 0, (void *)0));
        GL(glEnableVertexAttribArray(p->weights));
    }
}

/* Cube and quad */
#include "primitives.c"

void model3dtx_prepare(struct model3dtx *txm)
{
    struct shader_prog *p = txm->model->prog;
    struct model3d *   m = txm->model;

    model3d_prepare(txm->model);

    if (p->tex >= 0 && m->tex_obj && texture_loaded(txm->texture)) {
        GL(glBindBuffer(GL_ARRAY_BUFFER, m->tex_obj));
        GL(glVertexAttribPointer(p->tex, 2, GL_FLOAT, GL_FALSE, 0, (void *)0));
        GL(glEnableVertexAttribArray(p->tex));
        GL(glActiveTexture(GL_TEXTURE0));
        GL(glBindTexture(GL_TEXTURE_2D, texture_id(txm->texture)));
        GL(glUniform1i(p->texture_map, 0));
    }

    if (p->normal_map >= 0 && txm->normals && texture_loaded(txm->normals)) {
        GL(glActiveTexture(GL_TEXTURE1));
        GL(glBindTexture(GL_TEXTURE_2D, texture_id(txm->normals)));
        GL(glUniform1i(p->normal_map, 1));
    }

    if (p->emission_map >= 0 && texture_loaded(txm->emission)) {
        GL(glActiveTexture(GL_TEXTURE2));
        GL(glBindTexture(GL_TEXTURE_2D, texture_id(txm->emission)));
        GL(glUniform1i(p->emission_map, 2));
    }

    if (p->sobel_tex >= 0 && texture_loaded(txm->sobel)) {
        GL(glActiveTexture(GL_TEXTURE3));
        GL(glBindTexture(GL_TEXTURE_2D, texture_id(txm->sobel)));
        GL(glUniform1i(p->sobel_tex, 3));
    }
}

void model3dtx_draw(struct model3dtx *txm)
{
    struct model3d *m = txm->model;

    /* GL_UNSIGNED_SHORT == typeof *indices */
    GL(glDrawElements(m->draw_type, m->nr_faces[m->cur_lod], GL_UNSIGNED_SHORT, 0));
}

static void model3d_done(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    GL(glDisableVertexAttribArray(p->pos));
    if (m->norm_obj && p->norm >= 0)
        GL(glDisableVertexAttribArray(p->norm));
    if (m->tangent_obj && p->tangent >= 0)
        GL(glDisableVertexAttribArray(p->tangent));
    if (m->nr_joints && p->joints >= 0) {
        GL(glDisableVertexAttribArray(p->joints));
        GL(glDisableVertexAttribArray(p->weights));
    }

    /* both need to be bound for glDrawElements() */
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    GL(glBindBuffer(GL_ARRAY_BUFFER, 0));
    if (gl_does_vao())
        GL(glBindVertexArray(0));
    m->cur_lod = -1;
}

void model3dtx_done(struct model3dtx *txm)
{
    struct shader_prog *p = txm->model->prog;

    if (p->tex >= 0 && txm->model->tex_obj) {
        GL(glDisableVertexAttribArray(p->tex));
        GL(glActiveTexture(GL_TEXTURE0));
        GL(glBindTexture(GL_TEXTURE_2D, 0));
    }
    if (txm->normals) {
        GL(glActiveTexture(GL_TEXTURE1));
        GL(glBindTexture(GL_TEXTURE_2D, 0));
    }

    model3d_done(txm->model);
}

static int fbo_create(void)
{
    unsigned int fbo;

    GL(glGenFramebuffers(1, &fbo));
    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    return fbo;
}

static void fbo_texture_init(struct fbo *fbo)
{
    texture_init(&fbo->tex);
    texture_filters(&fbo->tex, GL_CLAMP_TO_EDGE, GL_LINEAR);
    texture_fbo(&fbo->tex, GL_COLOR_ATTACHMENT0, GL_RGBA, fbo->width, fbo->height);
}

static int fbo_depth_texture(struct fbo *fbo)
{
    int tex;

    texture_init(&fbo->depth);
    texture_filters(&fbo->depth, GL_CLAMP_TO_EDGE, GL_LINEAR);
    texture_fbo(&fbo->depth, GL_DEPTH_ATTACHMENT, GL_DEPTH_COMPONENT, fbo->width, fbo->height);

    return tex;
}

static int fbo_color_buffer(struct fbo *fbo, int output)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    if (fbo->ms)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, fbo->width, fbo->height));
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + output, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

static int fbo_depth_buffer(struct fbo *fbo)
{
    unsigned int buf;

    GL(glGenRenderbuffers(1, &buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, buf));
    if (fbo->ms)
        GL(glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
    else
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
    GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, buf));
    GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

    return buf;
}

void fbo_resize(struct fbo *fbo, int width, int height)
{
    if (!fbo)
        return;
    fbo->width = width;
    fbo->height = height;
    GL(glFinish());
    texture_resize(&fbo->tex, width, height);
    texture_resize(&fbo->depth, width, height);

    if (fbo->depth_buf) {
        GL(glBindRenderbuffer(GL_RENDERBUFFER, fbo->depth_buf));
        GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, fbo->width, fbo->height));
        GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));
    }
}

#define NR_TARGETS 4
void fbo_prepare(struct fbo *fbo)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo->fbo));
    GL(glViewport(0, 0, fbo->width, fbo->height));
    if (fbo->ms) {
        GLenum buffers[NR_TARGETS];
        int target;

        for (target = 0; target < darray_count(fbo->color_buf); target++)
            buffers[target] = GL_COLOR_ATTACHMENT0 + target;
        GL(glDrawBuffers(darray_count(fbo->color_buf), buffers));
    }
}

void fbo_done(struct fbo *fbo, int width, int height)
{
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    GL(glViewport(0, 0, width, height));
}

static int fbo_make(struct ref *ref)
{
    struct fbo *fbo = container_of(ref, struct fbo, ref);

    darray_init(&fbo->color_buf);
    fbo->depth_buf = -1;
    fbo->ms        = false;

    return 0;
}

static void fbo_drop(struct ref *ref)
{
    struct fbo *fbo = container_of(ref, struct fbo, ref);

    // dbg("dropping FBO %d: %d/%d/%d\n", fbo->fbo, fbo->tex, fbo->depth_tex, fbo->depth_buf);
    GL(glDeleteFramebuffers(1, &fbo->fbo));
    /* if the texture was cloned, its ->loaded==false making this a nop */
    texture_deinit(&fbo->tex);

    int *color_buf;
    darray_for_each(color_buf, &fbo->color_buf)
        glDeleteRenderbuffers(1, (const GLuint *)color_buf);
    darray_clearout(&fbo->color_buf.da);

    // texture_done(&fbo->depth);
    if (fbo->depth_buf >= 0)
        GL(glDeleteRenderbuffers(1, (GLuint *)&fbo->depth_buf));
    // ref_free(fbo);
}
DECLARE_REFCLASS2(fbo);

static void fbo_init(struct fbo *fbo, int nr_targets)
{
    int err;

    fbo->fbo = fbo_create();
    if (fbo->ms) {
        int target;

        for (target = 0; target < nr_targets; target++) {
            int *color_buf = darray_add(&fbo->color_buf.da);
            *color_buf = fbo_color_buffer(fbo, target);
        }
    } else {
        fbo_texture_init(fbo);
    }
    //fbo->depth_tex = fbo_depth_texture(fbo);
    fbo->depth_buf = fbo_depth_buffer(fbo);
    err = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (err != GL_FRAMEBUFFER_COMPLETE)
        dbg("## framebuffer status: %d\n", err);
    GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

struct fbo *fbo_new_ms(int width, int height, bool ms, int nr_targets)
{
    struct fbo *fbo;
    int ret;

    CHECK(fbo = ref_new(fbo));
    fbo->width = width;
    fbo->height = height;
    fbo->ms = ms;
    fbo_init(fbo, nr_targets);

    return fbo;
}

struct fbo *fbo_new(int width, int height)
{
    return fbo_new_ms(width, height, false, 1);
}

static void animation_destroy(struct animation *an)
{
    free(an->channels);
    free((void *)an->name);
    ref_put(an->model);
}

struct animation *animation_new(struct model3d *model, const char *name, unsigned int nr_channels)
{
    struct animation *an;

    CHECK(an = darray_add(&model->anis.da));
    an->name = strdup(name);
    an->model = model;
    an->nr_channels = nr_channels;
    an->cur_channel = 0;
    CHECK(an->channels = calloc(an->nr_channels, sizeof(struct channel)));

    return an;
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

void models_render(struct mq *mq, struct light *light, struct camera *camera,
                   struct matrix4f *proj_mx, struct entity3d *focus, int width, int height,
                   unsigned long *count)
{
    struct entity3d *e;
    struct shader_prog *prog = NULL;
    struct model3d *model;
    struct model3dtx *txmodel;
    struct matrix4f *view_mx = NULL, *inv_view_mx = NULL;
    unsigned long nr_txms = 0, nr_ents = 0, culled = 0;

    if (camera) {
        view_mx = camera->view_mx;
        inv_view_mx = camera->inv_view_mx;
    }

    list_for_each_entry(txmodel, &mq->txmodels, entry) {
        // err_on(list_empty(&txmodel->entities), "txm '%s' has no entities\n",
        //        txmodel_name(txmodel));
        model = txmodel->model;
        model->cur_lod = 0;
        /* XXX: model-specific draw method */
        if (model->cull_face) {
            GL(glEnable(GL_CULL_FACE));
            GL(glCullFace(GL_BACK));
        } else {
            GL(glDisable(GL_CULL_FACE));
        }
        /* XXX: only for UIs */
        if (model->alpha_blend) {
            GL(glEnable(GL_BLEND));
            GL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        } else {
            GL(glDisable(GL_BLEND));
        }
        if (model->debug || !model->cull_face)
            GL(glDisable(GL_DEPTH_TEST));
        else
            GL(glEnable(GL_DEPTH_TEST));

        //dbg("rendering model '%s'\n", model->name);
        if (model->prog != prog) {
            if (prog)
                shader_prog_done(prog);

            prog = model->prog;
            shader_prog_use(prog);
            trace("rendering model '%s' using '%s'\n", model->name, prog->name);

            if (prog->data.width >= 0)
                GL(glUniform1f(prog->data.width, width));
            if (prog->data.height >= 0)
                GL(glUniform1f(prog->data.height, height));

            if (light && prog->data.lightp >= 0 && prog->data.lightc >= 0) {
                GL(glUniform3fv(prog->data.lightp, LIGHTS_MAX, light->pos));
                GL(glUniform3fv(prog->data.lightc, LIGHTS_MAX, light->color));
            }

            if (light && prog->data.attenuation >= 0)
                GL(glUniform3fv(prog->data.attenuation, LIGHTS_MAX, light->attenuation));

            if (view_mx && prog->data.viewmx >= 0)
                /* View matrix is the same for all entities and models */
                GL(glUniformMatrix4fv(prog->data.viewmx, 1, GL_FALSE, view_mx->cell));
            if (inv_view_mx && prog->data.inv_viewmx >= 0)
                GL(glUniformMatrix4fv(prog->data.inv_viewmx, 1, GL_FALSE, inv_view_mx->cell));

            /* Projection matrix is the same for everything, but changes on resize */
            if (proj_mx && prog->data.projmx >= 0)
                GL(glUniformMatrix4fv(prog->data.projmx, 1, GL_FALSE, proj_mx->cell));
        }

        model3dtx_prepare(txmodel);
        if (prog->data.use_normals >= 0 && txmodel->normals)
            GL(glUniform1f(prog->data.use_normals, texture_id(txmodel->normals) ? 1.0 : 0.0));

        if (prog->data.shine_damper >= 0 && prog->data.reflectivity >= 0) {
            GL(glUniform1f(prog->data.shine_damper, txmodel->roughness));
            GL(glUniform1f(prog->data.reflectivity, txmodel->metallic));
        }

        list_for_each_entry (e, &txmodel->entities, entry) {
            float hc[] = { 0.7, 0.7, 0.0, 1.0 }, nohc[] = { 0.0, 0.0, 0.0, 0.0 };
            if (!e->visible) {
                //dbg("skipping element of '%s'\n", entity_name(e));
                continue;
            }

            dbg_on(!e->visible, "rendering an invisible entity!\n");

            if (!e->skip_culling &&
                camera && !camera_entity_in_frustum(camera, e)) {
                culled++;
                continue;
            }

            if (camera && camera->ch) {
                vec3 dist = { e->dx, e->dy, e->dz };
                unsigned int lod;

                vec3_sub(dist, dist, camera->ch->pos);
                lod = vec3_len(dist) / 80;
                model3d_set_lod(model, lod);
            }
#ifndef EGL_EGL_PROTOTYPES
            if (focus == e) {
                GL(glPolygonMode(GL_FRONT_AND_BACK, GL_LINE));
            } else {
                GL(glPolygonMode(GL_FRONT_AND_BACK, GL_FILL));
            }
#endif
            if (prog->data.albedo_texture >= 0)
                GL(glUniform1i(prog->data.albedo_texture, !!e->priv)); /* e->priv now points to character */
            if (prog->data.entity_hash >= 0)
                GL(glUniform1i(prog->data.entity_hash, fletcher32((void *)&e, sizeof(e) / 2)));
            if (prog->data.color >= 0)
                GL(glUniform4fv(prog->data.color, 1, e->color));
            if (prog->data.colorpt >= 0)
                GL(glUniform1f(prog->data.colorpt, 0.5 * e->color_pt));
            if (focus && prog->data.highlight >= 0)
                GL(glUniform4fv(prog->data.highlight, 1, focus == e ? (GLfloat *)hc : (GLfloat *)nohc));

            if (model->nr_joints && model->anis.da.nr_el && prog->data.joint_transforms >= 0) {
                GL(glUniform1f(prog->data.use_skinning, 1.0));
                GL(glUniformMatrix4fv(prog->data.joint_transforms, model->nr_joints,
                                      GL_FALSE, (GLfloat *)e->joint_transforms));
            } else {
                GL(glUniform1f(prog->data.use_skinning, 0.0));
            }
            if (prog->data.ray >= 0) {
                vec3 ray = { 0, 0, 0 };
                if (focus) {
                    ray[0] = focus->dx;
                    ray[1] = focus->dz;
                    ray[2] = 1.0;
                }
                GL(glUniform3fv(prog->data.ray, 1, ray));
            }
            if (prog->data.transmx >= 0) {
                /* Transformation matrix is different for each entity */
                GL(glUniformMatrix4fv(prog->data.transmx, 1, GL_FALSE, (GLfloat *)e->mx));
            }

            model3dtx_draw(txmodel);
            nr_ents++;
        }
        model3dtx_done(txmodel);
        nr_txms++;
        //dbg("RENDERED model '%s': %lu\n", txmodel->model->name, nr_ents);
    }

    //dbg("RENDERED: %lu/%lu\n", nr_txms, nr_ents);
    if (count)
        *count = nr_txms;
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

static float quat_dot(quat a, quat b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static void vec3_interp(vec3 res, vec3 a, vec3 b, float fac)
{
    float rfac = 1.f - fac;

    res[0] = rfac * a[0] + fac * b[0];
    res[1] = rfac * a[1] + fac * b[1];
    res[2] = rfac * a[2] + fac * b[2];
}

static void quat_interp(quat res, quat a, quat b, float fac)
{
    float dot = quat_dot(a, b);
    float rfac = 1.f - fac;

    if (dot < 0) {
        res[3] = rfac * a[3] - fac * b[3];
        res[0] = rfac * a[0] - fac * b[0];
        res[1] = rfac * a[1] - fac * b[1];
        res[2] = rfac * a[2] - fac * b[2];
    } else {
        res[3] = rfac * a[3] + fac * b[3];
        res[0] = rfac * a[0] + fac * b[0];
        res[1] = rfac * a[1] + fac * b[1];
        res[2] = rfac * a[2] + fac * b[2];
    }
    quat_norm(res, res);
}

/*
 * Lifted verbatim from
 * https://nicedoc.io/KhronosGroup/glTF-Tutorials/blob/master/gltfTutorial/gltfTutorial_007_Animations.md#linear
 */
static void quat_slerp(quat res, quat a, quat b, float fac)
{
    float dot = quat_dot(a, b);
    quat _b;

    memcpy(_b, b, sizeof(_b));
    if (dot < 0.0) {
        int i;
        dot = -dot;
        for (i = 0; i < 4; i++) _b[i] = -b[i];
    }
    if (dot > 0.9995) {
        quat_interp(res, a, _b, fac);
        return;
    }

    float theta_0 = acos(dot);
    float theta = fac * theta_0;
    float sin_theta = sin(theta);
    float sin_theta_0 = sin(theta_0);

    float _rfac = cos(theta) - dot * sin_theta / sin_theta_0;
    float _fac = sin_theta / sin_theta_0;
    quat scaled_a, scaled_b;
    quat_scale(scaled_a, a, _rfac);
    quat_scale(scaled_b, _b, _fac);
    quat_add(res, scaled_a, scaled_b);
}

static void channel_transform(struct entity3d *e, struct channel *chan, float time)
{
    struct model3d *model = e->txmodel->model;
    void *p_data, *n_data;
    struct joint *joint = &e->joints[chan->target];
    float p_time, n_time, fac = 0;
    int prev, next;
    mat4x4 rot;

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

    darray_for_each(child, &model->joints[joint].children)
        one_joint_transform(e, *child, joint);
}

void animation_start(struct entity3d *e, unsigned long start_frame, int ani)
{
    struct model3d *model = e->txmodel->model;
    struct animation *an;
    struct channel *chan;
    int j, ch;

    if (!model->anis.da.nr_el)
        return;

    if (ani >= model->anis.da.nr_el)
        ani %= model->anis.da.nr_el;
    an = &model->anis.x[ani];
    for (ch = 0; ch < an->nr_channels; ch++) {
        chan = &an->channels[ch];
        e->joints[chan->target].off[chan->path] = 0;
    }
    e->ani_frame = start_frame;
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
        e->ani_frame = (long)s->frames_total - an->time_end * gl_refresh_rate() * drand48();
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
    animation_start(e, s->frames_total, qa->animation);
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

        darray_clearout(&e->aniq.da);

        if (qa)
            animation_end(&_qa, s);
    }
    qa = darray_add(&e->aniq.da);
    qa->animation = id;
    qa->repeat = repeat;
    qa->speed = 1.0;
    if (clear) {
        animation_start(e, s->frames_total, id);
        e->animation = 0;
        e->ani_cleared = true;
    }
}

static void animated_update(struct entity3d *e, struct scene *s)
{
    struct model3d *model = e->txmodel->model;
    struct queued_animation *qa;
    struct animation *an;
    unsigned long framerate = gl_refresh_rate();
    int i;

    if (e->animation < 0)
        animation_next(e, s);
    qa = ani_current(e);
    an = &model->anis.x[qa->animation];
    channels_transform(e, an, (float)(s->frames_total - e->ani_frame) / framerate * qa->speed);
    one_joint_transform(e, 0, -1);

    if ((float)(s->frames_total - e->ani_frame) * qa->speed >= an->time_end * framerate)
        animation_next(e, s);
}

static bool needs_update(struct entity3d *e)
{
    /*
     * Cache TRS data, this should cut out a lot of matrix multiplications
     * especially on stationaly entities.
     */
    if (e->dx != e->_dx ||
        e->dy != e->_dy ||
        e->dz != e->_dz ||
        e->rx != e->_rx ||
        e->ry != e->_ry ||
        e->rz != e->_rz ||
        e->scale != e->_scale) {
        e->_dx = e->dx;
        e->_dy = e->dy;
        e->_dz = e->dz;
        e->_rx = e->rx;
        e->_ry = e->ry;
        e->_rz = e->rz;
        e->_scale = e->scale;
        return true;
    }
    return false;
}

static int default_update(struct entity3d *e, void *data)
{
    struct scene *scene = data;

    if (needs_update(e)) {
        mat4x4_identity(e->mx->m);
        mat4x4_translate_in_place(e->mx->m, e->dx, e->dy, e->dz);
        mat4x4_rotate_X(e->mx->m, e->mx->m, e->rx);
        mat4x4_rotate_Y(e->mx->m, e->mx->m, e->ry);
        mat4x4_rotate_Z(e->mx->m, e->mx->m, e->rz);
        mat4x4_scale_aniso(e->mx->m, e->mx->m, e->scale, e->scale, e->scale);

        entity3d_aabb_update(e);

        if (e->light_idx >= 0) {
            float pos[3] = { e->dx + e->light_off[0], e->dy + e->light_off[1], e->dz + e->light_off[2] };
            light_set_pos(&scene->light, e->light_idx, pos);
        }
    }
    if (entity_animated(e))
        animated_update(e, scene);
    // if (e->phys_body)
    //    phys_debug_draw(scene, e->phys_body);

    return 0;
}

void entity3d_reset(struct entity3d *e)
{
    default_update(e, NULL);
}

static void entity3d_drop(struct ref *ref)
{
    struct entity3d *e = container_of(ref, struct entity3d, ref);
    trace("dropping entity3d\n");
    list_del(&e->entry);
    ref_put(e->txmodel);

    darray_clearout(&e->aniq.da);
    if (e->phys_body) {
        phys_body_done(e->phys_body);
        e->phys_body = NULL;
    }
    free(e->joints);
    free(e->joint_transforms);
    free(e->mx);
}

DECLARE_REFCLASS(entity3d);

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
    e->mx = mx_new();
    e->update  = default_update;
    entity3d_aabb_update(e);
    if (model->anis.da.nr_el) {
        CHECK(e->joints = calloc(model->nr_joints, sizeof(*e->joints)));
        CHECK(e->joint_transforms = calloc(model->nr_joints, sizeof(mat4x4)));
    }
    darray_init(&e->aniq);
    e->animation = -1;
    e->light_idx = -1;

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

void entity3d_add_physics(struct entity3d *e, double mass, int class, int type, double geom_off, double geom_radius, double geom_length)
{
    struct model3d *m = e->txmodel->model;

    e->phys_body = phys_body_new(phys, e, class, geom_radius, geom_off, type, mass);
}

void entity3d_position(struct entity3d *e, float x, float y, float z)
{
    e->dx = x;
    e->dy = y;
    e->dz = z;
    if (e->phys_body) {
        dBodySetPosition(e->phys_body->body, e->dx, e->dy + e->phys_body->yoffset, e->dz);
        // dBodySetLinearVel(e->phys_body->body, 0, 0, 0);
        // dBodySetAngularVel(e->phys_body->body, 0, 0, 0);
        //dBodyDisable(e->phys_body->body);
    }
}

void entity3d_move(struct entity3d *e, float dx, float dy, float dz)
{
    entity3d_position(e, e->dx + dx, e->dy + dy, e->dz + dz);
}

void model3dtx_add_entity(struct model3dtx *txm, struct entity3d *e)
{
    list_append(&txm->entities, &e->entry);
}

struct entity3d *instantiate_entity(struct model3dtx *txm, struct instantiator *instor,
                                    bool randomize_yrot, float randomize_scale, struct scene *scene)
{
    struct entity3d *e = entity3d_new(txm);
    e->scale = 1.0;
    e->dx = instor->dx;
    e->dy = instor->dy;
    e->dz = instor->dz;
    if (randomize_yrot)
        e->ry = drand48() * 360;
    if (randomize_scale)
        e->scale = 1 + randomize_scale * (1 - drand48() * 2);
    default_update(e, scene);
    e->update = default_update;
    e->visible = 1;
    model3dtx_add_entity(txm, e);
    return e;
}

void create_entities(struct model3dtx *txmodel)
{
    long i;

    return;
    for (i = 0; i < 16; i++) {
        struct entity3d *e = entity3d_new(txmodel);
        float a = 0, b = 0, c = 0;

        if (!e)
            return;

        a = (float)rand()*20 / (float)RAND_MAX;
        b = (float)rand()*20 / (float)RAND_MAX;
        c = (float)rand()*20 / (float)RAND_MAX;
        a *= i & 1 ? 1 : -1;
        b *= i & 2 ? 1 : -1;
        c *= i & 4 ? 1 : -1;
        //msg("[%f,%f,%f]\n", a, b, c);
        //mat4x4_identity(e->base_mx->m);
        //mat4x4_translate_in_place(e->base_mx->m, a, b, c);
        e->scale = 1.0;
        //mat4x4_scale_aniso(e->base_mx->m, e->base_mx->m, e->scale, e->scale, e->scale);
        /*mat4x4_scale(e->base_mx->m, e->base_mx->m, 0.05);*/

        e->dx      = a;
        e->dy      = b;
        e->dz      = c;
        //e->mx      = e->base_mx;
        default_update(e, NULL);
        e->update  = default_update;
        e->priv    = (void *)i;
        e->visible = 1;
        model3dtx_add_entity(txmodel, e);
    }
}

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
    m->debug = true;
    m->draw_type = GL_LINES;
    ref_put(p);

    CHECK(txm = ref_new(model3dtx));
    txm->model = m;
    list_init(&txm->entities);
    mq_add_model(&scene->mq, txm);
    CHECK(dd->entity = entity3d_new(txm));
    model3dtx_add_entity(txm, dd->entity);
    dd->entity->visible = 1;
    dd->entity->update = NULL;
    dd->entity->color_pt = COLOR_PT_ALL;
    dd->entity->color[0] = 1.0;
    dd->entity->color[3] = 1.0;
    if (rot)
        memcpy(dd->entity->mx->m, rot, sizeof(mat4x4));
    else
        mat4x4_identity(dd->entity->mx->m);
    list_append(&scene->debug_draws, &dd->entry);

    return dd;
}

struct debug_draw *__debug_draw_line(struct scene *scene, vec3 a, vec3 b, mat4x4 *rot)
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
    struct model3dtx *txmodel, *ittxm;
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
