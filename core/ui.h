/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_H__
#define __CLAP_UI_H__

#include "object.h"
#include "messagebus.h"
#include "model.h"

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

/* offsets/dimensions are fractions of parent's as opposed to absolute */
#define UI_SZ_WIDTH_FRAC    0x40
#define UI_SZ_HEIGHT_FRAC   0x80
#define UI_SZ_FRAC          (UI_SZ_WIDTH_FRAC | UI_SZ_HEIGHT_FRAC)
#define UI_XOFF_FRAC        0x100
#define UI_YOFF_FRAC        0x200
#define UI_OFF_FRAC         (UI_XOFF_FRAC | UI_YOFF_FRAC)

enum uie_mv {
    UIE_MV_X_OFF = 0,
    UIE_MV_Y_OFF,
    UIE_MV_WIDTH,
    UIE_MV_HEIGHT,
    UIE_MV_MAX,
};

struct ui_element;
struct ui_widget;
struct ui;

/**
 * struct uivec - UI coordinates
 * @x:  x coordinate in UI coordinate system
 * @y:  y coordinate in UI coordinate system
 */
typedef struct uivec {
    unsigned int    x;
    unsigned int    y;
} uivec;

/**
 * uivec_from_input() - obtain UI coordinates from an input message
 * @ui: ui context
 * @m:  input message
 *
 * Return: uivec with coordinates in UI coordinate system
 */
uivec uivec_from_input(struct ui *ui, struct message *m);

typedef void (*on_click_fn)(struct ui_element *uie, float x, float y);
typedef void (*on_focus_fn)(struct ui_element *uie, bool focus);

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
    on_click_fn      on_click;
    on_focus_fn      on_focus;
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

/**
 * ui_element_click() - dispatch a pointer click to a ui element
 * @ui:     ui context
 * @uivec:  ui coordinates
 *
 * Scan through ui elements and deliver a pointer click to a matching
 * element, if a match exists, but calling its ->on_click().
 *
 * Return: true an hit, false if no element matched click coordinates.
 */
bool ui_element_click(struct ui *ui, uivec uivec);

typedef bool (*input_event_fn)(struct ui *ui, struct ui_widget *uiw, struct message *m);
typedef void (*on_create_fn)(struct ui *ui, struct ui_widget *uiw);

struct ui_widget_builder {
    unsigned long   affinity;
    float           x_off, y_off, w, h;
    unsigned long   el_affinity;
    float           el_x_off, el_y_off, el_w, el_h, el_margin;
    struct font     *font;
    float           el_color[4];
    float           text_color[4];
    void            (*el_cb)(struct ui_element *uie, unsigned int i);
    on_click_fn     el_on_click;
    on_focus_fn     el_on_focus;
    on_create_fn    on_create;
    input_event_fn  input_event;
};

DEFINE_REFCLASS_INIT_OPTIONS(ui_element,
    struct ui                   *ui;
    struct ui_element           *parent;
    model3dtx                   *txmodel;
    unsigned long               affinity;
    float                       x_off;
    float                       y_off;
    float                       width;
    float                       height;
    struct ui_widget_builder    *uwb;
    bool                        uwb_root;
);
DECLARE_REFCLASS(ui_element);

struct ui_widget {
    struct ui_element  *root;
    struct ui_element  **uies;
    input_event_fn     input_event;
    on_create_fn       on_create;
    struct ref         ref;
    unsigned int       nr_uies;
    int                focus;
    struct list        entry;
    void               *priv;
};

DEFINE_REFCLASS_INIT_OPTIONS(ui_widget,
    struct ui                   *ui;
    struct ui_widget_builder    *uwb;
    unsigned int                nr_items;
);
DECLARE_REFCLASS(ui_widget);

/**
 * ui_widget_click() - dispatch a pointer click within a widget
 * @uiw:    widget
 * @uivec:  UI coordinates
 *
 * Find an element within a widget where a pointer click lands and call its
 * on_click() method.
 *
 * Return: true if click landed, false if there's no element at [x, y].
 */
bool ui_widget_click(struct ui_widget *uiw, uivec uivec);

typedef struct clap_context clap_context;

struct ui {
    struct mq          mq;
    struct list        shaders;
    struct shader_prog *ui_prog;
    struct shader_prog *glyph_prog;
    renderer_t         *renderer;
    struct ui_widget   *inventory;
    struct list        widgets;
    struct list        widget_cleanup;
    double             time;
    clap_context       *clap_ctx;
    void               *priv;
    int width, height;
    float mod_x, mod_y;
};

/**
 * ui_modality_send() - send a modality toggle message
 * @ui:     ui context
 *
 * Send a command message to toggle UI modality state.
 */
static inline void ui_modality_send(struct ui *ui)
{
    message_send(
        ui->clap_ctx,
        &(struct message) {
            .type   = MT_COMMAND,
            .cmd    = (struct message_command) { .toggle_modality = 1 }
        }
    );
}

struct ui_element *ui_printf(struct ui *ui, struct font *font, struct ui_element *parent,
                             float *color, unsigned long flags, const char *fmt, ...)
                             __printf(6, 7) __nonnull_params((1, 2, 4, 6));
struct ui_widget *ui_wheel_new(struct ui *ui, const char **items);

typedef struct ui_menu_item ui_menu_item;
typedef void (*ui_menu_item_fn)(struct ui *ui, const ui_menu_item *item);

/**
 * struct ui_menu_item - menu item definition
 * @name:   name that appears in the menu
 * @uwb:    widget builder pointer
 * @fn:     function to call for leaf items
 * @items:  array of child items, NULL-terminated
 *
 * An item can be either a "group" (@items != NULL) or an "item"
 * (leaf, @fn != NULL). For groups, @uwb should point to a widget
 * builder structure. All nodes except the root need a valid @name.
 */
typedef struct ui_menu_item {
    const char                      *name;
    const struct ui_widget_builder  *uwb;
    ui_menu_item_fn                 fn;
    struct ui_menu_item             *items;
} ui_menu_item;

/**
 * define UI_MENU_GROUP - define a group menu entry
 * @_label: item text
 * @_uwb:   widget builder pointer
 * @_items: a list of child items (UI_MENU_GROUP, UI_MENU_ITEM, UI_MENU_END)
 */
#define UI_MENU_GROUP(_label, _uwb, _items...) \
    { \
        .name       = _label, \
        .uwb        = _uwb, \
        .items      = (ui_menu_item[]) { _items } \
    }

/**
 * define UI_MENU_ITEM - define a (leaf) menu item entry
 * @_label: item text
 * @_fn:    function to call for a leaf item
 */
#define UI_MENU_ITEM(_label, _fn)   { .name = _label, .fn = _fn }

/**
 * define UI_MENU_GROUP - item list terminator
 */
#define UI_MENU_END {}

/**
 * ui_menu_new() - create a menu widget
 * @ui:     ui context
 * @root:   root of the menu item tree
 *
 * Return: ui_widget pointer or NULL on error
 */
struct ui_widget *ui_menu_new(struct ui *ui, const ui_menu_item *root);
struct ui_widget *ui_osd_new(struct ui *ui, const struct ui_widget_builder *uwb,
                             const char **items, unsigned int nr_items);

typedef struct progress_bar_options {
    float           width;
    float           height;
    float           border;
    float           y_off;
    unsigned long   affinity;
    const char      *fmt;
    float           *border_color;
    float           *bar_color;
} progress_bar_options;

#define ui_progress_bar_new(_ui, args...) \
    _ui_progress_bar_new((_ui), &(progress_bar_options){ args })
cresp(ui_widget) _ui_progress_bar_new(struct ui *ui, const progress_bar_options *opts);
void ui_progress_bar_set_progress(struct ui_widget *bar, float progress);
void ui_progress_bar_set_color(struct ui_widget *bar, vec4 color);

cerr ui_init(struct ui *ui, clap_context *clap_ctx, int width, int height);
void ui_done(struct ui *ui);
void ui_update(struct ui *ui);
model3d *ui_quad_new(struct shader_prog *p, float x, float y, float w, float h);
model3dtx *ui_quadtx_get(void);
void ui_add_model(struct ui *ui, model3dtx *txmodel);
void ui_add_model_tail(struct ui *ui, model3dtx *txmodel);

void ui_element_animations_done(struct ui_element *uie);
void ui_element_animations_skip(struct ui_element *uie);
int ui_element_update(entity3d *e, void *data);

/**
 * ui_element_set_visibility() - set visibility for an element and all its children
 * @uie:        ui element to start with
 * @visible:    1: visible, 0: not
 *
 * Set visibility value for a given UI element and all its children.
 */
void ui_element_set_visibility(struct ui_element *uie, int visible);

/**
 * ui_element_set_alpha() - set alpha for an element and all its children
 * @uie:    ui element to start with
 * @alpha:  alpha channel value [0.0, 1.0]
 *
 * Set alpha channel value for a given UI element and all its children.
 */
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
