// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio-settings.h"
#include "clap.h"
#include "display.h"
#include "graphics-settings.h"
#include "input-controls.h"
#include "input-joystick.h"
#include "model.h"
#include "pipeline.h"
#include "render.h"
#include "shader_constants.h"
#include "sound.h"
#include "ui.h"
#include "ui-menus.h"
#include "ui-settings.h"

#define IC_ITEM_MAX         (NR_JOYS + 4)
#define IC_LABEL_MAX        96

enum ic_kind {
    IC_KIND_GAMEPAD_NONE,
    IC_KIND_GAMEPAD_ANY,
    IC_KIND_GAMEPAD_NAMED,
    IC_KIND_USE_MOUSE,
    IC_KIND_SENSITIVITY,
    IC_KIND_INVERT_Y,
    IC_KIND_BACK,
};

static const float sensitivity_steps[] = {
    0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f,
};
#define NR_SENS_STEPS  array_size(sensitivity_steps)

static ui_menu_item ic_items[IC_ITEM_MAX + 1];
static char         ic_labels[IC_ITEM_MAX][IC_LABEL_MAX];
static enum ic_kind ic_kinds[IC_ITEM_MAX];
static char         ic_names[IC_ITEM_MAX][64];
static unsigned int ic_nr_items;
static ui_menu_item ic_root_copy;

static void settings_on_create(struct ui *ui, struct ui_widget *uiw)
{
    ui->menu.widget = uiw;
}

static void settings_on_focus(struct ui_element *uie, bool focus)
{
    ui_element_set_visibility(uie, 1);
    ui_element_set_alpha(uie, 1.0);
    if (focus)  uia_lin_move(uie, UIE_MV_X_OFF, 1, 10, false, 1.0 / 8.0);
    else        uia_lin_move(uie, UIE_MV_X_OFF, 10, 1, false, 1.0 / 8.0);
}

static void settings_element_cb(struct ui_element *uie, unsigned int i)
{
    ui_element_set_visibility(uie, 0);
    uia_skip_duration(uie, 0.04 * i);
    uia_set_visible(uie, 1);
    uia_lin_float(uie, ui_element_set_alpha, 0, 1.0, true, 0.3);
    uia_cos_move(uie, UIE_MV_X_OFF, 80, 1, false, 0.3, 1.0, 0.0);
}

static bool settings_input(struct ui *ui, struct ui_widget *uiw, struct message *m);

static const struct ui_widget_builder settings_uwb = {
    .el_affinity    = UI_AF_TOP | UI_AF_RIGHT,
    .affinity       = UI_AF_TOP | UI_AF_RIGHT | UI_YOFF_FRAC,
    .el_margin      = 3,
    .x_off          = 10,
    .y_off          = 0.1,
    .w              = 560,
    .font_size      = 20,
    .on_create      = settings_on_create,
    .el_cb          = settings_element_cb,
    .el_on_focus    = settings_on_focus,
    .input_event    = settings_input,
    .el_color       = { 0.25f, 0.25f, 0.28f, 1.0f },
    .text_color     = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
};

static const vec4 color_on   = { 0.22f, 0.55f, 0.32f, 1.0f };
static const vec4 color_off  = { 0.25f, 0.25f, 0.28f, 1.0f };
static const vec4 color_back = { 0.52f, 0.12f, 0.12f, 1.0f };

static void ic_rebuild(struct ui *ui);

static void replace_widget(struct ui *ui, struct ui_widget *new_uiw)
{
    struct ui_widget *uiw = ui->menu.widget;
    on_create_fn on_create = uiw->on_create;
    void *priv = uiw->priv;
    int focus = uiw->focus;
    ref_put(uiw);
    new_uiw->priv = priv;
    /*
     * Preserve focus across rebuilds so adjusting a setting via the keyboard
     * doesn't bounce it back to the top. If the item list shrank (e.g. the
     * mouse toggle flipped off and the sensitivity/invert rows disappeared),
     * clamp to the last valid index.
     */
    if (focus >= 0 && new_uiw->nr_uies > 0) {
        if (focus >= (int)new_uiw->nr_uies)
            focus = (int)new_uiw->nr_uies - 1;
        new_uiw->focus = focus;
        if (new_uiw->uies[focus]->on_focus)
            new_uiw->uies[focus]->on_focus(new_uiw->uies[focus], true);
    }
    if (on_create)  on_create(ui, new_uiw);
}

static void gamepad_label(char *buf, size_t n)
{
    input_controls_gamepad p = input_controls_gamepad_policy();
    if (p == INPUT_GAMEPAD_NONE)
        snprintf(buf, n, "Gamepad: None");
    else if (p == INPUT_GAMEPAD_ANY) {
        const char *act = input_controls_active_gamepad_name();
        snprintf(buf, n, "Gamepad: Any (%s)", act ? act : "not detected");
    } else {
        const char *saved = input_controls_saved_gamepad_name();
        const char *act = input_controls_active_gamepad_name();
        if (saved && act && !strcmp(saved, act))
            snprintf(buf, n, "Gamepad: %s", saved);
        else if (saved)
            snprintf(buf, n, "Gamepad: %s (offline, using %s)",
                     saved, act ? act : "none");
        else
            snprintf(buf, n, "Gamepad: %s", act ? act : "not detected");
    }
}

static void mouse_label(char *buf, size_t n)
{
    snprintf(buf, n, "Use mouse for camera: %s",
             input_controls_use_mouse() ? "On" : "Off");
}

static void sensitivity_label(char *buf, size_t n)
{
    snprintf(buf, n, "  Mouse sensitivity: %.2fx  < >",
             input_controls_mouse_sensitivity());
}

static void invert_y_label(char *buf, size_t n)
{
    snprintf(buf, n, "  Invert Y: %s",
             input_controls_invert_y() ? "On" : "Off");
}

static void ic_select_none(struct ui *ui, const ui_menu_item *item);
static void ic_select_named(struct ui *ui, const ui_menu_item *item);
static void ic_toggle_mouse(struct ui *ui, const ui_menu_item *item);
static void ic_cycle_sensitivity(struct ui *ui, const ui_menu_item *item);
static void ic_toggle_invert_y(struct ui *ui, const ui_menu_item *item);
static void ic_back(struct ui *ui, const ui_menu_item *item);

static void ic_item(unsigned int idx, enum ic_kind kind, const char *label,
                    const char *name, ui_menu_item_fn fn)
{
    strncpy(ic_labels[idx], label, IC_LABEL_MAX - 1);
    ic_labels[idx][IC_LABEL_MAX - 1] = '\0';
    ic_kinds[idx] = kind;
    if (name) {
        strncpy(ic_names[idx], name, sizeof(ic_names[idx]) - 1);
        ic_names[idx][sizeof(ic_names[idx]) - 1] = '\0';
    } else {
        ic_names[idx][0] = '\0';
    }
    ic_items[idx].name  = ic_labels[idx];
    ic_items[idx].uwb   = &settings_uwb;
    ic_items[idx].fn    = fn;
    ic_items[idx].items = NULL;
}

static void ic_populate(void)
{
    unsigned int i = 0;
    char buf[IC_LABEL_MAX];

    gamepad_label(buf, sizeof(buf));
    ic_item(i++, IC_KIND_GAMEPAD_NONE, "Gamepad: None", NULL, ic_select_none);
    /* "Gamepad: Any" hidden for now: the policy resolves fine in native but
     * the web build (input-www.c) doesn't route input from it. Revisit
     * post-jam along with the Firefox pointer-lock item. */
    /* ic_item(i++, IC_KIND_GAMEPAD_ANY, "Gamepad: Any", NULL, ic_select_any); */
    for (int j = 0; j < NR_JOYS && i < IC_ITEM_MAX - 2; j++) {
        const char *n = joystick_name_at(j);
        if (!n) continue;
        snprintf(buf, sizeof(buf), "  %s", n);
        ic_item(i++, IC_KIND_GAMEPAD_NAMED, buf, n, ic_select_named);
    }

    mouse_label(buf, sizeof(buf));
    ic_item(i++, IC_KIND_USE_MOUSE, buf, NULL, ic_toggle_mouse);

    if (input_controls_use_mouse()) {
        sensitivity_label(buf, sizeof(buf));
        ic_item(i++, IC_KIND_SENSITIVITY, buf, NULL, ic_cycle_sensitivity);
        invert_y_label(buf, sizeof(buf));
        ic_item(i++, IC_KIND_INVERT_Y, buf, NULL, ic_toggle_invert_y);
    }

    ic_item(i++, IC_KIND_BACK, "Back", NULL, ic_back);

    ic_items[i] = (ui_menu_item){};   /* terminator */
    ic_nr_items = i;
}

static void ic_recolor(struct ui_widget *uiw)
{
    input_controls_gamepad p = input_controls_gamepad_policy();
    const char *saved = input_controls_saved_gamepad_name();
    bool use_mouse = input_controls_use_mouse();

    for (unsigned int i = 0; i < ic_nr_items && i < uiw->nr_uies; i++) {
        vec4 c;
        bool on = false;
        switch (ic_kinds[i]) {
        case IC_KIND_GAMEPAD_NONE:
            on = (p == INPUT_GAMEPAD_NONE);
            break;
        case IC_KIND_GAMEPAD_ANY:
            on = (p == INPUT_GAMEPAD_ANY);
            break;
        case IC_KIND_GAMEPAD_NAMED:
            on = (p == INPUT_GAMEPAD_NAMED) && saved &&
                 !strcmp(saved, ic_names[i]);
            break;
        case IC_KIND_USE_MOUSE:
            on = use_mouse;
            break;
        case IC_KIND_SENSITIVITY:
            /* Non-toggle knob: keep neutral color. */
            memcpy(c, color_off, sizeof(c));
            entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
            continue;
        case IC_KIND_INVERT_Y:
            on = input_controls_invert_y();
            break;
        case IC_KIND_BACK:
            memcpy(c, color_back, sizeof(c));
            entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
            continue;
        }
        memcpy(c, on ? color_on : color_off, sizeof(c));
        entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
    }
}

static struct ui_widget *ic_make_widget(struct ui *ui)
{
    ic_populate();
    ic_root_copy = (ui_menu_item){
        .name  = NULL,
        .uwb   = &settings_uwb,
        .items = ic_items,
    };
    struct ui_widget *uiw = ui_menu_new(ui, &ic_root_copy);
    if (uiw)
        ic_recolor(uiw);
    return uiw;
}

static void ic_rebuild(struct ui *ui)
{
    struct ui_widget *uiw = ic_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
}

static void ic_select_none(struct ui *ui, const ui_menu_item *item)
{
    input_controls_set_gamepad(ui->clap_ctx, INPUT_GAMEPAD_NONE, NULL);
    ic_rebuild(ui);
}

/* paired with the commented-out "Gamepad: Any" entry in ic_populate(); keep
 * both together so the revival is a single uncomment */
#if 0
static void ic_select_any(struct ui *ui, const ui_menu_item *item)
{
    input_controls_set_gamepad(ui->clap_ctx, INPUT_GAMEPAD_ANY, NULL);
    ic_rebuild(ui);
}
#endif

static void ic_select_named(struct ui *ui, const ui_menu_item *item)
{
    unsigned int idx = (unsigned int)(item - ic_items);
    if (idx >= IC_ITEM_MAX) return;
    input_controls_set_gamepad(ui->clap_ctx, INPUT_GAMEPAD_NAMED, ic_names[idx]);
    ic_rebuild(ui);
}

static void ic_toggle_mouse(struct ui *ui, const ui_menu_item *item)
{
    input_controls_set_use_mouse(ui->clap_ctx, !input_controls_use_mouse());
    ic_rebuild(ui);
}

static unsigned int sens_step_index(float cur)
{
    /* Pick the step closest to the current value. */
    unsigned int best = 0;
    float best_diff = fabsf(sensitivity_steps[0] - cur);
    for (unsigned int k = 1; k < NR_SENS_STEPS; k++) {
        float diff = fabsf(sensitivity_steps[k] - cur);
        if (diff < best_diff) { best_diff = diff; best = k; }
    }
    return best;
}

static void sens_step(struct ui *ui, int dir)
{
    unsigned int idx = sens_step_index(input_controls_mouse_sensitivity());
    int next = (int)idx + dir;
    if (next < 0) next = 0;
    if (next >= (int)NR_SENS_STEPS) next = NR_SENS_STEPS - 1;
    if ((unsigned int)next == idx) return;
    input_controls_set_mouse_sensitivity(ui->clap_ctx, sensitivity_steps[next]);
    ic_rebuild(ui);
}

static void ic_cycle_sensitivity(struct ui *ui, const ui_menu_item *item)
{
    /* Click / enter steps up; wraps at the top so mouse users can still
     * reach lower values. Keyboard users should prefer left/right arrows
     * for direct up/down stepping. */
    unsigned int idx = sens_step_index(input_controls_mouse_sensitivity());
    unsigned int next = (idx + 1) % NR_SENS_STEPS;
    input_controls_set_mouse_sensitivity(ui->clap_ctx, sensitivity_steps[next]);
    ic_rebuild(ui);
}

static void ic_toggle_invert_y(struct ui *ui, const ui_menu_item *item)
{
    input_controls_set_invert_y(ui->clap_ctx, !input_controls_invert_y());
    ic_rebuild(ui);
}

static void ic_back(struct ui *ui, const ui_menu_item *item)
{
    ui_menus_navigate_up(ui);
}

static bool settings_input(struct ui *ui, struct ui_widget *uiw, struct message *m)
{
    int focus = uiw->focus;
    if (focus >= 0 && focus < (int)uiw->nr_uies && focus < (int)ic_nr_items) {
        enum ic_kind k = ic_kinds[focus];
        if (k == IC_KIND_SENSITIVITY) {
            bool left = m->input.left == 1 || m->input.yaw_left == 1 ||
                        m->input.delta_lx < -0.99;
            bool right = m->input.right == 1 || m->input.yaw_right == 1 ||
                         m->input.delta_lx > 0.99;
            if (left)   { sens_step(ui, -1); return true; }
            if (right)  { sens_step(ui, +1); return true; }
        } else if (k == IC_KIND_INVERT_Y) {
            bool left = m->input.left == 1 || m->input.yaw_left == 1 ||
                        m->input.delta_lx < -0.99;
            bool right = m->input.right == 1 || m->input.yaw_right == 1 ||
                         m->input.delta_lx > 0.99;
            if (left || right) {
                input_controls_set_invert_y(ui->clap_ctx, !input_controls_invert_y());
                ic_rebuild(ui);
                return true;
            }
        }
    }
    return ui_menu_input(ui, uiw, m);
}

void ui_settings_open_controls(struct ui *ui, const ui_menu_item *item)
{
    struct ui_widget *uiw = ic_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
    if (ui->menu.depth + 1 < UI_MENU_STACK_MAX) {
        ui->menu.depth++;
        ui->menu.stack[ui->menu.depth] = &ic_root_copy;
    }
}

/***************************************************************************
 * Graphics settings screen
 ***************************************************************************/

#define GS_ITEM_MAX         8
#define GS_LABEL_MAX        64

enum gs_kind {
    GS_KIND_FILM_GRAIN,
    GS_KIND_HDR,
    GS_KIND_SSAO,
    GS_KIND_BACK,
};

static ui_menu_item gs_items[GS_ITEM_MAX + 1];
static char         gs_labels[GS_ITEM_MAX][GS_LABEL_MAX];
static enum gs_kind gs_kinds[GS_ITEM_MAX];
static unsigned int gs_nr_items;
static ui_menu_item gs_root_copy;

/* Same look as Controls, but let the default ui_menu_input handle input:
 * all graphics rows are plain toggles, no per-item arrow-key semantics. */
static const struct ui_widget_builder graphics_uwb = {
    .el_affinity    = UI_AF_TOP | UI_AF_RIGHT,
    .affinity       = UI_AF_TOP | UI_AF_RIGHT | UI_YOFF_FRAC,
    .el_margin      = 3,
    .x_off          = 10,
    .y_off          = 0.1,
    .w              = 560,
    .font_size      = 20,
    .on_create      = settings_on_create,
    .el_cb          = settings_element_cb,
    .el_on_focus    = settings_on_focus,
    .el_color       = { 0.25f, 0.25f, 0.28f, 1.0f },
    .text_color     = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
};

static void gs_toggle_film_grain(struct ui *ui, const ui_menu_item *item);
static void gs_toggle_hdr(struct ui *ui, const ui_menu_item *item);
static void gs_toggle_ssao(struct ui *ui, const ui_menu_item *item);
static void gs_back(struct ui *ui, const ui_menu_item *item);

static void gs_item(unsigned int idx, enum gs_kind kind, const char *label,
                    ui_menu_item_fn fn)
{
    strncpy(gs_labels[idx], label, GS_LABEL_MAX - 1);
    gs_labels[idx][GS_LABEL_MAX - 1] = '\0';
    gs_kinds[idx] = kind;
    gs_items[idx].name  = gs_labels[idx];
    gs_items[idx].uwb   = &graphics_uwb;
    gs_items[idx].fn    = fn;
    gs_items[idx].items = NULL;
}

static void gs_populate(struct ui *ui)
{
    render_options *ro = clap_get_render_options(ui->clap_ctx);
    char buf[GS_LABEL_MAX];
    unsigned int i = 0;

    snprintf(buf, sizeof(buf), "Film grain: %s", ro->film_grain ? "On" : "Off");
    gs_item(i++, GS_KIND_FILM_GRAIN, buf, gs_toggle_film_grain);

    if (display_supports_edr()) {
        snprintf(buf, sizeof(buf), "HDR output: %s",
                 ro->hdr_output_enabled ? "On" : "Off");
        gs_item(i++, GS_KIND_HDR, buf, gs_toggle_hdr);
    }

    renderer_t *r = clap_get_renderer(ui->clap_ctx);
    if (r && renderer_get_caps(r)->renderer != RENDER_WGPU) {
        snprintf(buf, sizeof(buf), "SSAO: %s", ro->ssao ? "On" : "Off");
        gs_item(i++, GS_KIND_SSAO, buf, gs_toggle_ssao);
    }

    gs_item(i++, GS_KIND_BACK, "Back", gs_back);

    gs_items[i] = (ui_menu_item){};
    gs_nr_items = i;
}

static void gs_recolor(struct ui *ui, struct ui_widget *uiw)
{
    render_options *ro = clap_get_render_options(ui->clap_ctx);

    for (unsigned int i = 0; i < gs_nr_items && i < uiw->nr_uies; i++) {
        vec4 c;
        bool on = false;
        switch (gs_kinds[i]) {
        case GS_KIND_FILM_GRAIN:    on = ro->film_grain;          break;
        case GS_KIND_HDR:           on = ro->hdr_output_enabled;  break;
        case GS_KIND_SSAO:          on = ro->ssao;                break;
        case GS_KIND_BACK:
            memcpy(c, color_back, sizeof(c));
            entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
            continue;
        }
        memcpy(c, on ? color_on : color_off, sizeof(c));
        entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
    }
}

static struct ui_widget *gs_make_widget(struct ui *ui)
{
    gs_populate(ui);
    gs_root_copy = (ui_menu_item){
        .name  = NULL,
        .uwb   = &graphics_uwb,
        .items = gs_items,
    };
    struct ui_widget *uiw = ui_menu_new(ui, &gs_root_copy);
    if (uiw)
        gs_recolor(ui, uiw);
    return uiw;
}

static void gs_rebuild(struct ui *ui)
{
    struct ui_widget *uiw = gs_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
}

static void gs_toggle_film_grain(struct ui *ui, const ui_menu_item *item)
{
    render_options *ro = clap_get_render_options(ui->clap_ctx);
    graphics_settings_set_film_grain(ui->clap_ctx, !ro->film_grain);
    gs_rebuild(ui);
}

static void gs_toggle_hdr(struct ui *ui, const ui_menu_item *item)
{
    render_options *ro = clap_get_render_options(ui->clap_ctx);
    graphics_settings_set_hdr_output(ui->clap_ctx, !ro->hdr_output_enabled);
    gs_rebuild(ui);
}

static void gs_toggle_ssao(struct ui *ui, const ui_menu_item *item)
{
    render_options *ro = clap_get_render_options(ui->clap_ctx);
    graphics_settings_set_ssao(ui->clap_ctx, !ro->ssao);
    gs_rebuild(ui);
}

static void gs_back(struct ui *ui, const ui_menu_item *item)
{
    ui_menus_navigate_up(ui);
}

void ui_settings_open_graphics(struct ui *ui, const ui_menu_item *item)
{
    struct ui_widget *uiw = gs_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
    if (ui->menu.depth + 1 < UI_MENU_STACK_MAX) {
        ui->menu.depth++;
        ui->menu.stack[ui->menu.depth] = &gs_root_copy;
    }
}

/***************************************************************************
 * Sound settings screen
 ***************************************************************************/

#define AS_ITEM_MAX         4
#define AS_LABEL_MAX        64
#define AS_VOL_STEPS        10

enum as_kind {
    AS_KIND_MUSIC,
    AS_KIND_SFX,
    AS_KIND_BACK,
};

static ui_menu_item as_items[AS_ITEM_MAX + 1];
static char         as_labels[AS_ITEM_MAX][AS_LABEL_MAX];
static enum as_kind as_kinds[AS_ITEM_MAX];
static unsigned int as_nr_items;
static ui_menu_item as_root_copy;

static bool as_input(struct ui *ui, struct ui_widget *uiw, struct message *m);

static const struct ui_widget_builder sound_uwb = {
    .el_affinity    = UI_AF_TOP | UI_AF_RIGHT,
    .affinity       = UI_AF_TOP | UI_AF_RIGHT | UI_YOFF_FRAC,
    .el_margin      = 3,
    .x_off          = 10,
    .y_off          = 0.1,
    .w              = 560,
    .font_size      = 20,
    .on_create      = settings_on_create,
    .el_cb          = settings_element_cb,
    .el_on_focus    = settings_on_focus,
    .input_event    = as_input,
    .el_color       = { 0.25f, 0.25f, 0.28f, 1.0f },
    .text_color     = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
};

static float as_vol(enum as_kind k, struct ui *ui)
{
    sound_context *sc = clap_get_sound(ui->clap_ctx);
    if (!sc)    return 0.0f;
    if (k == AS_KIND_MUSIC) return sound_get_master_volume(sc, SOUND_CAT_MUSIC);
    if (k == AS_KIND_SFX)   return sound_get_master_volume(sc, SOUND_CAT_SFX);
    return 0.0f;
}

static void as_apply(enum as_kind k, struct ui *ui, float volume)
{
    if (k == AS_KIND_MUSIC)
        audio_settings_set_music_volume(ui->clap_ctx, volume);
    else if (k == AS_KIND_SFX)
        audio_settings_set_sfx_volume(ui->clap_ctx, volume);
}

static void music_label(char *buf, size_t n, struct ui *ui)
{
    snprintf(buf, n, "Music volume: %d%%  < >",
             (int)(as_vol(AS_KIND_MUSIC, ui) * 100.0f + 0.5f));
}

static void sfx_label(char *buf, size_t n, struct ui *ui)
{
    snprintf(buf, n, "SFX volume:   %d%%  < >",
             (int)(as_vol(AS_KIND_SFX, ui) * 100.0f + 0.5f));
}

static void as_cycle_music(struct ui *ui, const ui_menu_item *item);
static void as_cycle_sfx(struct ui *ui, const ui_menu_item *item);
static void as_back(struct ui *ui, const ui_menu_item *item);

static void as_item(unsigned int idx, enum as_kind kind, const char *label,
                    ui_menu_item_fn fn)
{
    strncpy(as_labels[idx], label, AS_LABEL_MAX - 1);
    as_labels[idx][AS_LABEL_MAX - 1] = '\0';
    as_kinds[idx] = kind;
    as_items[idx].name  = as_labels[idx];
    as_items[idx].uwb   = &sound_uwb;
    as_items[idx].fn    = fn;
    as_items[idx].items = NULL;
}

static void as_populate(struct ui *ui)
{
    char buf[AS_LABEL_MAX];
    unsigned int i = 0;

    music_label(buf, sizeof(buf), ui);
    as_item(i++, AS_KIND_MUSIC, buf, as_cycle_music);
    sfx_label(buf, sizeof(buf), ui);
    as_item(i++, AS_KIND_SFX, buf, as_cycle_sfx);
    as_item(i++, AS_KIND_BACK, "Back", as_back);

    as_items[i] = (ui_menu_item){};
    as_nr_items = i;
}

static void as_recolor(struct ui_widget *uiw)
{
    for (unsigned int i = 0; i < as_nr_items && i < uiw->nr_uies; i++) {
        vec4 c;
        if (as_kinds[i] == AS_KIND_BACK)
            memcpy(c, color_back, sizeof(c));
        else
            memcpy(c, color_off, sizeof(c));
        entity3d_color(uiw->uies[i]->entity, COLOR_PT_ALL, c);
    }
}

static struct ui_widget *as_make_widget(struct ui *ui)
{
    as_populate(ui);
    as_root_copy = (ui_menu_item){
        .name  = NULL,
        .uwb   = &sound_uwb,
        .items = as_items,
    };
    struct ui_widget *uiw = ui_menu_new(ui, &as_root_copy);
    if (uiw)
        as_recolor(uiw);
    return uiw;
}

static void as_rebuild(struct ui *ui)
{
    struct ui_widget *uiw = as_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
}

static unsigned int as_vol_step_index(float cur)
{
    int idx = (int)(cur * AS_VOL_STEPS + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > AS_VOL_STEPS) idx = AS_VOL_STEPS;
    return (unsigned int)idx;
}

static void as_vol_step(struct ui *ui, enum as_kind kind, int dir)
{
    unsigned int idx = as_vol_step_index(as_vol(kind, ui));
    int next = (int)idx + dir;
    if (next < 0) next = 0;
    if (next > AS_VOL_STEPS) next = AS_VOL_STEPS;
    if ((unsigned int)next == idx) return;
    as_apply(kind, ui, (float)next / (float)AS_VOL_STEPS);
    as_rebuild(ui);
}

static void as_cycle_music(struct ui *ui, const ui_menu_item *item)
{
    unsigned int idx = as_vol_step_index(as_vol(AS_KIND_MUSIC, ui));
    unsigned int next = (idx + 1) % (AS_VOL_STEPS + 1);
    as_apply(AS_KIND_MUSIC, ui, (float)next / (float)AS_VOL_STEPS);
    as_rebuild(ui);
}

static void as_cycle_sfx(struct ui *ui, const ui_menu_item *item)
{
    unsigned int idx = as_vol_step_index(as_vol(AS_KIND_SFX, ui));
    unsigned int next = (idx + 1) % (AS_VOL_STEPS + 1);
    as_apply(AS_KIND_SFX, ui, (float)next / (float)AS_VOL_STEPS);
    as_rebuild(ui);
}

static void as_back(struct ui *ui, const ui_menu_item *item)
{
    ui_menus_navigate_up(ui);
}

static bool as_input(struct ui *ui, struct ui_widget *uiw, struct message *m)
{
    int focus = uiw->focus;
    if (focus >= 0 && focus < (int)uiw->nr_uies && focus < (int)as_nr_items) {
        enum as_kind k = as_kinds[focus];
        if (k == AS_KIND_MUSIC || k == AS_KIND_SFX) {
            bool left  = m->input.left  == 1 || m->input.yaw_left  == 1 ||
                         m->input.delta_lx < -0.99;
            bool right = m->input.right == 1 || m->input.yaw_right == 1 ||
                         m->input.delta_lx >  0.99;
            if (left)   { as_vol_step(ui, k, -1); return true; }
            if (right)  { as_vol_step(ui, k, +1); return true; }
        }
    }
    return ui_menu_input(ui, uiw, m);
}

void ui_settings_open_sound(struct ui *ui, const ui_menu_item *item)
{
    struct ui_widget *uiw = as_make_widget(ui);
    if (!uiw)
        return;
    replace_widget(ui, uiw);
    if (ui->menu.depth + 1 < UI_MENU_STACK_MAX) {
        ui->menu.depth++;
        ui->menu.stack[ui->menu.depth] = &as_root_copy;
    }
}
