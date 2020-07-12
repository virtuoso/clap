#include <errno.h>
#include "model.h"
#include "shader.h"
#include "messagebus.h"
#include "ui.h"
#include "font.h"

static struct model3d *ui_quad;
static struct model3dtx *ui_quadtx;

static void ui_element_position(struct ui_element *uie, struct ui *ui)
{
    struct entity3d *e = uie->entity;
    float parent_width = ui->width, parent_height = ui->height;
    float x_off, y_off;

    if (uie->actual_x >= 0.0)
        return;

    if (uie->parent) {
        if (uie->parent->actual_x < 0.0)
            ui_element_position(uie->parent, ui);
        /*trace("parent: %f/%f/%f/%f\n",
              uie->parent->actual_x,
              uie->parent->actual_y,
              uie->parent->actual_w,
              uie->parent->actual_h
             );*/
        parent_width = uie->parent->actual_w;
        parent_height = uie->parent->actual_h;
    }

    x_off = uie->x_off < 1.0 ? uie->x_off * parent_width : uie->x_off;
    y_off = uie->y_off < 1.0 ? uie->y_off * parent_height : uie->y_off;
    uie->actual_w = uie->width < 1.0 ? uie->width * parent_width : uie->width;
    uie->actual_h = uie->height < 1.0 ? uie->height * parent_height : uie->height;
    if (uie->parent) {
        /* clamp child's w/h to parent's */
        uie->actual_w = min(uie->actual_w, parent_width - x_off);
        uie->actual_h = min(uie->actual_h, parent_height - y_off);
    }

    if (uie->affinity & UI_AF_TOP) {
        if (uie->affinity & UI_AF_BOTTOM) {
            /* ignore y_off: vertically centered */
            uie->actual_y = (parent_height - uie->actual_h) / 2;
        } else {
            uie->actual_y = parent_height - y_off - uie->actual_h;
        }
    } else if (uie->affinity & UI_AF_BOTTOM) {
        uie->actual_y = y_off;
    }

    if (uie->affinity & UI_AF_RIGHT) {
        if (uie->affinity & UI_AF_LEFT) {
            /* ignore x_off: horizontally centered */
            uie->actual_x = (parent_width - uie->actual_w) / 2;
        } else {
            uie->actual_x = parent_width - x_off - uie->actual_w;
        }
    } else if (uie->affinity & UI_AF_LEFT) {
        uie->actual_x = x_off;
    }

    if (uie->parent) {
        uie->actual_x += uie->parent->actual_x;
        uie->actual_y += uie->parent->actual_y;
    }

    /*trace("VIEWPORT %fx%f; xywh: %f %f %f %f\n", parent_width, parent_height,
          uie->actual_x, uie->actual_y, uie->actual_w, uie->actual_h);*/
    mat4x4_identity(e->base_mx->m);
    mat4x4_translate_in_place(e->base_mx->m, uie->actual_x, uie->actual_y, 0.0);
    if (!uie->prescaled)
        mat4x4_scale_aniso(e->base_mx->m, e->base_mx->m, uie->actual_w, uie->actual_h, 1.0);
}

static int ui_element_update(struct entity3d *e, void *data)
{
    struct ui *ui = data;
    mat4x4 p;

    ui_element_position(e->priv, ui);
    mat4x4_identity(p);
    mat4x4_ortho(p, 0, (float)ui->width, 0, (float)ui->height, 1.0, -1.0);
    mat4x4_mul(e->mx->m, p, e->base_mx->m);

    return 0;
}

void ui_update(struct ui *ui)
{
    struct model3dtx *txmodel;
    struct entity3d  *ent;
    int              i;

    list_for_each_entry(txmodel, &ui->txmodels, entry) {
        list_for_each_entry(ent, &txmodel->entities, entry) {
            struct ui_element *uie = ent->priv;
            uie->actual_x = uie->actual_y = uie->actual_w = uie->actual_h = -1.0;
        }
    }

    list_for_each_entry(txmodel, &ui->txmodels, entry) {
        list_for_each_entry(ent, &txmodel->entities, entry) {
            entity3d_update(ent, ui);
        }
    }
}

static void ui_element_drop(struct ref *ref)
{
    struct ui_element *uie = container_of(ref, struct ui_element, ref);

    ref_put(&uie->parent->ref);
    entity3d_put(uie->entity);
    free(uie);
}

struct ui_element *ui_element_new(struct ui *ui, struct ui_element *parent, struct model3dtx *txmodel,
                                  unsigned long affinity, float x_off, float y_off, float w, float h)
{
    struct ui_element *uie;
    struct entity3d *  e = entity3d_new(txmodel);

    if (!e)
        return NULL;

    uie = ref_new(struct ui_element, ref, ui_element_drop);
    uie->entity = e;
    if (parent)
        uie->parent = ref_get(parent);

    uie->affinity = affinity;
    uie->width    = w;
    uie->height   = h;
    uie->x_off    = x_off;
    uie->y_off    = y_off;

    //dbg("VIEWPORT: %ux%u; width %u -> %f; height %u -> %f\n", ui->width, ui->height, w, width, h, height);

    e->update  = ui_element_update;
    e->priv    = uie;
    e->visible = 1;

    list_append(&txmodel->entities, &e->entry);
    ui_element_position(uie, ui);

    return uie;
}

static void ui_add_model(struct ui *ui, struct model3dtx *txmodel)
{
    list_prepend(&ui->txmodels, &txmodel->entry);
}

static int ui_model_init(struct ui *ui)
{
    struct font *font = font_get_default();
    float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
    struct shader_prog *prog = shader_prog_find(ui->prog, "ui"); /* XXX */

    if (!prog) {
        err("couldn't load 'ui' shaders\n");
        return -EINVAL;
    }
    
    ui_quad = model3d_new_quad(prog, x, y, w, h);
    /* XXX: maybe a "textured_model" as another interim object */
    ui_quadtx = model3dtx_new(ui_quad, "green.png");
    //ui_quadtx->texture_id = font_get_texture(font, 'A');
    ui_add_model(ui, ui_quadtx);
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

struct ui_text {
    struct font         *font;
    struct ui_element   **uies;
    struct model3dtx    **txms;
    unsigned int        nr_uies;
    struct ref          ref;
};

static void ui_text_drop(struct ref *ref)
{
    struct ui_text *uit = container_of(ref, struct ui_text, ref);
    unsigned int    i;

    for (i = 0; i < uit->nr_uies; i++) {
        ref_put(&uit->uies[i]->ref);
        ref_put(&uit->txms[i]->ref);
    }
    free(uit->uies);
    free(uit->txms);
    free(uit);
}
void ui_render_string(struct ui *ui, struct font *font, struct ui_element *parent, char *str)
{
    size_t len = strlen(str);
    struct ui_text      *uit;
    struct shader_prog  *prog;
    struct model3d      *m;
    unsigned int        i, txid;
    float               x, y = 30, w, h;

    CHECK(uit       = ref_new(struct ui_text, ref, ui_text_drop));
    CHECK(prog      = shader_prog_find(ui->prog, "glyph"));
    CHECK(uit->uies = calloc(len, sizeof(struct ui_element *)));
    CHECK(uit->txms = calloc(len, sizeof(struct model3dtx *)));
    uit->font = font_get(font);
    for (i = 0, x = 0.0; i < len; i++) {
        w       = font_get_width(uit->font, str[i]);
        h       = font_get_height(uit->font, str[i]);
        txid    = font_get_texture(uit->font, str[i]);
        m       = model3d_new_quad(prog, 0, 0, w, h);
        m->name = "glyph";
        m->cull_face = false;
        m->alpha_blend = true;
        uit->txms[i] = model3dtx_new_txid(m, txid);
        /* txm holds the only reference */
        ref_put(&m->ref);
        ui_add_model(ui, uit->txms[i]);
        uit->uies[i] = ui_element_new(ui, parent, uit->txms[i], UI_AF_TOP | UI_AF_LEFT,
                                      x/* + font_get_advance_x(uit->font, str[i])*/,
                                      y/* + font_get_advance_y(uit->font, str[i])*/, w, h);
        //dbg("'%c' at %f//%f/%f\n", str[i], x, w, h);
        uit->uies[i]->entity->color[0] = 0.0 + (float)i/5;
        uit->uies[i]->entity->color[1] = 0.2 + (float)i/5;
        uit->uies[i]->entity->color[2] = 0.4 + (float)i/5;
        uit->uies[i]->entity->color[3] = 1.0;//   -(float)i / 5;
        uit->uies[i]->prescaled = true;
        x += w;
    }
}

int ui_init(struct ui *ui, int width, int height)
{
    struct ui_element *uie;
    struct font *font = font_get_default();

    list_init(&ui->txmodels);
    lib_request_shaders("glyph", &ui->prog);
    lib_request_shaders("ui", &ui->prog);

    //font = font_open("Pixellettersfull-BnJ5.ttf");
    ui_model_init(ui);
    uie = ui_element_new(ui, NULL, ui_quadtx, UI_AF_TOP    | UI_AF_RIGHT, 10, 10, 300, 100);
    uie = ui_element_new(ui, NULL, ui_quadtx, UI_AF_BOTTOM | UI_AF_RIGHT, 0.01, 50, 300, 100);
    ui_render_string(ui, font, uie, "POD pod");
    uie = ui_element_new(ui, NULL, ui_quadtx, UI_AF_TOP    | UI_AF_LEFT, 10, 10, 300, 100);
    ui_render_string(ui, font, uie, "TEST");
    uie = ui_element_new(ui, NULL, ui_quadtx, UI_AF_BOTTOM | UI_AF_LEFT, 0.01, 50, 100, 100);
    uie = ui_element_new(ui, uie,  ui_quadtx, UI_AF_CENTER, 0.05, 0.05, /*100, 100*/0.2, 0.2);
    ui_element_new(ui, NULL, ui_quadtx, UI_AF_HCENTER| UI_AF_BOTTOM, 5, 5, 0.99, 30);

    font_put(font);
    subscribe(MT_INPUT, ui_handle_input, ui);
    return 0;
}

void ui_show(struct ui *ui)
{
}
