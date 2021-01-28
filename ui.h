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

struct ui_animation {
    struct list entry;
    struct ui_element *uie;
    void (*trans)(struct ui_animation *uia);
    void *setter;
    void (*iter)(struct ui_animation *uia);
    unsigned long start_frame;
    unsigned long sound_frame;
    int           int0;
    int           int1;
    float         float0;
    float         float_start;
    float         float_end;
    float         float_delta;
    float         float_shift;
};

struct ui_element {
    struct ref       ref;
    struct entity3d *entity;
    struct ui_element *parent;
    struct ui        *ui;
    struct list      children;
    struct list      child_entry;
    struct list      animation;
    unsigned long    affinity;
    void             *priv;
    void             (*on_click)(struct ui_element *uie, float x, float y);
    bool             prescaled;
    bool             autoremove;
    int              force_hidden;
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

struct ui_widget {
    struct ui_element  *root;
    struct ui_text     **texts;
    struct ui_element  **uies;
    struct ref         ref;
    unsigned int       nr_uies;
    int                focus;
};

//int ui_element_init(struct scene *s, float x, float y, float w, float h);
struct ui {
    struct model3d     *_model;
    struct list        txmodels;
    struct shader_prog *prog;
    struct sound       *click;
    struct ui_widget   *menu;
    unsigned long      frames_total;
    int width, height;
    bool modal;
    float mod_x, mod_y;
};

struct ui_element *ui_element_new(struct ui *ui, struct ui_element *parent, struct model3dtx *txmodel,
                                  unsigned long affinity, float x_off, float y_off, float w, float h);

int ui_init(struct ui *ui, int width, int height);
void ui_done(struct ui *ui);
void ui_update(struct ui *ui);

/* animations */
void uia_skip_frames(struct ui_element *uie, unsigned long frames);
void uia_action(struct ui_element *uie, void (*callback)(struct ui_animation *));
void uia_set_visible(struct ui_element *uie, int visible);
void uia_lin_float(struct ui_element *uie, void *setter, float start, float end, unsigned long frames);
void uia_quad_float(struct ui_element *uie, void *setter, float start, float end, float accel);
void uia_lin_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames);
void uia_cos_move(struct ui_element *uie, enum uie_mv mv, float start, float end, unsigned long frames, float phase, float shift);

#endif /* __CLAP_UI_H__ */