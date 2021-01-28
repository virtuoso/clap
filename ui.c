#define _GNU_SOURCE
#include <stdlib.h>
#include <errno.h>
#include "model.h"
#include "shader.h"
#include "messagebus.h"
#include "ui.h"
#include "font.h"

static struct model3d *ui_quad;
static struct model3dtx *ui_quadtx;

static bool __ui_element_is_visible(struct ui_element *uie, struct ui *ui)
{
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

    /* We might want force_invisible also */
    e->visible = __ui_element_is_visible(uie, ui) ? 1 : 0;
    /*trace("VIEWPORT %fx%f; xywh: %f %f %f %f\n", parent_width, parent_height,
          uie->actual_x, uie->actual_y, uie->actual_w, uie->actual_h);*/
    mat4x4_identity(e->mx->m);
    mat4x4_translate_in_place(e->mx->m, uie->actual_x, uie->actual_y, 0.0);
    if (!uie->prescaled)
        mat4x4_scale_aniso(e->mx->m, e->mx->m, uie->actual_w, uie->actual_h, 1.0);
}

static int ui_element_update(struct entity3d *e, void *data)
{
    struct ui *ui = data;
    mat4x4 p;

    ui_element_position(e->priv, ui);
    if (!e->visible)
        return 0;

    mat4x4_identity(p);
    mat4x4_ortho(p, 0, (float)ui->width, 0, (float)ui->height, 1.0, -1.0);
    mat4x4_mul(e->mx->m, p, e->mx->m);

    return 0;
}

void ui_update(struct ui *ui)
{
    struct model3dtx *txmodel;
    struct entity3d  *ent, *itent;

    list_for_each_entry(txmodel, &ui->txmodels, entry) {
        list_for_each_entry(ent, &txmodel->entities, entry) {
            struct ui_element *uie = ent->priv;
            uie->actual_x = uie->actual_y = uie->actual_w = uie->actual_h = -1.0;
        }
    }

    list_for_each_entry(txmodel, &ui->txmodels, entry) {
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            entity3d_update(ent, ui);
        }
    }
}

static void ui_animation_done(struct ui_animation *uia);

static void ui_element_drop(struct ref *ref)
{
    struct ui_element *uie = container_of(ref, struct ui_element, ref);
    struct ui_animation *ua, *itua;

    trace("dropping ui_element\n");
    if (uie->parent) {
        list_del(&uie->child_entry);
        ref_put(&uie->parent->ref);
    }

    /* XXX: factor out cancelling animations? */
    list_for_each_entry_iter(ua, itua, &uie->animation, entry) {
        ui_animation_done(ua);
    }
    err_on(!list_empty(&uie->children), "ui_element still has children\n");
    ref_put_last(&uie->entity->ref);
    free(uie);
}

struct ui_element *ui_element_new(struct ui *ui, struct ui_element *parent, struct model3dtx *txmodel,
                                  unsigned long affinity, float x_off, float y_off, float w, float h)
{
    struct ui_element *uie;
    struct entity3d *e = entity3d_new(txmodel);

    if (!e)
        return NULL;

    uie = ref_new(struct ui_element, ref, ui_element_drop);
    uie->entity = e;
    uie->ui     = ui;
    if (parent) {
        uie->parent = ref_get(parent);
        list_append(&parent->children, &uie->child_entry);
    }

    uie->affinity = affinity;
    uie->width    = w;
    uie->height   = h;
    uie->x_off    = x_off;
    uie->y_off    = y_off;
    list_init(&uie->children);
    list_init(&uie->animation);

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
    list_append(&ui->txmodels, &txmodel->entry);
}

static void ui_add_model_tail(struct ui *ui, struct model3dtx *txmodel)
{
    list_prepend(&ui->txmodels, &txmodel->entry);
}

static int ui_model_init(struct ui *ui)
{
    float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
    struct shader_prog *prog = shader_prog_find(ui->prog, "ui"); /* XXX */

    if (!prog) {
        err("couldn't load 'ui' shaders\n");
        return -EINVAL;
    }

    ui_quad = model3d_new_quad(prog, x, y, 0.1, w, h);
    ui_quad->cull_face = false;
    ui_quad->alpha_blend = true;
    /* XXX: maybe a "textured_model" as another interim object */
    ui_quadtx = model3dtx_new(ui_quad, "transparent.png");
    ref_put(&ui_quad->ref);
    ui_add_model_tail(ui, ui_quadtx);
    ref_put(&prog->ref);  /* matches shader_prog_find() above */
    return 0;
}

struct ui_text {
    struct font         *font;
    struct ui_element   *parent;
    char                *str;
    struct ui_element   **uies;
    struct model3dtx    **txms;
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
    struct ref          ref;
};

static void ui_text_drop(struct ref *ref)
{
    struct ui_text *uit = container_of(ref, struct ui_text, ref);
    unsigned int    i;

    trace("dropping ui_text\n");
    for (i = 0; i < uit->nr_uies; i++) {
        /* XXX: whitespaces etc don't have ui elements or txms */
        if (uit->uies[i])
            ref_put_last(&uit->uies[i]->ref);
        /* txms are reused, so not ref_put_last() */
        if (uit->txms[i])
            ref_put(&uit->txms[i]->ref);
    }

    free(uit->str);
    free(uit->line_nrw);
    free(uit->line_ws);
    free(uit->line_w);
    font_put(uit->font);
    free(uit->uies);
    free(uit->txms);
    free(uit);
}

//#define UIT_REFLOW  1

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

    free(uit->line_nrw);
    free(uit->line_ws);
    free(uit->line_w);

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

static struct model3dtx *ui_txm_find_by_texid(struct ui *ui, GLuint texid)
{
    struct model3dtx *txmodel;

    /* XXX: need trees for better search, these lists are actually long */
    list_for_each_entry(txmodel, &ui->txmodels, entry) {
        if (txmodel->texture_id == texid)
            return ref_get(txmodel);
    }

    return NULL;
}

struct ui_text *
ui_render_string(struct ui *ui, struct font *font, struct ui_element *parent,
                 const char *str, float *color, unsigned long flags)
{
    size_t len = strlen(str);
    struct ui_text      *uit;
    struct shader_prog  *prog;
    struct glyph        *glyph;
    struct model3d      *m;
    unsigned int        i, line;
    float               x, y;

    if (!flags)
        flags = UI_AF_VCENTER;

    CHECK(uit       = ref_new(struct ui_text, ref, ui_text_drop));
    uit->flags      = flags;
    uit->margin_x   = 10;
    uit->margin_y   = 10;
    uit->parent     = parent;
    CHECK(uit->str  = strdup(str));
    uit->font       = font_get(font);
    ui_text_measure(uit);
    uit->parent->width = uit->width + uit->margin_x * 2;
    uit->parent->height = uit->height + uit->margin_y * 2;
    ui_element_position(uit->parent, ui);
    y = (float)uit->margin_y + uit->y_off;
    dbg_on(y < 0, "y: %f, height: %d y_off: %d, margin_y: %d\n",
           y, uit->height, uit->y_off, uit->margin_y);
    CHECK(prog      = shader_prog_find(ui->prog, "glyph"));
    CHECK(uit->uies = calloc(len, sizeof(struct ui_element *)));
    CHECK(uit->txms = calloc(len, sizeof(struct model3dtx *)));
    uit->nr_uies = len;
    for (line = 0, i = 0, x = x_off(uit, line); i < len; i++) {
        if (str[i] == '\n') {
            line++;
            y += (uit->height / uit->nr_lines);
            x = x_off(uit, line);
            continue;
        }
        if (isspace(str[i])) {
            x += uit->line_ws[line];
            continue;
        }
        glyph   = font_get_glyph(uit->font, str[i]);
        uit->txms[i] = ui_txm_find_by_texid(ui, glyph->texture_id);
        if (!uit->txms[i]) {
            m       = model3d_new_quad(prog, 0, 0, 0, glyph->width, glyph->height);
            //model3d_set_name(m, "glyph");
            model3d_set_name(m, "glyph_%s_%c", font_name(uit->font), str[i]);
            m->cull_face = false;
            m->alpha_blend = true;
            uit->txms[i] = model3dtx_new_txid(m, glyph->texture_id);
            /* txm holds the only reference */
            ref_put(&m->ref);
            ui_add_model(ui, uit->txms[i]);
        }
        uit->uies[i] = ui_element_new(ui, parent, uit->txms[i], UI_AF_TOP | UI_AF_LEFT,
                                      x + glyph->bearing_x,
                                      y - glyph->bearing_y,
                                      glyph->width, glyph->height);
        ref_only(&uit->uies[i]->entity->ref);
        ref_only(&uit->uies[i]->ref);
        uit->uies[i]->entity->color[0] = color[0];
        uit->uies[i]->entity->color[1] = color[1];
        uit->uies[i]->entity->color[2] = color[2];
        uit->uies[i]->entity->color[3] = color[3];
        uit->uies[i]->prescaled = true;
        x += glyph->advance_x >> 6;
    }

    ref_put(&prog->ref); /* matches shader_prog_find() above */

    return uit;
}

struct ui_element *ui_roll_element;
struct ui_text *ui_roll_text;

static void ui_roll_done(void)
{
    if (!ui_roll_element || !ui_roll_text)
        return;

    ref_put_last(&ui_roll_text->ref);
    ref_put_last(&ui_roll_element->ref);

    ui_roll_text    = NULL;
    ui_roll_element = NULL;
}

static void ui_roll_init(struct ui *ui)
{
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    struct lib_handle *lh;
    LOCAL(char, buffer);
    struct font *font;
    size_t       size;

    lh = lib_read_file(RES_ASSET, "TODO.txt", (void **)&buffer, &size);
    font            = font_open("Pixellettersfull-BnJ5.ttf", 18);
    ui_roll_element = ui_element_new(ui, NULL, ui_quadtx, UI_AF_VCENTER | UI_AF_HCENTER, 0, 0, 300, 100);
    ui_roll_element->entity->update = ui_element_update;

    ui_roll_text = ui_render_string(ui, font, ui_roll_element, buffer, color, UI_AF_HCENTER | UI_AF_TOP | UI_SZ_NORES);
    ref_put_last(&lh->ref);

    font_put(font);
}

static struct ui_text *bottom_uit;
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

static void ui_element_set_visibility(struct ui_element *uie, int visible)
{
    ui_element_for_each_child(uie, __set_visibility, &visible);
}

static void __set_alpha(struct ui_element *uie, void *data)
{
    uie->entity->color[3] = *(float *)data;
}

static void ui_element_set_alpha(struct ui_element *uie, float alpha)
{
    ui_element_for_each_child(uie, __set_alpha, &alpha);
}

static void ui_animation_done(struct ui_animation *uia)
{
    list_del(&uia->entry);
    free(uia);
}

static int ui_animation_update(struct entity3d *e, void *data)
{
    struct ui_element *uie = e->priv;
    struct ui_animation *ua;

    if (list_empty(&uie->animation)) {
	    e->update = ui_element_update;
	    goto out;
    }

    ua = list_first_entry(&uie->animation, struct ui_animation, entry);
    ua->trans(ua);

out:
    return ui_element_update(e, data);
}

static void ui_animation_next(struct ui_animation *ua)
{
    struct ui_animation *next;

    if (ua == list_last_entry(&ua->uie->animation, struct ui_animation, entry))
        return;

    next = list_next_entry(ua, entry);
    next->trans(next);
}

static struct ui_animation *ui_animation(struct ui_element *uie)
{
    struct ui_animation *ua;

    CHECK(ua = calloc(1, sizeof (struct ui_animation)));
    ua->uie = uie;
    list_append(&uie->animation, &ua->entry);
    uie->entity->update = ui_animation_update;

    return ua;
}

/* ------------------------------ ANIMATIONS ------------------------------- */
static void __uia_skip_frames(struct ui_animation *ua)
{
    if (ua->uie->ui->frames_total < ua->start_frame)
        return;

    ui_animation_next(ua);
    ui_animation_done(ua);
}

void uia_skip_frames(struct ui_element *uie, unsigned long frames)
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->start_frame = uie->ui->frames_total + frames;
    uia->trans = __uia_skip_frames;
}

static void __uia_action(struct ui_animation *ua)
{
    struct ui_element *uie = ua->uie;
    bool done = false;

    if (ua == list_first_entry(&uie->animation, struct ui_animation, entry)) {
        done = true;
        ua->iter(ua);
    }

    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_action(struct ui_element *uie, void (*callback)(struct ui_animation *))
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->trans = __uia_action;
    uia->iter  = callback;
}

static void __uia_set_visible(struct ui_animation *ua)
{
    ui_element_set_visibility(ua->uie, ua->int0);

    ui_animation_next(ua);
    ui_animation_done(ua);
}

void uia_set_visible(struct ui_element *uie, int visible)
{
    struct ui_animation *uia;

    CHECK(uia = ui_animation(uie));
    uia->int0 = visible;
    uia->trans = __uia_set_visible;
}

/*
 * Updaters
 */
static void __uia_lin_float(struct ui_animation *ua)
{
    ua->float0 += ua->float_delta;
}

static void __uia_quad_float(struct ui_animation *ua)
{
    ua->float0 += ua->float_delta;
    ua->float_delta += ua->float_delta;
}

static void __uia_float(struct ui_animation *ua)
{
    void (*float_setter)(struct ui_element *, float) = ua->setter;
    bool done = false;

    if (!ua->int0) {
        ua->float0 = ua->float_start;
        ua->int0++;
    } else {
        ua->iter(ua);
    }

    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end)) {
        done = true;
        /* clamp, in case we overshoot */
        ua->float0 = ua->float_end;
    }

    (*float_setter)(ua->uie, ua->float0);
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_lin_float(struct ui_element *uie, void *setter, float start, float end, unsigned long frames)
{
    struct ui_animation *uia;
    float len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = len / frames;
    uia->setter      = setter;
    uia->iter        = __uia_lin_float;
    uia->trans       = __uia_float;
}

void uia_quad_float(struct ui_element *uie, void *setter, float start, float end, float accel)
{
    struct ui_animation *uia;

    if ((start > end && accel >= 0) || (start < end && accel <= 0)) {
        warn("end %f unreachable from start %f via %f\n", end, start, accel);
        return;
    }

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = accel;
    uia->setter      = setter;
    uia->iter        = __uia_quad_float;
    uia->trans       = __uia_float;
}

static void __uia_float_move(struct ui_animation *ua)
{
    bool done = false;

    if (!ua->int0) {
        ua->float0 = ua->float_start;
        ua->start_frame = ua->uie->ui->frames_total;
        ua->int0++;
    } else {
        ua->iter(ua);
    }

    if ((ua->float_start < ua->float_end && ua->float0 >= ua->float_end) ||
        (ua->float_start > ua->float_end && ua->float0 <= ua->float_end)) {
        done = true;
        ua->float0 = ua->float_end;
    }

    ua->uie->movable[ua->int1] = ua->float0;
    ui_animation_next(ua);

    if (done)
        ui_animation_done(ua);
}

void uia_lin_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames)
{
    struct ui_animation *uia;
    float len = end - start;

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = len / frames;
    uia->int1        = mv;
    uia->trans       = __uia_float_move;
    uia->iter        = __uia_lin_float;
}

static void __uia_cos_float(struct ui_animation *ua)
{
    struct ui *ui = ua->uie->ui;

    ua->float0 = cos_interp(ua->float_start, ua->float_end,
                            ua->float_shift + ua->float_delta * (ui->frames_total - ua->start_frame));
}

void uia_cos_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames, float phase, float shift)
{
    struct ui_animation *uia;
    float len = fabsf(start - end);
    float delta = len / frames;

    CHECK(uia = ui_animation(uie));
    uia->float_start = start;
    uia->float_end   = end;
    uia->float_delta = (delta / len) * phase;
    uia->float_shift = delta * shift;
    uia->int1        = mv;
    uia->trans       = __uia_float_move;
    uia->iter        = __uia_cos_float;
}

static struct ui_widget *ui_menu_new(struct ui *ui, const char **items, unsigned int nr_items);
static void ui_menu_done(struct ui *ui);

static const char *sub_items[] = { "...todo", "...help", "...credits" };
static void menu_onclick(struct ui_element *uie, float x, float y)
{
    int nr = (long)uie->priv;
    struct ui *ui = uie->ui;

    if (!strcmp(ui->menu->texts[nr]->str, "Help")) {
        ref_put_last(&ui->menu->ref);
        ui->menu = ui_menu_new(ui, sub_items, array_size(sub_items));
    } else if (!strcmp(ui->menu->texts[nr]->str, "...todo")) {
        ui_menu_done(ui); /* cancels modality */
        ui_roll_init(ui);
    }
}

static void ui_widget_drop(struct ref *ref)
{
    struct ui_widget *uiw = container_of(ref, struct ui_widget, ref);
    int i;

    for (i = 0; i < uiw->nr_uies; i++) {
        ref_put(&uiw->texts[i]->ref);
        ref_put(&uiw->uies[i]->ref);
    }
    ref_put_last(&uiw->root->ref);
    free(uiw->texts);
    free(uiw->uies);
    free(uiw);
}

static struct ui_widget *ui_menu_new(struct ui *ui, const char **items, unsigned int nr_items)
{
    float quad_color[] = { 0.0, 0.3, 0.1, 0.0 };
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    struct ui_widget *menu;
    struct model3dtx *txm;
    struct model3d *model;
    struct font *font;
    float off, width;
    int i;

    CHECK(menu        = ref_new(struct ui_widget, ref, ui_widget_drop));
    CHECK(menu->uies  = calloc(nr_items, sizeof(struct ui_element *)));
    CHECK(menu->texts = calloc(nr_items, sizeof(struct ui_text *)));
    CHECK(menu->root  = ui_element_new(ui, NULL, ui_quadtx, UI_AF_VCENTER | UI_AF_RIGHT, 10, 10, 500, 0.8));
    menu->nr_uies = nr_items;
    menu->focus   = -1;

    model = model3d_new_quad(ui->prog, 0, 0, 0.05, 1, 1);
    txm = model3dtx_new(model, "green.png");
    ref_put(&model->ref);
    ui_add_model(ui, txm);
    font = font_open("Pixellettersfull-BnJ5.ttf", 48);
    for (i = 0, off = 0.0, width = 0.0; i < nr_items; i++) {
        menu->uies[i]           = ui_element_new(ui, menu->root, txm, UI_AF_TOP | UI_AF_RIGHT, 10, 10 + off, 300, 100);
        menu->uies[i]->on_click = menu_onclick;
        menu->uies[i]->priv     = (void *)(long)i;

        memcpy(&menu->uies[i]->entity->color, quad_color, sizeof(quad_color));
        uia_skip_frames(menu->uies[i], i * 7);
        uia_set_visible(menu->uies[i], 1);
        uia_lin_float(menu->uies[i], ui_element_set_alpha, 0, 1.0, 25);
        uia_cos_move(menu->uies[i], UIE_MV_X_OFF, 200, 1, 30, 1.0, 0.0);

        CHECK(menu->texts[i] = ui_render_string(ui, font, menu->uies[i], items[i], color, 0));
        width = max(width, menu->uies[i]->width);
        off += menu->uies[i]->height + 4 /* XXX: margin */;
        ui_element_set_visibility(menu->uies[i], 0);
    }
    for (i = 0; i < nr_items; i++) {
        menu->uies[i]->width = width;
    }
    //ref_put(&txm->ref);
    font_put(font);

    return menu;
}

static void ui_widget_pick_rel(struct ui_widget *uiw, int dpos)
{
    if (!dpos)
        return;

    /* out-of-focus animation */
    if (uiw->focus >= 0)
        uia_lin_move(uiw->uies[uiw->focus], UIE_MV_X_OFF, 20, 1, 10);

    if (dpos + (int)uiw->focus < 0)
        uiw->focus = uiw->nr_uies - 1;
    else if (dpos + uiw->focus >= uiw->nr_uies)
        uiw->focus = 0;
    else
        uiw->focus += dpos;

    /* in-focus-animation */
    uia_lin_move(uiw->uies[uiw->focus], UIE_MV_X_OFF, 1, 20, 10);
}

static const char *menu_items[] = { "Level", "Settings", "Network", "Devel", "Help" };
static void ui_menu_init(struct ui *ui)
{
    ui->menu = ui_menu_new(ui, menu_items, array_size(menu_items));
    /* XXX: should send a message to others that the UI owns inputs now */
    ui->modal = true;
}

static void ui_menu_done(struct ui *ui)
{
    ref_put(&ui->menu->ref);
    ui->menu = NULL;
    ui->modal = false;
}

static int ui_widget_within(struct ui_widget *uiw, int x, int y)
{
    struct ui_element *child;
    int i;

    for (i = 0; i < uiw->nr_uies; i++) {
        child = uiw->uies[i];

        if (x >= child->actual_x && x < child->actual_x + child->actual_w && y >= child->actual_y &&
            y < child->actual_y + child->actual_h)
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
        uia_lin_move(uiw->uies[uiw->focus], UIE_MV_X_OFF, 20, 1, 10);
    if (n >= 0)
        uia_lin_move(uiw->uies[n], UIE_MV_X_OFF, 1, 20, 10);
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
    struct font *font = font_get_default();
    float color[] = { 0.7, 0.7, 0.7, 1.0 };
    struct ui *ui = data;
    LOCAL(char, str);

    if (m->type != MT_COMMAND)
        return 0;

    if (m->cmd.status) {
        ref_put_last(&bottom_uit->ref);
        CHECK(asprintf(&str, "FPS: %d\nTime: %d:%02d", m->cmd.fps,
                       m->cmd.sys_seconds / 60, m->cmd.sys_seconds % 60));
        bottom_uit = ui_render_string(ui, font, bottom_element, str, color, UI_AF_RIGHT);
    } else if (m->cmd.menu_enter) {
        ui_menu_init(ui);
    } else if (m->cmd.menu_exit) {
        ui_menu_done(ui);
    }
    font_put(font);

    return 0;
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
        if (!ui->menu)
            ui_menu_init(ui);
        else
            ui_menu_click(ui->menu, m->input.x, (int)ui->height - m->input.y);
    }

    if (!ui->modal)
        return 0;
    
    if (m->input.mouse_move)
        ui_widget_hover(ui->menu, m->input.x, (int)ui->height - m->input.y);
    
    /* UI owns the inputs */
    ui->mod_y += m->input.delta_y;
    if (m->input.up || ui->mod_y <= -100) {
        // select previous
        ui->mod_y = 0;
        ui_widget_pick_rel(ui->menu, -1);
    } else if (m->input.down || ui->mod_y >= 100) {
        // select next
        ui->mod_y = 0;
        ui_widget_pick_rel(ui->menu, 1);
    } else if (m->input.left || m->input.delta_x < 0) {
        // go back
    } else if (m->input.right || m->input.delta_x > 0) {
        // enter
        ui->menu->uies[ui->menu->focus]->on_click(ui->menu->uies[ui->menu->focus], 0, 0);
    }
    return 0;
}

static struct ui_text *limeric_uit;
static struct ui_text *build_uit;
struct ui_element *uie0, *uie1;

static const char text_str[] =
    "On the chest of a barmaid in Sale\n"
    "Were tattooed all the prices of ale;\n"
    "And on her behind, for the sake of the blind,\n"
    "Was the same information in Braille";
int ui_init(struct ui *ui, int width, int height)
{
    struct font *font;
    float color[] = { 0.7, 0.7, 0.7, 1.0 };

    list_init(&ui->txmodels);
    lib_request_shaders("glyph", &ui->prog);
    lib_request_shaders("ui", &ui->prog);

    ui->click = sound_load("stapler.ogg");
    sound_set_gain(ui->click, 0.2);
    font = font_open("Pixellettersfull-BnJ5.ttf", 32);
    ui_model_init(ui);
    uie0 = ui_element_new(ui, NULL, ui_quadtx, UI_AF_TOP    | UI_AF_RIGHT, 10, 10, 300, 100);
    bottom_element = ui_element_new(ui, NULL, ui_quadtx, UI_AF_BOTTOM | UI_AF_RIGHT, 0.01, 50, 400, 150);
    bottom_uit = ui_render_string(ui, font, bottom_element, "Ppodq\nQq", color, UI_AF_RIGHT);
    uie1 = ui_element_new(ui, NULL, ui_quadtx, UI_AF_TOP    | UI_AF_LEFT, 10, 10, 300, 100);
    limeric_uit = ui_render_string(ui, font, uie0, text_str, color, 0/*UI_AF_RIGHT*/);
    build_uit = ui_render_string(ui, font, uie1, BUILDDATE, color, 0);

    font_put(font);
    subscribe(MT_COMMAND, ui_handle_command, ui);
    subscribe(MT_INPUT, ui_handle_input, ui);
    return 0;
}

void ui_done(struct ui *ui)
{
    struct model3dtx *txmodel, *ittxm;
    struct entity3d *ent, *itent;

    if (ui->menu)
        ui_menu_done(ui);

    ref_put(&uie0->ref);
    ref_put_last(&build_uit->ref);
    ref_put(&uie1->ref);
    ref_put_last(&bottom_uit->ref);
    ref_put_last(&bottom_element->ref);
    ref_put_last(&limeric_uit->ref);
    ui_roll_done();

    /*
     * Iterate the flat list of txmodels; these are parent and children ui
     * elements.
     */
    list_for_each_entry_iter(txmodel, ittxm, &ui->txmodels, entry) {
        list_for_each_entry_iter(ent, itent, &txmodel->entities, entry) {
            struct ui_element *uie = ent->priv;

            ref_put(&uie->ref);
        }
        ref_put_last(&txmodel->ref);
    }
}

void ui_show(struct ui *ui)
{
}
