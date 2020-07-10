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

    /* delete gl buffers */
    dbg("dropping model '%s'\n", m->name);
    free(m);
}

#ifdef CONFIG_BROWSER
#include <limits.h>
static unsigned char *fetch_png(const char *name, int *width, int *height)
{
    char path[PATH_MAX];
    snprintf(path, PATH_MAX, "/asset/%s", name);
    return /*(unsigned char *)*/emscripten_get_preloaded_image_data(path, width, height);
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

int model3d_add_texture(struct model3d *m, const char *name)
{
    int ret;

    shader_prog_use(m->prog);
    ret = load_gl_texture(m->prog, name, &m->texture_id);
    shader_prog_done(m->prog);

    return ret;
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

    shader_prog_use(p);
    load_gl_buffer(m->prog->pos, vx, vxsz, &m->vertex_obj, 3, GL_ARRAY_BUFFER);
    load_gl_buffer(-1, idx, idxsz, &m->index_obj, 0, GL_ELEMENT_ARRAY_BUFFER);

    if (txsz)
        load_gl_buffer(m->prog->tex, tx, txsz, &m->tex_obj, 2, GL_ARRAY_BUFFER);

    if (normsz)
        load_gl_buffer(m->prog->norm, norm, normsz, &m->norm_obj, 3, GL_ARRAY_BUFFER);
    shader_prog_done(p);

    m->nr_vertices = idxsz / sizeof(*idx); /* XXX: could be GLuint? */
    dbg("created model '%s' vobj: %d iobj: %d nr_vertices: %d\n",
        m->name, m->vertex_obj, m->index_obj, m->nr_vertices);

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

/* Cube and quad */
#include "primitives.c"

void model3d_prepare(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->index_obj);
    glBindBuffer(GL_ARRAY_BUFFER, m->vertex_obj);
    glVertexAttribPointer(p->pos, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(p->pos);

    if (m->tex_obj && m->texture_id) {
        glBindBuffer(GL_ARRAY_BUFFER, m->tex_obj);
        glVertexAttribPointer(p->tex, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
        glEnableVertexAttribArray(p->tex);
        glBindTexture(GL_TEXTURE_2D, m->texture_id);
    }
    if (m->norm_obj && p->norm >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m->norm_obj);
        glVertexAttribPointer(p->norm, 3, GL_FLOAT, GL_FALSE, 0, (void *)0);
        glEnableVertexAttribArray(p->norm);
    }
}

void model3d_draw(struct model3d *m)
{
    /* GL_UNSIGNED_SHORT == typeof *indices */
    glDrawElements(GL_TRIANGLES, m->nr_vertices, GL_UNSIGNED_SHORT, 0);
}

void model3d_done(struct model3d *m)
{
    struct shader_prog *p = m->prog;

    glDisableVertexAttribArray(p->pos);
    if (m->tex_obj && m->texture_id)
        glDisableVertexAttribArray(p->tex);
    if (m->norm_obj)
        glDisableVertexAttribArray(p->norm);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* both need to be bound for glDrawElements() */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void models_render(struct model3d *model, struct light *light, struct matrix4f *view_mx,
                   struct matrix4f *inv_view_mx, struct matrix4f *proj_mx, struct entity3d *focus)
{
    struct entity3d *e;
    struct shader_prog *prog = NULL;
    GLint viewmx_loc, transmx_loc, lightp_loc, lightc_loc, projmx_loc;
    GLint inv_viewmx_loc, shine_damper_loc, reflectivity_loc;
    GLint highlight_loc;
    int i;

    for (; model; model = model->next) {
        /* XXX: model-specific draw method */
        if (!strcmp(model->name, "terrain")) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
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
        model3d_prepare(model);
        for (i = 0, e = model->ent; e; e = e->next, i++) {
            float hc[] = { 0.7, 0.7, 0.0, 1.0 }, nohc[] = { 0.0, 0.0, 0.0, 0.0 };
            if (!e->visible)
                continue;

            if (focus && highlight_loc >= 0)
                glUniform4fv(highlight_loc, 1, focus == e ? (GLfloat *)hc : (GLfloat *)nohc);

            if (transmx_loc >= 0) {
                /* Transformation matrix is different for each entity */
                glUniformMatrix4fv(transmx_loc, 1, GL_FALSE, (GLfloat *)e->mx);
            }

            model3d_draw(model);
        }
        model3d_done(model);
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
    //model3d_add_texture(m, "purple.png");
    ref_put(&h->ref);

    scene_add_model(s, m);
    //create_entities(m);
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
    //model3d_add_texture(m, "purple.png");
    ref_put(&h->ref);

    scene_add_model(s, m);
    //create_entities(m);
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
    entity3d_free(e);
}

struct entity3d *entity3d_new(struct model3d *m)
{
    struct entity3d *e;

    e = ref_new(struct entity3d, ref, entity3d_drop);
    if (!e)
        return NULL;

    memset(e, 0, sizeof(*e));
    e->model = m; /* XXX: ref_get */
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

    //dbg("  => entity %d\n", i);
    mat4x4_identity(m);
    //if ()
    //mat4x4_rotate_X(m, m, (float)(scene->frames_total)/20);
    if (strcmp(e->model->name, "terrain"))
        mat4x4_rotate_X(m, m, to_radians((float)(scene->frames_total * i) / 50.0));
    //mat4x4_rotate_Z(m, m, to_radians((float)(scene->frames_total)));
    //if (strncmp(model->name, "ui_", 3)) {
    mat4x4_ortho(p, /*scene->aspect*/ 1.0, /*-scene->aspect*/ -1.0, -1.f, 1.f, 1.f, -1.f);
    //} else {
    //    mat4x4_identity(p);
    //}
    mat4x4_mul(mvp, m, p);
    mat4x4_mul(e->mx->m, e->base_mx->m, mvp);
    //mat4x4_mul(mvp, m, e->mx->m);
    return 0;
}

void create_entities(struct model3d *model)
{
    int i, max = 16;
    bool static_coords = false;

    if (!strcmp(model->name, "terrain")) {
        max = 1;
        static_coords = true;
    }
    
    if (!strncmp(model->name, "ui_", 3)) {
        max = 1;
        static_coords = true;
    }

    for (i = 0; i < max; i++) {
        struct entity3d *e = entity3d_new(model);
        //mat4x4 m, p;
        float a = 0, b = 0, c = 0;

        if (!e)
            return;

        /*mat4x4_identity(m);
        mat4x4_rotate_Z(m, m, (float)s->frames / 60);
        mat4x4_ortho(p, s->aspect, -s->aspect, -1.f, 1.f, 1.f, -1.f);
        mat4x4_mul(e->mx->m, p, m);*/
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
        /*entity3d_set_trans(e, a, b, c);*/
        //a = i ? (float)rand()*10 / (float)RAND_MAX : 1.0;
        //entity3d_set_scale(e, a);

        e->update  = silly_update;
        e->priv    = (void *)i;
        e->visible = 1;
        e->next    = model->ent;
        model->ent = e;
    }
}

