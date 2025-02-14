/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_H__
#define __CLAP_UI_H__

#include "object.h"
#include "model.h"
#include "scene.h"
#include "sound.h"

/* alignment affinity */
#define UI_AF_TOP    0x1
#define UI_AF_BOTTOM 0x2
#define UI_AF_LEFT   0x4
#define UI_AF_RIGHT  0x8
#define UI_AF_HCENTER (UI_AF_LEFT | UI_AF_RIGHT)
#define UI_AF_VCENTER (UI_AF_TOP | UI_AF_BOTTOM)
#define UI_AF_CENTER (UI_AF_VCENTER | UI_AF_HCENTER)
/* disable resizing */
#define UI_SZ_NOHRES 0x10
#define UI_SZ_NOVRES 0x20
#define UI_SZ_NORES (UI_SZ_NOHRES | UI_SZ_NOVRES)

enum uie_mv {
    UIE_MV_X_OFF = 0,
    UIE_MV_Y_OFF,
    UIE_MV_WIDTH,
    UIE_MV_HEIGHT,
    UIE_MV_MAX,
};

struct ui_element;
struct ui_widget;

struct ui_element {
    struct ref       ref;
    entity3d         *entity;
    struct ui_element *parent;
    struct ui        *ui;
    struct list      children;
    struct list      child_entry;
    struct list      animation;
    unsigned long    affinity;
    struct ui_widget *widget;
    void             *priv;
    void             (*on_click)(struct ui_element *uie, float x, float y);
    void             (*on_focus)(struct ui_element *uie, bool focus);
    bool             prescaled;
    bool             force_hidden;
    /* 2-byte hole */
    union {
        struct {
            float    x_off, y_off;
            float    width, height;
        };
        float        movable[UIE_MV_MAX];
    };
    float            actual_x;
    float            actual_y;
    float            actual_w;
    float            actual_h;
};

struct ui_widget_builder {
    unsigned long   affinity;
    float           x_off, y_off, w, h;
    unsigned long   el_affinity;
    float           el_x_off, el_y_off, el_w, el_h, el_margin;
    struct font     *font;
    float           el_color[4];
    float           text_color[4];
    void            (*el_cb)(struct ui_element *uie, unsigned int i);
};

struct ui_widget {
    struct ui_element  *root;
    struct ui_element  **uies;
    struct ref         ref;
    unsigned int       nr_uies;
    int                focus;
    struct list        entry;
};

//int ui_element_init(struct scene *s, float x, float y, float w, float h);
struct ui {
    struct mq          mq;
    struct list        shaders;
    struct shader_prog *ui_prog;
    struct shader_prog *glyph_prog;
    renderer_t         *renderer;
    struct sound       *click;
    struct ui_widget   *menu;
    struct ui_widget   *inventory;
    struct list        widget_cleanup;
    unsigned long      frames_total;
    double             time;
    int width, height;
    bool modal;
    float mod_x, mod_y;
};

void ui_pip_update(struct ui *ui, fbo_t *fbo);
struct ui_element *
ui_render_string(struct ui *ui, struct font *font, struct ui_element *parent,
                 const char *str, float *color, unsigned long flags);
struct ui_element *ui_element_new(struct ui *ui, struct ui_element *parent, model3dtx *txmodel,
                                  unsigned long affinity, float x_off, float y_off, float w, float h);
struct ui_widget *ui_wheel_new(struct ui *ui, const char **items);
struct ui_widget *ui_menu_new(struct ui *ui, const char **items, unsigned int nr_items);
struct ui_widget *ui_osd_new(struct ui *ui, const char **items, unsigned int nr_items);

cerr ui_init(struct ui *ui, renderer_t *r, int width, int height);
void ui_done(struct ui *ui);
void ui_update(struct ui *ui);
model3dtx *ui_quadtx_get(void);

void ui_element_animations_done(struct ui_element *uie);
int ui_element_update(entity3d *e, void *data);
void ui_element_set_visibility(struct ui_element *uie, int visible);
void ui_element_set_alpha(struct ui_element *uie, float alpha);

/* animations */
struct ui_animation;
struct ui_element *ui_animation_element(struct ui_animation *uia);
void uia_skip_duration(struct ui_element *uie, double duration);
void uia_action(struct ui_element *uie, void (*callback)(struct ui_animation *));
void uia_set_visible(struct ui_element *uie, int visible);
void uia_lin_float(struct ui_element *uie, void *setter, float start, float end, bool wait, double duration);
void uia_cos_float(struct ui_element *uie, void *setter, float start, float end, bool wait, double duration,
                   float phase, float shift);
void uia_lin_move(struct ui_element *uie, enum uie_mv mv, float start, float end, bool wait, double duration);
void uia_cos_move(struct ui_element *uie, enum uie_mv mv, float start, float end, bool wait, double duration, float phase, float shift);

#endif /* __CLAP_UI_H__ */
