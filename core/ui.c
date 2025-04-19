// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>
#include <errno.h>
#include "error.h"
#include "model.h"
#include "primitives.h"
#include "render.h"
#include "shader.h"
#include "shader_constants.h"
#include "messagebus.h"
#include "input.h"
#include "ui.h"
#include "font.h"
#include "render.h"
#include "ui-debug.h"

model3dtx *ui_quadtx;

static texture_t *ui_fbo_tex; /* XXX */
static model3dtx *ui_pip;

static bool __ui_element_is_visible(struct ui_element *uie, struct ui *ui)
{
    if (uie->affinity & UI_SZ_NORES)
        return true;
    if (uie->actual_x + uie->actual_w < 0)
        return false;
    if (uie->actual_x > ui->width)
        return false;
    if (uie->actual_y + uie->height < 0)
        return false;
    if (uie->actual_y > ui->height)
        return false;
    if (uie->force_hidden)
        return false;

    return true;
}

static void ui_element_position(struct ui_element *uie, struct ui *ui)
{
    entity3d *e = uie->entity;
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

    x_off = uie->x_off < 1.0 && uie->x_off > 0 ? uie->x_off * parent_width : uie->x_off;
    y_off = uie->y_off < 1.0 && uie->y_off > 0 ? uie->y_off * parent_height : uie->y_off;
    uie->actual_w = uie->width < 1.0 ? uie->width * parent_width : uie->width;
    uie->actual_h = uie->height < 1.0 ? uie->height * parent_height : uie->height;
    if (uie->parent && !(uie->affinity & UI_SZ_NORES)) {
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

    /* We might want force_invisible also */
    e->visible = __ui_element_is_visible(uie, ui) ? 1 : 0;
    /*trace("VIEWPORT %fx%f; xywh: %f %f %f %f\n", parent_width, parent_height,
          uie->actual_x, uie->actual_y, uie->actual_w, uie->actual_h);*/
    mat4x4_identity(e->mx);
    mat4x4_translate_in_place(e->mx, uie->actual_x, uie->actual_y, 0.0);
    //dbg("## positioning '%s' at %f,%f\n", entity_name(e), uie->actual_x, uie->actual_y);
    if (!uie->prescaled)
        mat4x4_scale_aniso(e->mx, e->mx, uie->actual_w, uie->actual_h, 1.0);
}

int ui_element_update(entity3d *e, void *data)
{
    struct ui_element *uie = e->priv;
    struct ui *ui = uie->ui;
    mat4x4 p;

    ui_element_position(uie, ui);
    if (!e->visible)
        return 0;

    mat4x4_identity(p);
    mat4x4_ortho(p, 0, (float)ui->width, 0, (float)ui->height, 1.0, -1.0);
    mat4x4_mul(e->mx, p, e->mx);

    return 0;
}

static void ui_reset_positioning(entity3d *e, void *data)
{
    struct ui_element *uie = e->priv;
    /* XXX */
    uie->actual_x = uie->actual_y = uie->actual_w = uie->actual_h = -1.0;
}

static void ui_roll_done(void);
static bool ui_roll_finished;

static void widgets_cleanup(struct list *list)
{
    struct ui_widget *widget, *iter;
    list_for_each_entry_iter(widget, iter, list, entry)
        ref_put_last(widget);
}

void ui_update(struct ui *ui)
{
    ui->time = clap_get_current_time(ui->clap_ctx);

    ui_debug_update(ui);
    ui_debug_selector();

    /* XXX: this is double for_each, make better */
    mq_for_each(&ui->mq, ui_reset_positioning, NULL);
    mq_update(&ui->mq);

    widgets_cleanup(&ui->widget_cleanup);

    if (ui_roll_finished)
        ui_roll_done();
}

static void ui_element_destroy(entity3d *e)
{
    struct ui_element *uie = e->priv;

    ref_put(uie);
}

static cerr ui_element_make(struct ref *ref, void *_opts)
{
    rc_init_opts(ui_element) *opts = _opts;

    if (!opts->ui || !opts->txmodel || (!opts->affinity && !opts->uwb->affinity))
        return CERR_INVALID_ARGUMENTS;

    struct ui_element *uie = container_of(ref, struct ui_element, ref);

    cresp(entity3d) res = ref_new_checked(entity3d, .txmodel = opts->txmodel);

    if (IS_CERR(res))
        return cerr_error_cres(res);

    entity3d *e = res.val;
    uie->entity = e;
    uie->entity->destroy = ui_element_destroy;
    uie->ui = opts->ui;
    if (opts->parent) {
        uie->parent = ref_get(opts->parent);
        uie->widget = opts->parent->widget;
        list_append(&opts->parent->children, &uie->child_entry);
    }

    /* Use ui_widget_builder to initialize the geometry */
    if (opts->uwb && !opts->uwb_root) {
        uie->affinity = opts->uwb->el_affinity;
        uie->width    = opts->uwb->el_w;
        uie->height   = opts->uwb->el_h;
        uie->x_off    = opts->uwb->el_x_off;
        uie->y_off    = opts->uwb->el_y_off;
    } else if (opts->uwb && opts->uwb_root) {
        uie->affinity = opts->uwb->affinity;
        uie->width    = opts->uwb->w;
        uie->height   = opts->uwb->h;
        uie->x_off    = opts->uwb->x_off;
        uie->y_off    = opts->uwb->y_off;
    }

    /*
     * But if individual fields are provided, the override
     * whatever came from the ui_widget_builder
     */
    if (opts->affinity)
        uie->affinity   = opts->affinity;
    if (opts->width)
        uie->width      = opts->width;
    if (opts->height)
        uie->height     = opts->height;
    if (opts->x_off)
        uie->x_off      = opts->x_off;
    if (opts->y_off)
        uie->y_off      = opts->y_off;

    list_init(&uie->children);
    list_init(&uie->animation);

    e->update  = ui_element_update;
    e->priv    = uie;
    e->visible = 1;
    entity3d_color(e, COLOR_PT_NONE, (vec4){});

    ui_element_position(uie, opts->ui);

    return CERR_OK;
}

static void ui_element_drop(struct ref *ref)
{
    struct ui_element *uie = container_of(ref, struct ui_element, ref);
    struct ui_element *child, *iter;

    trace("dropping ui_element\n");

    list_for_each_entry_iter(child, iter, &uie->children, child_entry) {
        list_del(&child->child_entry);
        child->parent = NULL;
        ref_put(child);
    }

    if (uie->parent) {
        list_del(&uie->child_entry);
        ref_put(uie->parent);
    }

    ui_element_animations_done(uie);
    err_on(!list_empty(&uie->children), "ui_element still has children\n");
    ref_put_last(uie->entity);
}

DEFINE_REFCLASS2(ui_element);

model3d *ui_quad_new(struct shader_prog *p, float x, float y, float w, float h)
{
    model3d *model = model3d_new_quadrev(p, x, y, 0, w, h);
    model->depth_testing = false;
    model->alpha_blend = true;
    return model;
}

void ui_add_model(struct ui *ui, model3dtx *txmodel)
{
    mq_add_model(&ui->mq, txmodel);
}

void ui_add_model_tail(struct ui *ui, model3dtx *txmodel)
{
    mq_add_model_tail(&ui->mq, txmodel);
}

static cerr ui_model_init(struct ui *ui)
{
    model3d *ui_quad = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
    ui_quad->alpha_blend = true;
    model3d_set_name(ui_quad, "ui_quad");
    ui_quadtx = ref_new(model3dtx, .model = ref_pass(ui_quad), .tex = transparent_pixel());
    if (!ui_quadtx)
        return CERR_INITIALIZATION_FAILED;

    ui_add_model_tail(ui, ui_quadtx);
    return CERR_OK;
}

model3dtx *ui_quadtx_get(void)
{
    return ui_quadtx;
}

struct ui_text {
    struct font         *font;
    const char          *str;
    struct ui_element   *uietex;
    unsigned long       flags;
    unsigned int        nr_uies;
    unsigned int        nr_lines;
    /* total width of all glyphs in each line, not counting whitespace */
    unsigned int        *line_w; /* array of int x nr_lines */
    /* width of one whitespace for each line */
    unsigned int        *line_ws; /* likewise */
    /* number of words in each line */
    unsigned int        *line_nrw; /* likewise */
    int                 width, height, y_off;
    int                 margin_x, margin_y;
};

/*
 * TODO:
 *  + optional reflow
 */
static void ui_text_measure(struct ui_text *uit)
{
    unsigned int i, w = 0, nr_words = 0, nonws_w = 0, ws_w;
    int h_top = 0, h_bottom = 0;
    size_t len = strlen(uit->str);
    struct glyph *glyph;

    mem_free(uit->line_nrw);
    mem_free(uit->line_ws);
    mem_free(uit->line_w);

    glyph = font_get_glyph(uit->font, '-');
    ws_w = glyph->width;
    for (i = 0; i <= len; i++) {
        if (uit->str[i] == '\n' || !uit->str[i]) { /* end of line */
            nr_words++;

            CHECK(uit->line_w = realloc(uit->line_w, sizeof(*uit->line_w) * (uit->nr_lines + 1)));
            CHECK(uit->line_ws = realloc(uit->line_ws, sizeof(*uit->line_ws) * (uit->nr_lines + 1)));
            CHECK(uit->line_nrw = realloc(uit->line_nrw, sizeof(*uit->line_nrw) * (uit->nr_lines + 1)));
            uit->line_w[uit->nr_lines] = nonws_w;// + ws_w * (nr_words - 1);
            uit->line_nrw[uit->nr_lines] = nr_words - 1;
            w = max(w, nonws_w + ws_w * (nr_words - 1));
            uit->nr_lines++;
            nonws_w = nr_words = 0;
            continue;
        }

        if (isspace(uit->str[i])) {
            nr_words++;
            continue;
        }

        glyph = font_get_glyph(uit->font, uit->str[i]);
        nonws_w += glyph->advance_x >> 6;
        if (glyph->bearing_y < 0) {
            h_top = max(h_top, (int)glyph->height + glyph->bearing_y);
            h_bottom = max(h_bottom, -glyph->bearing_y);
        } else {
            h_top = max(h_top, glyph->bearing_y);
            h_bottom = max(h_bottom, max((int)glyph->height - glyph->bearing_y, 0));
        }
    }

    for (i = 0; i < uit->nr_lines; i++)
        if ((uit->flags & UI_AF_VCENTER) == UI_AF_VCENTER)
            uit->line_ws[i] = uit->line_nrw[i] ? (w - uit->line_w[i]) / uit->line_nrw[i] : 0;
        else
            uit->line_ws[i] = ws_w;

    uit->width  = w;
    uit->y_off = h_top;
    uit->height = (h_top + h_bottom) * uit->nr_lines;
}

static inline int x_off(struct ui_text *uit, unsigned int line)
{
    int x = uit->margin_x;

    if (uit->flags & UI_AF_RIGHT) {
        if (uit->flags & UI_AF_LEFT) {
            if (uit->line_w[line])
               x += (uit->width - uit->line_w[line]) / 2;
        } else {
            x = uit->width + uit->margin_x - uit->line_w[line] -
                uit->line_ws[line] * uit->line_nrw[line];
        }
    }

    return x;
}

static model3dtx *ui_txm_find_by_texture(struct ui *ui, texture_t *tex)
{
    model3dtx *txmodel;

    /* XXX: need trees for better search, these lists are actually long */
    /* XXX^2: struct mq */
    list_for_each_entry(txmodel, &ui->mq.txmodels, entry) {
        /*
         * Since it's already on the list, the "extra" list
         * reference is already taken, the next ui element
         * to use it needs only its own reference.
         */
        if (texture_id(txmodel->texture) == texture_id(tex))
            return txmodel;
    }

    return NULL;
}

struct ui_element *ui_printf(struct ui *ui, struct font *font, struct ui_element *parent,
                             float *color, unsigned long flags, const char *fmt, ...)
{
    va_list ap;
    LOCAL(char, str);

    va_start(ap, fmt);
    cres(int) str_res = mem_vasprintf(&str, fmt, ap);
    va_end(ap);

    if (IS_CERR(str_res))
        return NULL;

    size_t              len = str_res.val;
    struct ui           fbo_ui;
    struct ui_element   **uies;
    model3dtx           *txm, *txmtex;
    fbo_t               *fbo;
    struct ui_text      uit = {};
    struct glyph        *glyph;
    model3d             *m;
    unsigned int        i, line;
    float               x, y;

    if (!flags)
        flags = UI_AF_VCENTER;

    uit.flags      = flags;
    uit.margin_x   = 10;
    uit.margin_y   = 10;
    uit.str = str;
    uit.font       = font;

    ui_text_measure(&uit);

    fbo_ui.width = uit.width + uit.margin_x * 2;
    fbo_ui.height = uit.height + uit.margin_y * 2;
    mq_init(&fbo_ui.mq, &fbo_ui);
    cresp(fbo_t) res = fbo_new(fbo_ui.width, fbo_ui.height);
    if (IS_CERR(res))
        return NULL;

    fbo = res.val;

    if (parent) {
        parent->width = uit.width + uit.margin_x * 2;
        parent->height = uit.height + uit.margin_y * 2;
        ui_element_position(parent, ui);
    }
    y = (float)uit.margin_y + uit.y_off;
    dbg_on(y < 0, "y: %f, height: %d y_off: %d, margin_y: %d\n",
           y, uit.height, uit.y_off, uit.margin_y);
    uies = mem_alloc(sizeof(struct ui_element *), .nr = len, .fatal_fail = 1);
    uit.nr_uies = len;
    for (line = 0, i = 0, x = x_off(&uit, line); i < len; i++) {
        if (str[i] == '\n') {
            line++;
            y += (uit.height / uit.nr_lines);
            x = x_off(&uit, line);
            continue;
        }
        if (isspace(str[i])) {
            x += uit.line_ws[line];
            continue;
        }
        glyph   = font_get_glyph(uit.font, str[i]);
        txm = ui_txm_find_by_texture(&fbo_ui, &glyph->tex);
        if (!txm) {
            m = ui_quad_new(ui->glyph_prog, 0, 0, glyph->width, glyph->height);
            model3d_set_name(m, "glyph_%s_%c", font_name(uit.font), str[i]);
            txm = ref_new(model3dtx, .model = ref_pass(m), .tex = &glyph->tex);
            ui_add_model(&fbo_ui, txm);
        }
        /* uies[i] consumes (holds the only reference to) txm */
        uies[i] = ref_new(ui_element,
                          .ui       = &fbo_ui,
                          .txmodel  = txm,
                          .affinity = UI_AF_TOP | UI_AF_LEFT,
                          .x_off    = x + glyph->bearing_x,
                          .y_off    = y - glyph->bearing_y,
                          .width    = glyph->width,
                          .height   = glyph->height);
        ref_only(uies[i]->entity);
        ref_only(uies[i]);
        entity3d_color(uies[i]->entity, COLOR_PT_ALL, color);

        uies[i]->prescaled = true;
        
        /* XXX: to trigger ui_element_position() XXX */
        uies[i]->actual_x = uies[i]->actual_y = -1;
        entity3d_update(uies[i]->entity, &fbo_ui);
        x += glyph->advance_x >> 6;
    }

    fbo_prepare(fbo);
    renderer_depth_test(ui->renderer, false);
    renderer_clearcolor(ui->renderer, (vec4){});
    renderer_clear(ui->renderer, true, true, false);
    models_render(ui->renderer, &fbo_ui.mq);
    mq_release(&fbo_ui.mq);
    fbo_done(fbo, ui->width, ui->height);

    mem_free(uies);
    mem_free(uit.line_nrw);
    mem_free(uit.line_ws);
    mem_free(uit.line_w);

    m = model3d_new_quad(ui->glyph_prog, 0, 1, 0, 1, -1);
    model3d_set_name(m, "ui_text: '%s'", str);
    m->depth_testing = false;
    m->alpha_blend = true;
    txmtex = ref_new(model3dtx, .model = ref_pass(m),
                     .tex = texture_clone(fbo_texture(fbo, FBO_COLOR_TEXTURE(0))));
    fbo_put_last(fbo);
    ui_add_model(ui, txmtex);

    uit.uietex = ref_new(ui_element,
                         .ui        = ui,
                         .parent    = parent,
                         .txmodel   = ref_pass(txmtex),
                         .affinity  = parent ? UI_AF_CENTER : UI_AF_HCENTER | UI_AF_BOTTOM,
                         .width     = fbo_ui.width,
                         .height    = fbo_ui.height);
    entity3d_color(uit.uietex->entity, COLOR_PT_ALPHA, (vec4){ 0, 0, 0, 1 });
    ref_only(uit.uietex->entity);
    ref_only(uit.uietex);

    return uit.uietex;
}

static const char *menu_font = "ofl/Unbounded-Regular.ttf";
static struct ui_element *ui_roll_element;

static void ui_roll_done(void)
{
    if (!ui_roll_element)
        return;

    ref_put_last(ui_roll_element);

    ui_roll_element = NULL;
}

static int ui_roll_update(entity3d *e, void *data)
{
    struct ui_element *uie = e->priv;
    struct ui *ui = uie->ui;

    if (uie->y_off == ui->height + uie->height) {
        dbg("credit roll done at %f\n", uie->y_off);
        ui_roll_finished = true;
        return 0;
    }
    uie->y_off++;
    ui_element_update(e, data);

    return 0;
}

static void ui_roll_init(struct ui *ui)
{
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    LOCAL(lib_handle, lh);
    LOCAL(char, buffer);
    struct font *font;
    size_t       size;

    lh = lib_read_file(RES_ASSET, "LICENSE", (void **)&buffer, &size);
    if (!lh)
        return;

    font = font_get_default(clap_get_font(ui->clap_ctx));
    if (!font) {
        ref_put_last(lh);
        return;
    }

    ui_roll_element = ui_printf(ui, font, NULL, color, UI_AF_HCENTER | UI_AF_BOTTOM | UI_SZ_NORES,
                                "%s", buffer);
    ui_roll_element->entity->update = ui_roll_update;
    ui_roll_element->y_off = -ui_roll_element->height;
    ui_element_position(ui_roll_element, ui);
    buffer = NULL;

    font_put(font);
}

static bool display_fps;
static struct ui_element *bottom_uit;
static struct ui_element *bottom_element;

static void ui_element_for_each_child(struct ui_element *uie, void (*cb)(struct ui_element *x, void *data), void *data)
{
    struct ui_element *child, *itc;

    list_for_each_entry_iter(child, itc, &uie->children, child_entry) {
        ui_element_for_each_child(child, cb, data);
    }
    cb(uie, data);
}

static void __set_visibility(struct ui_element *uie, void *data)
{
    uie->entity->visible = !!*(int *)data;
    uie->force_hidden = !*(int *)data;
}

void ui_element_set_visibility(struct ui_element *uie, int visible)
{
    ui_element_for_each_child(uie, __set_visibility, &visible);
}

static void __set_alpha(struct ui_element *uie, void *data)
{
    uie->entity->color[3] = *(float *)data;
}

void ui_element_set_alpha_one(struct ui_element *uie, float alpha)
{
    __set_alpha(uie, &alpha);
}

void ui_element_set_alpha(struct ui_element *uie, float alpha)
{
    ui_element_for_each_child(uie, __set_alpha, &alpha);
}

static void ui_menu_done(struct ui *ui);

static void do_debugs(struct ui *ui, const char *debug_name)
{
    ui_show_debug(debug_name);
    ui_menu_done(ui);
}

static const char *help_items[] = { "...license", "...help", "...credits" };
static const char *hud_items[] = { "FPS", };
static void menu_onclick(struct ui_element *uie, float x, float y)
{
    const char *str = uie->priv;
    struct ui *ui = uie->ui;

    if (!strcmp(str, "Help")) {
        ref_put_last(ui->menu);
        ui->menu = ui_menu_new(ui, help_items, array_size(help_items));
    } else if (!strcmp(str, "Exit")) {
        ui_menu_done(ui);
        display_request_exit();
    } else if (!strcmp(str, "HUD")) {
        ref_put_last(ui->menu);
        ui->menu = ui_menu_new(ui, hud_items, array_size(hud_items));
    } else if (!strcmp(str, "Monitor")) {
        ref_put_last(ui->menu);
        ui->menu = ui_debug_menu(ui);
    } else if (!strcmp(str, "Fullscreen")) {
        struct message_input mi;
        memset(&mi, 0, sizeof(mi));
        mi.fullscreen = 1;
        message_input_send(&mi, NULL);
    } else if (!strcmp(str, "FPS")) {
        if (display_fps) {
            display_fps = false;
            ref_put_last(bottom_uit);
            ref_put_last(bottom_element);
            bottom_uit = NULL;
        } else {
            display_fps = true;
        }
    } else if (!strcmp(str, "Devel")) {
        ui_toggle_debug_selector();
        ui_menu_done(ui); /* cancels modality */
    } else if (!strcmp(str, "...license")) {
        ui_roll_init(ui);
        ui_menu_done(ui); /* cancels modality */
    } else {
        do_debugs(ui, str);
    }
}

static void menu_onfocus(struct ui_element *uie, bool focus)
{
    if (focus)
        uia_lin_move(uie, UIE_MV_X_OFF, 1, 20, false, 1.0 / 6.0);
    else
        uia_lin_move(uie, UIE_MV_X_OFF, 20, 1, false, 1.0 / 6.0);
}

static void inv_onfocus(struct ui_element *uie, bool focus)
{
    int j;
    struct ui_element *current_item, *x;
    struct ui_widget *inv = uie->ui->inventory;
    long focused = (intptr_t)uie->priv;
    float focus_color[] = { 1.0, 0.0, 0.0, 1.0 };
    float non_focus_color[] = { 1.0, 1.0, 1.0, 1.0 };
    float *color;
    
    for (int i = 0; i < inv->nr_uies; i++) {
        current_item = inv->uies[i];
        if (i == focused)
            color = focus_color;
        else
            color = non_focus_color;
        j = 0;
        list_for_each_entry(x, &current_item->children, child_entry) {
            if (j == 0) // hack: frame is the first child.
                memcpy(x->entity->color, color, sizeof(focus_color));
            j++;
        }
    }
}

static void ui_element_children(struct ui_element *uie, struct list *list)
{
    struct ui_element *child, *iter;

    if (!uie)
        return;

    list_for_each_entry_iter(child, iter, &uie->children, child_entry)
        ui_element_children(child, list);
    list_del(&uie->child_entry);
    list_append(list, &uie->child_entry);
}

static cerr ui_widget_make(struct ref *ref, void *_opts)
{
    rc_init_opts(ui_widget) *opts = _opts;

    if (!opts->ui || !opts->uwb || !opts->nr_items)
        return CERR_INVALID_ARGUMENTS;

    struct ui_widget *uiw = container_of(ref, struct ui_widget, ref);

    uiw->uies = mem_alloc(sizeof(struct ui_element *), .nr = opts->nr_items);
    if (!uiw->uies)
        return CERR_NOMEM;

    /* XXX: render texts to FBOs to textures */
    cresp(ui_element) res = ref_new_checked(ui_element,
                                            .ui          = opts->ui,
                                            .txmodel     = ui_quadtx,
                                            .uwb         = opts->uwb,
                                            .uwb_root    = true);
    if (IS_CERR(res)) {
        mem_free(uiw->uies);
        return cerr_error_cres(res);
    }

    uiw->root = res.val;
    uiw->root->widget = uiw;
    uiw->nr_uies = opts->nr_items;
    list_append(&opts->ui->widgets, &uiw->entry);

    return CERR_OK;
}

static void ui_widget_drop(struct ref *ref)
{
    struct ui_widget *uiw = container_of(ref, struct ui_widget, ref);
    int i;

    for (i = 0; i < uiw->nr_uies; i++) {
        struct ui_element *uie, *iter;
        DECLARE_LIST(free_list);

        ui_element_children(uiw->uies[i], &free_list);
        list_for_each_entry_iter(uie, iter, &free_list, child_entry)
            ref_put(uie);
    }
    ref_put_last(uiw->root);
    list_del(&uiw->entry);
    mem_free(uiw->uies);
}

DEFINE_REFCLASS2(ui_widget);

void ui_widget_delete(struct ui_widget *widget)
{
    struct ui *ui = widget->root->ui;

    list_del(&widget->entry);
    list_append(&ui->widget_cleanup, &widget->entry);
}

static void __widget_delete_action(struct ui_animation *ua)
{
    struct ui_element *uie = ui_animation_element(ua);

    if (!uie->widget) {
        err("trying to delete an element without a widget: %s\n", entity_name(uie->entity));
        return;
    }

    ui_widget_delete(uie->widget);
}

void ui_widget_schedule_deletion(struct ui_element *uie)
{
    uia_action(uie, __widget_delete_action);
}

/*
 * TODO: move widgets into a separate compilation unit
 */
struct ui_widget *ui_wheel_new(struct ui *ui, const char **items)
{
    float quad_color[] = { 0.0, 0.3, 0.1, 1.0 };
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    unsigned long affs[] = {
        UI_AF_TOP     | UI_AF_HCENTER,
        UI_AF_VCENTER | UI_AF_RIGHT,
        UI_AF_BOTTOM  | UI_AF_HCENTER,
        UI_AF_VCENTER | UI_AF_LEFT,
    };
    enum uie_mv motions[] = {
        UIE_MV_Y_OFF,
        UIE_MV_X_OFF,
        UIE_MV_X_OFF,
        UIE_MV_Y_OFF,
    };
    float width, height;
    struct ui_widget *wheel;
    struct ui_element *tui;
    struct font *font;
    int i;

    /* XXX^0: affinity and placement hardcoded */
    cresp(ui_widget) res = ref_new_checked(ui_widget,
                                           .ui       = ui,
                                           .nr_items = 4,
                                           .uwb      = &(struct ui_widget_builder) {
                                                 .affinity   = UI_AF_VCENTER | UI_AF_HCENTER,
                                                 .w          = 0.3,
                                                 .h          = 0.3
                                           });

    if (IS_CERR(res))
        return NULL;

    wheel = res.val;
    wheel->focus   = -1;

    /* XXX^2: font global/hardcoded */
    font = ref_new(font, .ctx = clap_get_font(ui->clap_ctx), .name = "ProggyTiny.ttf", .size = 48);
    if (!font) {
        ref_put_last(wheel);
        return NULL;
    }

    for (i = 0, width = 0.0, height = 0.0; i < 4; i++) {
        /* XXX^3: element placement hardcoded */
        wheel->uies[i]           = ref_new(ui_element,
                                           .ui          = ui,
                                           .parent      = wheel->root,
                                           .txmodel     = ui_quadtx,
                                           .affinity    = affs[i],
                                           .width       = 300,
                                           .height      = 100);
        wheel->uies[i]->on_click = menu_onclick;
        wheel->uies[i]->on_focus = menu_onfocus;
        wheel->uies[i]->priv     = (void *)(uintptr_t)i;

        entity3d_color(wheel->uies[i]->entity, COLOR_PT_ALL, quad_color);
        /* XXX^5: animations hardcoded */
        // uia_skip_frames(wheel->uies[i], i * 7);
        uia_set_visible(wheel->uies[i], 1);
        uia_lin_float(wheel->uies[i], ui_element_set_alpha_one, 0, 1.0, false, 1.6);
        uia_cos_move(wheel->uies[i], motions[i], i < 2 ? 200 : 1, i < 2 ? 1 : 200, false, 0.5, 1.0, 0.0);

        CHECK(tui = ui_printf(ui, font, wheel->uies[i], color, 0, "%s", items[i]));
        width = max(width, wheel->uies[i]->width);
        height = max(height, wheel->uies[i]->height);
        ui_element_set_visibility(wheel->uies[i], 0);
    }
    for (i = 0; i < 4; i++) {
        wheel->uies[i]->width = width;
        wheel->uies[i]->height = height;
    }
    font_put(font);

    return wheel;
}

static void ui_osd_element_cb(struct ui_element *uie, unsigned int i)
{
    /*
     * the first skip_frames is relative to ui::frames_total;
     * the subsequent ones are relative to the previous skip_frame.
     *
     * 1 second to fade in, 2 seconds to stay, 1 second to fade out,
     * 1 second until the next one == 5 seconds per element.
     */
    uia_skip_duration(uie, 1.0 + i * 5.0);
    uia_set_visible(uie, 1);
    uia_lin_float(uie, ui_element_set_alpha, 0, 1, true, 1.0);
    uia_skip_duration(uie, 2.0);
    uia_lin_float(uie, ui_element_set_alpha, 1.0, 0.0, true, 1.0);
    uia_set_visible(uie, 0);
    if (i == uie->widget->nr_uies - 1)
        ui_widget_schedule_deletion(uie);
}

static struct ui_widget *
ui_osd_build(struct ui *ui, struct ui_widget_builder *uwb, const char **items, unsigned int nr_items)
{
    struct ui_widget *osd = ref_new(ui_widget, .ui = ui, .uwb = uwb, .nr_items = nr_items);
    struct ui_element *tui;
    int i;

    for (i = 0; i < nr_items; i++) {
        osd->uies[i]           = ref_new(ui_element,
                                         .ui        = ui,
                                         .parent    = osd->root,
                                         .txmodel   = ui_quadtx,
                                         .affinity  = uwb->el_affinity,
                                         .uwb       = uwb);
        osd->uies[i]->priv     = (void *)items[i];

        if (uwb->el_cb)
            uwb->el_cb(osd->uies[i], i);

        CHECK(tui = ui_printf(ui, uwb->font, osd->uies[i], uwb->text_color, 0, "%s", items[i]));
        ui_element_set_visibility(osd->uies[i], 0);
    }

    return osd;
}

struct ui_widget *ui_osd_new(struct ui *ui, const char **items, unsigned int nr_items)
{
    struct ui_widget *osd;
    struct ui_widget_builder uwb = {
        .el_affinity  = UI_AF_CENTER,
        .affinity   = UI_AF_BOTTOM | UI_AF_HCENTER,
        .el_x_off   = 10,
        .el_y_off   = 10,
        .el_w       = 500,
        .el_h       = 100,
        .el_margin  = 4,
        .x_off      = 0,
        .y_off      = 0,
        .w          = 500,
        .h          = 0.3,
        .el_cb      = ui_osd_element_cb,
        .el_color   = { 0.0, 0.0, 0.0, 0.0 },
        .text_color = { 0.8, 0.8, 0.8, 1.0 },
    };

    uwb.font = ref_new(font, .ctx = clap_get_font(ui->clap_ctx), .name = menu_font, .size = 32);
    if (!uwb.font)
        return NULL;

    osd = ui_osd_build(ui, &uwb, items, nr_items);
    font_put(uwb.font);

    return osd;
}

static void ui_menu_element_cb(struct ui_element *uie, unsigned int i)
{
    /* XXX^5: animations hardcoded */
    uia_skip_duration(uie, 0.12 * i);
    uia_set_visible(uie, 1);
    uia_lin_float(uie, ui_element_set_alpha, 0, 1.0, true, 0.5);
    uia_cos_move(uie, UIE_MV_X_OFF, 200, 1, false, 0.5, 1.0, 0.0);
}

static struct ui_widget *
ui_menu_build(struct ui *ui, struct ui_widget_builder *uwb, const char **items, unsigned int nr_items)
{
    struct ui_widget *menu = ref_new(ui_widget, .ui = ui, .uwb = uwb, .nr_items = nr_items);
    struct ui_element *tui;
    float off, width, height;
    int i;

    menu->focus = -1;

    for (i = 0, off = 0.0, width = 0.0, height = 0.0; i < nr_items; i++) {
        menu->uies[i]           = ref_new(ui_element,
                                          .ui       = ui,
                                          .parent   = menu->root,
                                          .txmodel  = ui_quadtx,
                                          .uwb      = uwb);
        menu->uies[i]->on_click = menu_onclick;
        menu->uies[i]->on_focus = menu_onfocus;
        menu->uies[i]->priv     = (void *)items[i];

        entity3d_color(menu->uies[i]->entity, COLOR_PT_ALL, uwb->el_color);

        if (uwb->el_cb)
            uwb->el_cb(menu->uies[i], i);

        CHECK(tui = ui_printf(ui, uwb->font, menu->uies[i], uwb->text_color, 0, "%s", items[i]));
        width = max(width, menu->uies[i]->width);
        height = max(height, menu->uies[i]->height);
        off += menu->uies[i]->height + uwb->el_margin;
        ui_element_set_visibility(menu->uies[i], 0);
    }

    for (i = 0; i < nr_items; i++) {
        menu->uies[i]->width = width;
        menu->uies[i]->height = height;
        if (i > 0)
            menu->uies[i]->y_off = uwb->el_y_off + (uwb->el_margin + height) * i;
    }

    return menu;
}

struct ui_widget *ui_menu_new(struct ui *ui, const char **items, unsigned int nr_items)
{
    struct ui_widget *menu;
    struct ui_widget_builder uwb = {
        .el_affinity  = UI_AF_TOP | UI_AF_RIGHT,
        .affinity   = UI_AF_VCENTER | UI_AF_RIGHT,
        .el_x_off   = 10,
        .el_y_off   = 10,
        .el_w       = 300,
        .el_h       = 100,
        .el_margin  = 4,
        .x_off      = 10,
        .y_off      = 10,
        .w          = 500,
        .h          = 0.8,
        .el_cb      = ui_menu_element_cb,
        .el_color   = { 0.52f, 0.12f, 0.12f, 1.0f },
        .text_color = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
    };

    uwb.font = ref_new(font, .ctx = clap_get_font(ui->clap_ctx), .name = menu_font, .size = 32);
    if (!uwb.font)
        return NULL;

    menu = ui_menu_build(ui, &uwb, items, nr_items);
    font_put(uwb.font);

    return menu;
}

static void ui_widget_pick_rel(struct ui_widget *uiw, int dpos)
{
    struct ui_element *uie;
    int new_focus;
    if (!dpos)
        return;

    /* out-of-focus animation */
    if (uiw->focus >= 0) {
        uie = uiw->uies[uiw->focus];
        if (uie->on_focus)
            uie->on_focus(uie, false);
    }
    new_focus = dpos + uiw->focus;
    if (new_focus < 0)
        new_focus = uiw->nr_uies - 1;
    else if (new_focus >= uiw->nr_uies)
        new_focus -= uiw->nr_uies;
    uiw->focus = new_focus;

    /* in-focus-animation */
    uie = uiw->uies[uiw->focus];
    if (uie->on_focus)
        uie->on_focus(uie, true);
}

static void ui_modality_send(void)
{
    struct message m;

    memset(&m, 0, sizeof(m));
    m.type = MT_COMMAND;
    m.cmd.toggle_modality = 1;
    message_send(&m);
}

static const char *menu_items[] = {
    "HUD",
#ifndef CONFIG_FINAL
    "Devel",
    "Monitor",
    "Fullscreen",
#endif
    "Help",
#ifndef CONFIG_BROWSER
    "Exit"
#endif
};
static void ui_menu_init(struct ui *ui)
{
    ui_modality_send();
    ui->menu = ui_menu_new(ui, menu_items, array_size(menu_items));
    /* XXX: should send a message to others that the UI owns inputs now */
    ui->modal = true;
}

static void ui_menu_done(struct ui *ui)
{
    ui_modality_send();
    ref_put(ui->menu);
    ui->menu = NULL;
    if (!ui->inventory)
        ui->modal = false;
}

static void inv_onclick(struct ui_element *uie, float x, float y)
{
    dbg("ignoring click on '%s'\n", entity_name(uie->entity));
}

void ui_inventory_init(struct ui *ui, int number_of_apples, float apple_ages[],
                       void (*on_click)(struct ui_element *uie, float x, float y))
{
    unsigned int rows = 3, cols = 3, nr_items = rows * cols, i;
    float color[] = { 0.5, 0.5, 0.4, 1.0 };
    struct ui_widget *inv;
    model3dtx *apple_txm = NULL, *frame_txm, *bar_txm = NULL;
    model3d *apple_m, *frame_m, *bar_m;
    struct ui_element *frame, *bar, *tui;
    struct font *font = font_get_default(clap_get_font(ui->clap_ctx));
    float xoff = 0, yoff = 0, width = 0;

    ui_modality_send();

    CHECK(inv = ref_new(ui_widget,
                        .ui         = ui,
                        .nr_items   = nr_items,
                        .uwb        = &(struct ui_widget_builder) {
                            .affinity   = UI_AF_VCENTER | UI_AF_HCENTER,
                            .w          = 0.3,
                            .h          = 0.3
                        }));
    inv->focus = -1;

    int number_of_immature_apples = 0;
    for (i = 0; i < number_of_apples; i++) {
        if (apple_ages[i] < 1.0)
            number_of_immature_apples++;
    }
    if (number_of_apples > 0) {
        apple_m = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
        model3d_set_name(apple_m, "inventory apple");
        apple_txm = ref_new(model3dtx, .model = ref_pass(apple_m), .texture_file_name = "apple.png");
        ui_add_model(ui, apple_txm);
    }
    if (number_of_immature_apples > 0) {
        bar_m = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
        model3d_set_name(bar_m, "inventory bar on immature apple");
        bar_m->alpha_blend = false;
        bar_txm = ref_new(model3dtx, .model = ref_pass(bar_m), .tex = white_pixel());
        ui_add_model(ui, bar_txm);
    }
    frame_m = model3d_new_frame(ui->ui_prog, 0, 0, 0.01, 1, 1, 0.02);
    model3d_set_name(frame_m, "inventory item frame");
    frame_m->alpha_blend = false;
    frame_txm = ref_new(model3dtx, .model = ref_pass(frame_m), .tex = white_pixel());
    ui_add_model(ui, frame_txm);

    width = 200;
    for (i = 0, xoff = 0, yoff = 0; i < nr_items; i++) {
        xoff = (i % cols) * (width + 10);
        yoff = (i / cols) * (width + 10);
        if (i < number_of_apples) {
            // drawing "apple"
            inv->uies[i] = ref_new(ui_element,
                                   .ui          = ui,
                                   .parent      = inv->root,
                                   .txmodel     = apple_txm,
                                   .affinity    = UI_AF_TOP | UI_AF_LEFT,
                                   .x_off       = xoff,
                                   .y_off       = yoff,
                                   .width       = 100,
                                   .height      = 100);
        } else {
            // drawing "empty"
            inv->uies[i] = ref_new(ui_element,
                                   .ui          = ui,
                                   .parent      = inv->root,
                                   .txmodel     = ui_quadtx,
                                   .affinity    = UI_AF_TOP | UI_AF_LEFT,
                                   .x_off       = xoff,
                                   .y_off       = yoff,
                                   .width       = 100,
                                   .height      = 100);
        }

        // frame must be the first child.
        CHECK(frame = ref_new(ui_element,
                              .ui       = ui,
                              .parent   = inv->uies[i],
                              .txmodel  = frame_txm,
                              .affinity = UI_AF_BOTTOM | UI_AF_LEFT,
                              .width    = 1,
                              .height   = 1));

        if (i < number_of_apples) {
            // "apple"
            inv->uies[i]->on_click = on_click;
            inv->uies[i]->on_focus = inv_onfocus;
            inv->uies[i]->priv = (void *)(uintptr_t)i;
            if (apple_ages[i] < 1.0) {
                // immature apple
                entity3d_color(inv->uies[i]->entity, COLOR_PT_ALPHA, (vec4){ 0.1, 0.5, 0.9, 0.3 });
            } else {
                // mature apple
                entity3d_color(inv->uies[i]->entity, COLOR_PT_NONE, (vec4){});
            }
            CHECK(tui = ui_printf(ui, font, inv->uies[i], color, 0, "apple"));
        } else {
            inv->uies[i]->on_click = inv_onclick;
            inv->uies[i]->on_focus = inv_onfocus;
            inv->uies[i]->priv = (void *)(uintptr_t)i;
            CHECK(tui = ui_printf(ui, font, inv->uies[i], color, 0, "empty"));
        }
        tui->entity->color_pt = COLOR_PT_NONE;
        if (i < number_of_apples) {
            if (apple_ages[i] < 1.0) {
                // immature apple
                CHECK(bar = ref_new(ui_element,
                                    .ui         = ui,
                                    .parent     = frame,
                                    .txmodel    = bar_txm,
                                    .affinity   = UI_AF_TOP | UI_AF_LEFT,
                                    .y_off      = 10,
                                    .width      = width * apple_ages[i],
                                    .height     = 5));
                entity3d_color(bar->entity, COLOR_PT_ALL, (vec4){ 0, 1, 0, 1 });
            }
        }
        entity3d_color(frame->entity, COLOR_PT_ALL, (vec4){ 1, 1, 1, 1 });
        
        // memcpy(&inv->uies[i]->entity->color, quad_color, sizeof(quad_color));
        // inv->uies[i]->entity->color_pt = COLOR_PT_ALL;

        /* XXX^5: animations hardcoded */
        inv->uies[i]->width = width;
        inv->uies[i]->height = width;
        frame->width = width;
        frame->height = width;

        //uia_skip_frames(inv->uies[i], i * 2);
        //uia_set_visible(inv->uies[i], 1);
        //uia_lin_float(inv->uies[i], ui_element_set_alpha, 0, 0.4, 20);
        //uia_cos_move(inv->uies[i], UIE_MV_X_OFF, 40, 1, 30, 1.0, 0.0);

        //ui_element_set_visibility(inv->uies[i], 0);
        //ui_element_set_visibility(frame, 0);
    }
    inv->root->width = width * cols + 10 * (cols - 1);
    inv->root->height = width * rows + 10 * (cols - 1);
    font_put(font);
    ui->inventory = inv;
    ui->modal = true;
}

void ui_inventory_done(struct ui *ui)
{
    dbg("bai\n");
    ui_modality_send();
    ref_put(ui->inventory);
    ui->inventory = NULL;
    if (!ui->menu)
        ui->modal = false;
}

static bool ui_element_within(struct ui_element *e, int x, int y)
{
    return x >= e->actual_x && x < e->actual_x + e->actual_w &&
           y >= e->actual_y && y < e->actual_y + e->actual_h;
}

static int ui_widget_within(struct ui_widget *uiw, int x, int y)
{
    struct ui_element *child;
    int i;

    for (i = 0; i < uiw->nr_uies; i++) {
        child = uiw->uies[i];

        if (ui_element_within(child, x, y))
            return i;
    }

    return -1;
}

static void ui_widget_hover(struct ui_widget *uiw, int x, int y)
{
    int n = ui_widget_within(uiw, x, y);

    if (n == uiw->focus)
        return;

    if (uiw->focus >= 0)
        uia_lin_move(uiw->uies[uiw->focus], UIE_MV_X_OFF, 20, 1, false, 1.0 / 6.0);
    if (n >= 0)
        uia_lin_move(uiw->uies[n], UIE_MV_X_OFF, 1, 20, false, 1.0 / 6.0);
    uiw->focus = n;
}

static void ui_menu_click(struct ui_widget *uiw, int x, int y)
{
    int n = ui_widget_within(uiw, x, y);
    struct ui_element *child;

    if (n < 0) {
        ui_menu_done(uiw->root->ui); /* cancels modality */
        return;
    }

    child = uiw->uies[n];
    child->on_click(child, (float)x - child->actual_x, (float)y - child->actual_y);
}

static int ui_handle_command(struct message *m, void *data)
{
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    struct ui *ui = data;
    struct font *font = font_get_default(clap_get_font(ui->clap_ctx));
    LOCAL(char, str);

    if (!font)
        return -1;

    if (m->type != MT_COMMAND)
        return 0;

    if (m->cmd.status && display_fps) {
        if (bottom_uit) {
            ref_put_last(bottom_uit);
        } else {
            bottom_element = ref_new(ui_element,
                                     .ui        = ui,
                                     .txmodel   = ui_quadtx,
                                     .affinity  = UI_AF_BOTTOM | UI_AF_RIGHT,
                                     .x_off     = 0.01,
                                     .y_off     = 50,
                                     .width     = 400,
                                     .height    = 150);
        }

        bottom_uit = ui_printf(ui, font, bottom_element, color, UI_AF_RIGHT,
                               "FPS: %d\nTime: %d:%02d", m->cmd.fps,
                               m->cmd.sys_seconds / 60, m->cmd.sys_seconds % 60);
    } else if (m->cmd.menu_enter) {
        ui_menu_init(ui);
    } else if (m->cmd.menu_exit) {
        ui_menu_done(ui);
    }
    font_put(font);

    return 0;
}

struct ui_element_match_struct {
    struct ui_element   *match;
    int                 x, y;
};

static void ui_element_match(entity3d *e, void *data)
{
    struct ui_element_match_struct *sd = data;
    struct ui_element *uie = e->priv;

    if (sd->match)
        return;

    if (ui_element_within(uie, sd->x, sd->y))
        sd->match = e->priv;
}

static bool ui_element_click(struct ui *ui, int x, int y)
{
    struct ui_element_match_struct sd = {
        .x = x,
        .y = (int)ui->height - y,
    };

    mq_for_each(&ui->mq, ui_element_match, &sd);
    if (sd.match && sd.match->on_click) {
        sd.match->on_click(sd.match, x - sd.match->x_off, y - sd.match->y_off);
        return true;
    }

    return false;
}

static int ui_handle_input(struct message *m, void *data)
{
    struct ui *ui = data;

    if (m->input.menu_toggle) {
        if (ui->menu)
            ui_menu_done(ui);
        else
            ui_menu_init(ui);
    } else if (m->input.mouse_click) {
        if (!ui->menu) {
            if (!ui_element_click(ui, m->input.x, m->input.y))
                ui_menu_init(ui);
        } else
            ui_menu_click(ui->menu, m->input.x, (int)ui->height - m->input.y);
    }

    if (!ui->modal)
        return MSG_HANDLED;

    if (ui->menu) {
        if (m->input.mouse_move)
            ui_widget_hover(ui->menu, m->input.x, (int)ui->height - m->input.y);
        
        /* UI owns the inputs */
        ui->mod_y += m->input.delta_ly;
        if (m->input.up == 1 || m->input.pitch_up == 1 || ui->mod_y <= -10) {
            // select previous
            ui->mod_y = 0;
            ui_widget_pick_rel(ui->menu, -1);
        } else if (m->input.down == 1 || m->input.pitch_down == 1 || ui->mod_y >= 10) {
            // select next
            ui->mod_y = 0;
            ui_widget_pick_rel(ui->menu, 1);
        } else if (m->input.left == 1 || m->input.yaw_left == 1 || m->input.delta_lx < -0.99 || m->input.back) {
            // go back
            ui_menu_done(ui);
        } else if (m->input.right == 1 || m->input.yaw_right == 1 || m->input.delta_lx > 0.99 || m->input.enter) {
            // enter
            if (ui->menu->focus >= 0)
                ui->menu->uies[ui->menu->focus]->on_click(ui->menu->uies[ui->menu->focus], 0, 0);
        }
    } else if (ui->inventory) {
        /* UI owns the inputs */
        ui->mod_y += m->input.delta_ly;
        ui->mod_x += m->input.delta_lx;
        if (m->input.up == 1 || m->input.pitch_up == 1 || ui->mod_y <= -100) {
            // up
            ui->mod_y = 0;
            ui_widget_pick_rel(ui->inventory, -3);
        } else if (m->input.down == 1 || m->input.pitch_down == 1 || ui->mod_y >= 100) {
            // down
            ui->mod_y = 0;
            ui_widget_pick_rel(ui->inventory, 3);
        } else if (m->input.left == 1 || m->input.yaw_left == 1 || ui->mod_x < 0) {
            // left
            ui->mod_x = 0;
            ui_widget_pick_rel(ui->inventory, -1);
        } else if (m->input.right == 1 || m->input.yaw_right == 1 || ui->mod_x > 0) {
            // right
            ui->mod_x = 0;
            ui_widget_pick_rel(ui->inventory, 1);            
        } else if (m->input.pad_y) {
            if (ui->inventory->focus >= 0)
                ui->inventory->uies[ui->inventory->focus]->on_click(ui->inventory->uies[ui->inventory->focus], 0, 0);
            ui_inventory_done(ui);
        }            
    }
    return MSG_STOP;
}

#ifndef CONFIG_FINAL
static struct ui_element *build_uit;
#endif /* CONFIG_FINAL */

struct ui_element *uie0, *uie1, *pocket, **pocket_text;
static int pocket_buckets, *pocket_count, *pocket_total;

void ui_pip_update(struct ui *ui, fbo_t *fbo)
{
    model3d *m;

    ui_fbo_tex = fbo_texture(fbo, FBO_COLOR_TEXTURE(0));

    if (ui_pip)
        ref_put(ui_pip);
    if (uie0)
        ref_put(uie0);

    m = ui_quad_new(ui->ui_prog, 0, 1, 1, -1);
    ui_pip = ref_new(model3dtx, .model = ref_pass(m), .tex = ui_fbo_tex);
    ui_add_model_tail(ui, ui_pip);
    dbg("### ui_pip tex: %d width: %d height: %d\n", texture_id(ui_fbo_tex), fbo_width(fbo), fbo_height(fbo));
    if (fbo_width(fbo) < fbo_height(fbo))
        uie0 = ref_new(ui_element,
                       .ui          = ui,
                       .txmodel     = ui_pip,
                       .affinity    = UI_AF_VCENTER | UI_AF_LEFT,
                       .width       = fbo_width(fbo),
                       .height      = fbo_height(fbo));
    else
        uie0 = ref_new(ui_element,
                       .ui          = ui,
                       .txmodel     = ui_pip,
                       .affinity    = UI_AF_TOP | UI_AF_HCENTER,
                       .width       = fbo_width(fbo),
                       .height      = fbo_height(fbo));
    entity3d_color(uie0->entity, COLOR_PT_NONE, (vec4){});
}

struct ui_element *ui_pocket_new(struct ui *ui, const char **tex, int nr)
{
    model3d *model;
    model3dtx *txm;
    struct ui_element *p, *t;
    struct font *font = ref_new(font,
                                .ctx  = clap_get_font(ui->clap_ctx),
                                .name = "ProggyTiny.ttf",
                                .size = 48);
    int i;

    if (!font)
        return NULL;

    pocket_text = mem_alloc(sizeof(struct ui_element *), .nr = nr, .fatal_fail = 1);
    pocket_count = mem_alloc(sizeof(int), .nr = nr, .fatal_fail = 1);
    pocket_total = mem_alloc(sizeof(int), .nr = nr, .fatal_fail = 1);

    p = ref_new(ui_element,
                .ui         = ui,
                .parent     = NULL,
                .txmodel    = ui_quadtx,
                .affinity   = UI_AF_TOP | UI_AF_RIGHT,
                .x_off      = 10,
                .y_off      = 10,
                .width      = 200,
                .height     = 100 * nr);
    for (i = 0; i < nr; i++) {
        model = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
        model3d_set_name(model, "ui_pocket_element");
        txm = ref_new(model3dtx, .model = ref_pass(model), .texture_file_name = tex[i]);
        if (!txm)
            continue;
        ui_add_model(ui, txm);

        (void)ref_new(ui_element,
                      .ui       = ui,
                      .parent   = p,
                      .txmodel  = txm,
                      .affinity = UI_AF_LEFT | UI_AF_TOP,
                      .y_off    = 100 * i,
                      .width    = 100,
                      .height   = 100);
        t = ref_new(ui_element,
                    .ui       = ui,
                    .parent   = p,
                    .txmodel  = ui_quadtx,
                    .affinity = UI_AF_LEFT | UI_AF_TOP,
                    .x_off    = 100,
                    .y_off    = 100 * i,
                    .width    = 100,
                    .height   = 100);
        pocket_text[i] = ui_printf(ui, font, t, (vec4){ 1, 1, 1, 1 }, UI_AF_LEFT | UI_AF_VCENTER,
                                   "%s", tex[i]);
    }
    ui_element_set_visibility(p, 0);
    font_put(font);
    pocket_buckets = nr;

    return p;
}

void show_apple_in_pocket()
{
    ui_element_set_visibility(pocket, 1);
}

void show_empty_pocket()
{
    ui_element_set_visibility(pocket, 0);
}

void pocket_update(struct ui *ui)
{
    struct ui_element *parent;
    float color[4] = { 1, 1, 1, 1 };
    struct font *font = ref_new(font,
                                .ctx  = clap_get_font(ui->clap_ctx),
                                .name = "ProggyTiny.ttf",
                                .size = 48);
    int i;

    if (!font)
        return;

    for (i = 0; i < pocket_buckets; i++) {
        parent = pocket_text[i]->parent;
        ref_put_last(pocket_text[i]);

        pocket_text[i] = ui_printf(ui, font, parent, color, UI_AF_LEFT | UI_AF_VCENTER,
                                   "x %d/%d", pocket_count[i], pocket_total[i]);
    }
    font_put(font);
}

void pocket_count_set(struct ui *ui, int kind, int count)
{
    pocket_count[kind] = count;
    pocket_update(ui);
}

void pocket_total_set(struct ui *ui, int kind, int total)
{
    pocket_total[kind] = total;
    pocket_update(ui);
}

cresp(ui_widget) _ui_progress_bar_new(struct ui *ui, const progress_bar_options *opts)
{
    if (!opts->width || !opts->height || !opts->affinity)
        return cresp_error(ui_widget, CERR_INVALID_ARGUMENTS);

    float bar_width = opts->width - 2 * opts->border;
    float bar_height = opts->height - 2 * opts->border;

    cresp(ui_widget) res    = ref_new_checked(ui_widget,
        .ui                 = ui,
        .nr_items           = 2,
        .uwb                = &(struct ui_widget_builder) {
            .affinity       = opts->affinity,
            .w              = opts->width,
            .h              = opts->height,
            .y_off          = opts->y_off,
        }
    );

    if (IS_CERR(res))
        return cresp_error_cerr(ui_widget, res);

    res.val->priv = (void *)(uintptr_t)bar_width;

    model3d *frame_m = model3d_new_frame(ui->ui_prog, 0, 0, 0, opts->width, opts->height, opts->border);
    frame_m->depth_testing = false;
    frame_m->alpha_blend = false;
    model3dtx *frame_txm = ref_new(model3dtx, .model = ref_pass(frame_m), .tex = white_pixel());
    ui_add_model(ui, frame_txm);

    model3d *bar_m = ui_quad_new(ui->ui_prog, 0, 0, 1, 1);
    model3dtx *bar_txm = ref_new(model3dtx, .model = ref_pass(bar_m), .tex = white_pixel());
    ui_add_model(ui, bar_txm);

    struct ui_widget *progress_bar = res.val;
    cresp(ui_element) uie_res = ref_new_checked(ui_element,
        .ui         = ui,
        .parent     = progress_bar->root,
        .txmodel    = bar_txm,
        .affinity   = UI_AF_VCENTER | UI_AF_LEFT,
        .x_off      = opts->border,
        .y_off      = opts->border,
        .width      = bar_width,
        .height     = bar_height,
    );
    if (IS_CERR(uie_res)) {
        ref_put(res.val);
        return cresp_error_cerr(ui_widget, uie_res);
    }

    progress_bar->uies[0] = uie_res.val;
    entity3d_color(progress_bar->uies[0]->entity, COLOR_PT_ALL,
                   opts->bar_color ? : (vec4){ 0, 0, 1, 1 });

    uie_res = ref_new_checked(ui_element,
        .ui         = ui,
        .parent     = progress_bar->root,
        .txmodel    = frame_txm,
        .affinity   = UI_AF_BOTTOM | UI_AF_LEFT,
        .width      = opts->width,
        .height     = opts->height,
    );
    if (IS_CERR(uie_res)) {
        ref_put(res.val);
        return cresp_error_cerr(ui_widget, uie_res);
    }

    progress_bar->uies[1] = uie_res.val;
    progress_bar->uies[1]->width = 1.0;
    progress_bar->uies[1]->height = 1.0;
    ui_element_position(progress_bar->uies[1], ui);
    entity3d_color(progress_bar->uies[1]->entity, COLOR_PT_ALL,
                   opts->border_color ? : (vec4){ 1, 1, 1, 1 });

    return cresp_val(ui_widget, progress_bar);
}

void ui_progress_bar_set_progress(struct ui_widget *bar, float progress)
{
    float total_width = (float)(uintptr_t)bar->priv;
    bar->uies[0]->width = total_width * progress;
}

void ui_progress_bar_set_color(struct ui_widget *bar, vec4 color)
{
    entity3d_color(bar->uies[1]->entity, COLOR_PT_ALL, color);
}

static void build_onclick(struct ui_element *uie, float x, float y)
{
    dbg("build onclick\n");
}

static __unused const char *wheel_items[] = { "^", ">", "v", "<" };
cerr ui_init(struct ui *ui, clap_context *clap_ctx, int width, int height)
{
    cerr ret = CERR_OK;
    struct font *font;

    ui->width = width;
    ui->height = height;
    mq_init(&ui->mq, ui);
    list_init(&ui->shaders);
    list_init(&ui->widgets);
    list_init(&ui->widget_cleanup);
    lib_request_shaders(clap_get_shaders(clap_ctx), "glyph", &ui->shaders);
    lib_request_shaders(clap_get_shaders(clap_ctx), "ui", &ui->shaders);

    ui->clap_ctx = clap_ctx;
    ui->renderer = clap_get_renderer(ui->clap_ctx);
    ui->time = clap_get_current_time(ui->clap_ctx);
    ui->ui_prog = shader_prog_find(&ui->shaders, "ui");
    ui->glyph_prog = shader_prog_find(&ui->shaders, "glyph");
    if (!ui->ui_prog || !ui->glyph_prog) {
        ret = CERR_SHADER_NOT_LOADED;
        goto err;
    }

    ret = ui_debug_init(ui);
    if (IS_CERR(ret))
        goto err;

    font = ref_new(font, .ctx = clap_get_font(ui->clap_ctx), .name = "ProggyTiny.ttf", .size = 16);
    if (!font) {
        ret = CERR_FONT_NOT_LOADED;
        goto err;
    }

    ret = ui_model_init(ui);
    if (IS_CERR(ret)) {
        ui_debug_done(ui);
        font_put(font);
        goto err;
    }

    uie1 = ref_new(ui_element,
                   .ui          = ui,
                   .txmodel     = ui_quadtx,
                   .affinity    = UI_AF_TOP | UI_AF_LEFT,
                   .x_off       = 10,
                   .y_off       = 10,
                   .width       = 300,
                   .height      = 100);
    uie1->on_click = build_onclick;

#ifndef CONFIG_FINAL
    build_uit = ui_printf(ui, font, uie1, (vec4){ 0.7, 0.7, 0.7, 1.0 }, 0, "%s @%s", clap_version, build_date);
#endif

    const char *pocket_textures[] = { "apple.png", "mushroom thumb.png" };
    pocket = ui_pocket_new(ui, pocket_textures, array_size(pocket_textures));
    font_put(font);

    ret = subscribe(MT_COMMAND, ui_handle_command, ui);
    if (IS_CERR(ret))
        goto err;
    ret = subscribe(MT_INPUT, ui_handle_input, ui);
    if (IS_CERR(ret))
        goto err;

    return ret;

err:
    shaders_free(&ui->shaders);
    return ret;
}

void ui_done(struct ui *ui)
{
    if (ui->menu)
        ui_menu_done(ui);
    if (ui->inventory)
        ui_inventory_done(ui);

    widgets_cleanup(&ui->widget_cleanup);
    widgets_cleanup(&ui->widgets);

    ui_debug_done(ui);
    if (uie0)
        ref_put(uie0);
#ifndef CONFIG_FINAL
    if (build_uit)
        ref_put_last(build_uit);
#endif
    if (uie1)
        ref_put_last(uie1);
    if (display_fps && bottom_uit) {
        ref_put_last(bottom_uit);
        ref_put_last(bottom_element);
    }
    ui_roll_done();

    mq_release(&ui->mq);

    /* these match shader_prog_find() in ui_init() */
    shader_prog_done(ui->ui_prog);
    shader_prog_done(ui->glyph_prog);

    /*
     * clean up the shaders that weren't freed by model3d_drop()
     * via mq_release()
     */
    shaders_free(&ui->shaders);
}

void ui_show(struct ui *ui)
{
}
