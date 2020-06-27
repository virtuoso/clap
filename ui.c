#include <errno.h>
#include "model.h"
#include "shader.h"
#include "messagebus.h"
#include "ui.h"

static struct model3d *ui_quad;

static void ui_element_position(struct ui_element *uie, struct ui *ui)
{
    struct entity3d *e = uie->entity;
    float x = 0.0, y = 0.0, x_off, y_off, width, height;

    x_off = uie->x_off < 1.0 ? uie->x_off * (float)ui->width : uie->x_off;
    y_off = uie->y_off < 1.0 ? uie->y_off * (float)ui->height : uie->y_off;
    width = uie->width < 1.0 ? uie->width * (float)ui->width : uie->width;
    height = uie->height < 1.0 ? uie->height * (float)ui->width : uie->height;

    if (uie->affinity & UI_AF_TOP) {
        if (uie->affinity & UI_AF_BOTTOM) {
            /* ignore y_off: vertically centered */
            y = ((float)ui->height + height) / 2;
        } else {
            y = ui->height - y_off;
        }
    } else if (uie->affinity & UI_AF_BOTTOM) {
        y = y_off + height;
    }

    if (uie->affinity & UI_AF_RIGHT) {
        if (uie->affinity & UI_AF_LEFT) {
            /* ignore x_off: horizontally centered */
            x = ((float)ui->width + width) / 2;
        } else {
            x = ui->width - x_off;
        }
    } else if (uie->affinity & UI_AF_LEFT) {
        x = x_off + width;
    }

    //dbg("VIEWPORT %ux%u; xywh: %f %f %f %f\n", ui->width, ui->height, x, y, width, height);
    mat4x4_identity(e->base_mx->m);
    mat4x4_translate_in_place(e->base_mx->m, x, y, 0.0);
    mat4x4_scale_aniso(e->base_mx->m, e->base_mx->m, width, height, 1.0);
}

static int ui_element_update(struct entity3d *e, void *data)
{
    struct ui *ui = data;
    mat4x4 p;

    ui_element_position(e->priv, ui);
    //dbg("ui frame: %lu\n", ui->frames_total);
    mat4x4_identity(p);
    //mat4x4_ortho(p, -ui->aspect, ui->aspect, -1.0, 1.f, 1.f, -1.f);
    mat4x4_ortho(p, 0, (float)ui->width, 0, (float)ui->height, 1.0, -1.0);
    //mat4x4_rotate_Z(p, p, to_radians((float)(scene->frames_total) / 50.0));
    mat4x4_mul(e->mx->m, p, e->base_mx->m);

    return 0;
}

void ui_update(struct ui *ui)
{
    struct model3d * model;
    struct entity3d *ent;
    int              i;

    for (model = ui->model; model; model = model->next) {
        for (i = 0, ent = model->ent; ent; ent = ent->next, i++) {
            entity3d_update(ent, ui);
        }
    }
}

static void ui_element_drop(struct ref *ref)
{
    struct ui_element *uie = container_of(ref, struct ui_element, ref);

    //ref_put(uie->entity->ref);
    entity3d_put(uie->entity);
    free(uie);
}

static int ui_element_init(struct ui *ui, struct model3d *model, unsigned long affinity, float x_off, float y_off,
                           float w, float h)
{
    struct ui_element *uie;
    struct entity3d *  e = entity3d_new(model);

    if (!e)
        return -1;

    uie = ref_new(struct ui_element, ref, ui_element_drop);
    uie->entity = e;

    uie->affinity = affinity;
    uie->width    = w;
    uie->height   = h;
    uie->x_off    = x_off;
    uie->y_off    = y_off;

    //dbg("VIEWPORT: %ux%u; width %u -> %f; height %u -> %f\n", ui->width, ui->height, w, width, h, height);

    e->update  = ui_element_update;
    e->priv    = uie;
    e->visible = 1;
    e->next    = model->ent;
    model->ent = e;
    ui_element_position(uie, ui);

    return 0;
}

static void ui_add_model(struct ui *ui, struct model3d *model)
{
    model->next = ui->model;
    ui->model   = model;
}

static int ui_model_init(struct ui *ui)
{
    float x = -1.f, y = -1.f, w = 1.f, h = 1.f;
    GLfloat quad_vx[] = { x, y + h, 0.0, x, y, 0.0, x + w, y, 0.0, x + w, y + h, 0.0, };
    GLfloat quad_tx[]  = { 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0, };
    GLushort quad_idx[] = { 0, 1, 3, 3, 1, 2 };
    struct shader_prog *prog = shader_prog_find(ui->prog, "ui"); /* XXX */

    if (!prog) {
        err("couldn't load 'ui' shaders\n");
        return -EINVAL;
    }
    
    ui_quad = model3d_new_from_vectors("ui_quad", prog, quad_vx, sizeof(quad_vx),
                                     quad_idx, sizeof(quad_idx),
                                     quad_tx, sizeof(quad_tx), NULL, 0);
    /* XXX: maybe a "textured_model" as another interim object */
    model3d_add_texture(ui_quad, "hello.png");
    ui_add_model(ui, ui_quad);
    return 0;
}

static int ui_handle_input(struct message *m, void *data)
{
    struct ui *ui = data;

    if (m->input.resize) {
        //ui->width = m->input.x;
        //ui->height = m->input.y;
    }

    return 0;
}

int ui_init(struct ui *ui, int width, int height)
{
    lib_request_shaders("ui", &ui->prog);

    ui_model_init(ui);
    ui_element_init(ui, ui_quad, UI_AF_TOP    | UI_AF_RIGHT, 10, 10, 300, 100);
    ui_element_init(ui, ui_quad, UI_AF_BOTTOM | UI_AF_RIGHT, 0.01, 50, 300, 100);
    ui_element_init(ui, ui_quad, UI_AF_TOP    | UI_AF_LEFT, 10, 10, 300, 100);
    ui_element_init(ui, ui_quad, UI_AF_BOTTOM | UI_AF_LEFT, 0.01, 50, 100, 100);
    ui_element_init(ui, ui_quad, UI_AF_HCENTER| UI_AF_BOTTOM, 5, 5, 0.99, 30);
    //ui_element_init(ui, ui_quad, UI_AF_CENTER, 0.05, 0.05, 100, 100);

    subscribe(MT_INPUT, ui_handle_input, ui);
    return 0;
}

void ui_show(struct ui *ui)
{
}
