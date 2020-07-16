#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "librarian.h"
#include "common.h"
#include "matrix.h"
#include "util.h"
#include "object.h"
#include "model.h"
#include "shader.h"
#include "scene.h"

/****************************************************************************
 * model3d
 * the actual rendered model
 ****************************************************************************/

static void model3d_drop(struct ref *ref)
{
    struct model3d *m = container_of(ref, struct model3d, ref);

    glDeleteBuffers(1, &m->vertex_obj);
    glDeleteBuffers(1, &m->index_obj);
    if (m->norm_obj)
        glDeleteBuffers(1, &m->norm_obj);
    if (m->tex_obj)
        glDeleteBuffers(1, &m->tex_obj);
    /* delete gl buffers */
    ref_put(&m->prog->ref);
    trace("dropping model '%s'\n", m->name);
    free(m);
}

#ifdef CONFIG_BROWSER
#include <limits.h>
static unsigned char *fetch_png(const char *name, int *width, int *height)
{
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/asset/%s", name);
    return (unsigned char *)emscripten_get_preloaded_image_data(path, width, height);
}
#else
unsigned char *fetch_png(const char *file_name, int *width, int *height);
#endif

static int load_gl_texture(struct shader_prog *p, const char *name, GLuint *obj)
{
    int width = 0, height = 0;
    unsigned char *buffer = fetch_png(name, &width, &height);
    GLuint textureLoc = shader_prog_find_var(p, "tex");

    if (!buffer)
        return -EINVAL;

    //hexdump(buffer, 16);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, obj);
    glUniform1i(textureLoc, 0);

    // Bind it
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, *obj);

    // Load the texture from the image buffer
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(buffer);
    dbg("loaded texture %d %s %dx%d\n", *obj, name, width, height);

    return 0;
}

static int model3d_add_texture(struct model3dtx *txm, const char *name)
{
    int ret;

    shader_prog_use(txm->model->prog);
    ret = load_gl_texture(txm->model->prog, name, &txm->texture_id);
    shader_prog_done(txm->model->prog);

    return ret;
}

static void model3dtx_drop(struct ref *ref)
{
    struct model3dtx *txm = container_of(ref, struct model3dtx, ref);

    trace("dropping model3dtx [%s]\n", txm->model->name);
    list_del(&txm->entry);
    if (!txm->external_tex)
        glDeleteTextures(1, &txm->texture_id);
    ref_put(&txm->model->ref);
    free(txm);
}

struct model3dtx *model3dtx_new(struct model3d *model, const char *name)
{
    struct model3dtx *txm = ref_new(struct model3dtx, ref, model3dtx_drop);

    if (!txm)
        return NULL;

    txm->model = ref_get(model);
    list_init(&txm->entities);
    model3d_add_texture(txm, name);

    return txm;
}

struct model3dtx *model3dtx_new_txid(struct model3d *model, unsigned int txid)
{
    struct model3dtx *txm = ref_new(struct model3dtx, ref, model3dtx_drop);

    if (!txm)
        return NULL;

    txm->model = ref_get(model);
    txm->texture_id = txid;
    txm->external_tex = true;
    list_init(&txm->entities);

    return txm;
}

static void load_gl_buffer(GLint loc, void *data, size_t sz, GLuint *obj, GLint nr_coords, GLenum target)
{
    glGenBuffers(1, obj);
    glBindBuffer(target, *obj);
    glBufferData(target, sz, data, GL_STATIC_DRAW);
    
    /*
     *   <attr number>
     *   <number of elements in a vector>
     *   <type>
     *   <is normalized?>
     *   <stride> for interleaving
     *   <offset>
     */
    if (loc >= 0)
        glVertexAttribPointer(loc, nr_coords, GL_FLOAT, GL_FALSE, 0, (void *)0);
    
    glBindBuffer(target, 0);
}

struct model3d *
model3d_new_from_vectors(const char *name, struct shader_prog *p, GLfloat *vx, size_t vxsz,
                         GLushort *idx, size_t idxsz, GLfloat *tx, size_t txsz,
                         GLfloat *norm, size_t normsz)
{
    struct model3d *m;

    m = ref_new(struct model3d, ref, model3d_drop);
    if (!m)
        return NULL;

    m->name = name;
    m->prog = ref_get(p);
    m->cull_face = true;
    m->alpha_blend = false;

    shader_prog_use(p);
    load_gl_buffer(m->prog->pos, vx, vxsz, &m->vertex_obj, 3, GL_ARRAY_BUFFER);
    load_gl_buffer(-1, idx, idxsz, &m->index_obj, 0, GL_ELEMENT_ARRAY_BUFFER);

    if (txsz)
        load_gl_buffer(m->prog->tex, tx, txsz, &m->tex_obj, 2, GL_ARRAY_BUFFER);

    if (normsz)
        load_gl_buffer(m->prog->norm, norm, normsz, &m->norm_obj, 3, GL_ARRAY_BUFFER);
    shader_prog_done(p);

    m->nr_vertices = idxsz / sizeof(*idx); /* XXX: could be GLuint? */
    /*dbg("created model '%s' vobj: %d iobj: %d nr_vertices: %d\n",
        m->name, m->vertex_obj, m->index_obj, m->nr_vertices);*/

    return m;
}

struct model3d *
model3d_new_from_model_data(const char *name, struct shader_prog *p, struct model_data *md)
{
    struct model3d *m;
    size_t vxsz, idxsz, txsz;
    GLfloat *tx, *norm;
    GLushort *idx;

    model_data_to_vectors(md, &tx, &txsz, &norm, &vxsz, &idx, &idxsz);
    /* now the easy part */
    m = model3d_new_from_vectors(name, p, md->v, vxsz, idx, idxsz, tx, txsz, norm, vxsz);
    model_data_free(md);
    free(tx);
    free(norm);
    free(idx);

    return m;
}

static void model3d_prepare(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->index_obj);
    glBindBuffer(GL_ARRAY_BUFFER, m->vertex_obj);
    glVertexAttribPointer(p->pos, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(p->pos);

    if (m->norm_obj && p->norm >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m->norm_obj);
        glVertexAttribPointer(p->norm, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
        glEnableVertexAttribArray(p->norm);
    }
}

/* Cube and quad */
#include "primitives.c"

void model3dtx_prepare(struct model3dtx *txm)
{
    struct shader_prog *p = txm->model->prog;
    struct model3d *    m = txm->model;

    model3d_prepare(txm->model);

    if (m->tex_obj && txm->texture_id) {
        glBindBuffer(GL_ARRAY_BUFFER, m->tex_obj);
        glVertexAttribPointer(p->tex, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
        glEnableVertexAttribArray(p->tex);
        glBindTexture(GL_TEXTURE_2D, txm->texture_id);
    }
}

void model3dtx_draw(struct model3dtx *txm)
{
    /* GL_UNSIGNED_SHORT == typeof *indices */
    glDrawElements(GL_TRIANGLES, txm->model->nr_vertices, GL_UNSIGNED_SHORT, 0);
}

static void model3d_done(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    glDisableVertexAttribArray(p->pos);
    if (m->norm_obj)
        glDisableVertexAttribArray(p->norm);

    /* both need to be bound for glDrawElements() */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void model3dtx_done(struct model3dtx *txm)
{
    struct shader_prog *p = txm->model->prog;

    if (txm->model->tex_obj && txm->texture_id) {
        glDisableVertexAttribArray(p->tex);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    model3d_done(txm->model);
}

void models_render(struct list *list, struct light *light, struct matrix4f *view_mx,
                   struct matrix4f *inv_view_mx, struct matrix4f *proj_mx, struct entity3d *focus)
{
    struct entity3d *e;
    struct shader_prog *prog = NULL;
    struct model3d *model;
    struct model3dtx *txmodel;
    GLint viewmx_loc, transmx_loc, lightp_loc, lightc_loc, projmx_loc;
    GLint inv_viewmx_loc, shine_damper_loc, reflectivity_loc;
    GLint highlight_loc, color_loc;
    int i;

    list_for_each_entry(txmodel, list, entry) {
        model = txmodel->model;
        /* XXX: model-specific draw method */
        if (model->cull_face) {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        /* XXX: only for UIs */
        if (model->alpha_blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glDisable(GL_BLEND);
        }
        //dbg("rendering model '%s'\n", model->name);
        if (model->prog != prog) {
            if (prog)
                shader_prog_done(prog);

            prog = model->prog;
            shader_prog_use(prog);
            trace("rendering model '%s' using '%s'\n", model->name, prog->name);

            /* XXX: factor out */
            projmx_loc       = shader_prog_find_var(prog, "proj");
            viewmx_loc       = shader_prog_find_var(prog, "view");
            transmx_loc      = shader_prog_find_var(prog, "trans");
            inv_viewmx_loc   = shader_prog_find_var(prog, "inverse_view");
            lightp_loc       = shader_prog_find_var(prog, "light_pos");
            lightc_loc       = shader_prog_find_var(prog, "light_color");
            shine_damper_loc = shader_prog_find_var(prog, "shine_damper");
            reflectivity_loc = shader_prog_find_var(prog, "reflectivity");
            highlight_loc    = shader_prog_find_var(prog, "highlight_color");
            color_loc        = shader_prog_find_var(prog, "color");

            /* XXX: entity properties */
            if (shine_damper_loc >= 0 && reflectivity_loc >= 0) {
                glUniform1f(shine_damper_loc, 10.0);
                glUniform1f(reflectivity_loc, 0.7);
            }

            if (light && lightp_loc >= 0 && lightc_loc >= 0) {
                glUniform3fv(lightp_loc, 1, light->pos);
                glUniform3fv(lightc_loc, 1, light->color);
            }

            if (view_mx && viewmx_loc >= 0)
                /* View matrix is the same for all entities and models */
                glUniformMatrix4fv(viewmx_loc, 1, GL_FALSE, view_mx->cell);
            if (inv_view_mx && inv_viewmx_loc >= 0)
                 glUniformMatrix4fv(inv_viewmx_loc, 1, GL_FALSE, inv_view_mx->cell);

            /* Projection matrix is the same for everything, but changes on resize */
            if (proj_mx && projmx_loc >= 0)
                glUniformMatrix4fv(projmx_loc, 1, GL_FALSE, proj_mx->cell);
        }
        model3dtx_prepare(txmodel);

        i = 0;
        list_for_each_entry(e, &txmodel->entities, entry) {
            float hc[] = { 0.7, 0.7, 0.0, 1.0 }, nohc[] = { 0.0, 0.0, 0.0, 0.0 };
            if (!e->visible)
                continue;

            if (color_loc >= 0)
                glUniform4fv(color_loc, 1, e->color);
            if (focus && highlight_loc >= 0)
                glUniform4fv(highlight_loc, 1, focus == e ? (GLfloat *)hc : (GLfloat *)nohc);

            if (transmx_loc >= 0) {
                /* Transformation matrix is different for each entity */
                glUniformMatrix4fv(transmx_loc, 1, GL_FALSE, (GLfloat *)e->mx);
            }

            model3dtx_draw(txmodel);
        }
        model3dtx_done(txmodel);
    }

    shader_prog_done(prog);
}

static void model_obj_loaded(struct lib_handle *h, void *data)
{
    struct shader_prog *prog;
    struct scene *s = data;
    struct model_data * md;
    struct model3d *m;

    prog = shader_prog_find(s->prog, "model");
    dbg("loaded '%s' %p %zu %d\n", h->name, h->buf, h->size, h->state);
    if (!h->buf)
        return;

    md = model_data_new_from_obj(h->buf, h->size);
    if (!md)
        return;

    m = model3d_new_from_model_data(h->name, prog, md);
    ref_put(&h->ref);

    //scene_add_model(s, m);
    s->_model = m;
}

static void model_bin_vec_loaded(struct lib_handle *h, void *data)
{
    struct shader_prog *prog;
    struct scene *s = data;
    struct model3d *m;
    struct bin_vec_header *hdr = h->buf;
    float *vx, *tx, *norm;
    unsigned short *idx;

    prog = shader_prog_find(s->prog, "model");

    dbg("loaded '%s' nr_vertices: %lu\n", h->name, hdr->nr_vertices);
    vx   = h->buf + sizeof(*hdr);
    tx   = h->buf + sizeof(*hdr) + hdr->vxsz;
    norm = h->buf + sizeof(*hdr) + hdr->vxsz + hdr->txsz;
    idx  = h->buf + sizeof(*hdr) + hdr->vxsz + hdr->txsz + hdr->vxsz;
    m = model3d_new_from_vectors(h->name, prog, vx, hdr->vxsz, idx, hdr->idxsz, tx, hdr->txsz, norm, hdr->vxsz);
    ref_put(&h->ref);

    //scene_add_model(s, m);
    s->_model = m;
}

struct lib_handle *lib_request_obj(const char *name, struct scene *scene)
{
    struct lib_handle *lh;
    lh = lib_request(RES_ASSET, name, model_obj_loaded, scene);
    return lh;
}

struct lib_handle *lib_request_bin_vec(const char *name, struct scene *scene)
{
    struct lib_handle *lh;
    lh = lib_request(RES_ASSET, name, model_bin_vec_loaded, scene);
    return lh;
}

/*int model3d_new(const char *name)
{
    size_t size;
    char *buf;
    int ret;

    ret = lib_read_file(RES_ASSET, name, &buf, &size);
    if (ret)
        return ret;

    return 0;
}*/

/****************************************************************************
 * entity3d
 * instance of the model3d
 ****************************************************************************/

static void entity3d_free(struct entity3d *e)
{
    // TODO: linked list?
    /*struct model3d *m = e->model;
    struct entity3d *iter;

    for (iter = m->ent; iter; iter = iter->next)
        if (iter == e) {

        }*/
    free(e);
}

static int default_update(struct entity3d *e, void *data)
{
    //struct scene *scene = data;
    mat4x4_identity(e->base_mx->m);
    mat4x4_translate_in_place(e->base_mx->m, e->dx, e->dy, e->dz);
    mat4x4_scale_aniso(e->base_mx->m, e->base_mx->m, e->scale, e->scale, e->scale);
    e->mx = e->base_mx;
    return 0;
}

static void entity3d_drop(struct ref *ref)
{
    struct entity3d *e = container_of(ref, struct entity3d, ref);
    trace("dropping entity3d\n");
    list_del(&e->entry);
    ref_put(&e->txmodel->ref);
    entity3d_free(e);
}

struct entity3d *entity3d_new(struct model3dtx *txm)
{
    struct entity3d *e;

    e = ref_new(struct entity3d, ref, entity3d_drop);
    if (!e)
        return NULL;

    e->txmodel = ref_get(txm);
    e->mx = mx_new();
    e->base_mx = mx_new();
    e->update  = default_update;

    return e;
}

/* XXX: static inline? via macro? */
void entity3d_put(struct entity3d *e)
{
    ref_put(&e->ref);
}

void entity3d_update(struct entity3d *e, void *data)
{
    if (e->update)
        e->update(e, data);
}

static int silly_update(struct entity3d *e, void *data)
{
    struct scene *scene = data;
    mat4x4 m, p, mvp;
    int    i = (int)e->priv;

    mat4x4_identity(m);
    mat4x4_ortho(p, /*scene->aspect*/ 1.0, /*-scene->aspect*/ -1.0, -1.f, 1.f, 1.f, -1.f);
    mat4x4_mul(mvp, m, p);
    mat4x4_mul(e->mx->m, e->base_mx->m, mvp);
    return 0;
}

void model3dtx_add_entity(struct model3dtx *txm, struct entity3d *e)
{
    list_append(&txm->entities, &e->entry);
}

void create_entities(struct model3dtx *txmodel)
{
    int i, max = 16;
    bool static_coords = false;
    struct model3d *model = txmodel->model;

    for (i = 0; i < max; i++) {
        struct entity3d *e = entity3d_new(txmodel);
        float a = 0, b = 0, c = 0;

        if (!e)
            return;

        if (!static_coords) {
            a = /*i ? */(float)rand()*20 / (float)RAND_MAX/* : 0.0*/;
            b = /*i ? */(float)rand()*20 / (float)RAND_MAX/* : 0.0*/;
            c = /*i ? */(float)rand()*20 / (float)RAND_MAX/* : 0.0*/;
            a *= i & 1 ? 1 : -1;
            b *= i & 2 ? 1 : -1;
            c *= i & 4 ? 1 : -1;
        }
        //msg("[%f,%f,%f]\n", a, b, c);
        mat4x4_identity(e->base_mx->m);
        mat4x4_translate_in_place(e->base_mx->m, a, b, c);
        if (!static_coords)
            mat4x4_scale(e->base_mx->m, e->base_mx->m, 0.05);

        e->update  = silly_update;
        e->priv    = (void *)i;
        e->visible = 1;
        model3dtx_add_entity(txmodel, e);
    }
}

