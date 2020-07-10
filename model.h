#ifndef __CLAP_MODEL_H__
#define __CLAP_MODEL_H__

#include "common.h"
#include "object.h"
#include "librarian.h"
#include "display.h" /* XXX: OpenGL headers are included there */
#include "objfile.h"

struct scene;
struct shader_prog;

struct light {
    GLfloat pos[3];
    GLfloat color[3];
};

struct model3d {
    const char          *name;
    struct ref          ref;
    struct shader_prog  *prog;
    GLuint              vertex_obj;
    GLuint              index_obj;
    GLuint              tex_obj;
    GLuint              norm_obj;
    GLuint              nr_vertices;
};

struct model3dtx {
    struct model3d *model;
    GLuint          texture_id;
    struct ref      ref;
    struct model3dtx    *next;
    struct entity3d     *ent;
};

struct model3d *model3d_new_from_vectors(const char *name, struct shader_prog *p, GLfloat *vx, size_t vxsz,
                                         GLushort *idx, size_t idxsz, GLfloat *tx, size_t txsz, GLfloat *norm,
                                         size_t normsz);
struct model3d *model3d_new_from_model_data(const char *name, struct shader_prog *p, struct model_data *md);
struct model3dtx *model3dtx_new(struct model3d *m, const char *name);
struct model3d *model3d_new_cube(struct shader_prog *p);
struct model3d *model3d_new_quad(struct shader_prog *p);
void model3dtx_prepare(struct model3dtx *m);
void model3dtx_done(struct model3dtx *m);
void model3dtx_draw(struct model3dtx *m);
void models_render(struct model3dtx *txmodel, struct light *light, struct matrix4f *view_mx, struct matrix4f *inv_view_mx,
                   struct matrix4f *proj_mx, struct entity3d *focus);
struct lib_handle *lib_request_obj(const char *name, struct scene *scene);
struct lib_handle *lib_request_bin_vec(const char *name, struct scene *scene);

static inline const char *txmodel_name(struct model3dtx *txm)
{
    return txm->model->name;
}

struct entity3d {
    struct model3dtx *txmodel;
    struct matrix4f  *mx;
    struct matrix4f  *base_mx;
    struct ref       ref;
    struct entity3d  *next;
    unsigned int     visible;
    GLfloat dx, dy, dz;
    GLfloat rx, ry, rz;
    GLfloat scale;
    int (*update)(struct entity3d *e, void *data);
    void *priv;
};

static inline const char *entity_name(struct entity3d *e)
{
    return txmodel_name(e->txmodel);
}

struct entity3d *entity3d_new(struct model3dtx *txm);
void entity3d_update(struct entity3d *e, void *data);
void entity3d_put(struct entity3d *e);
void create_entities(struct model3dtx *txmodel);

#endif /* __CLAP_MODEL_H__ */
