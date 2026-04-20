// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "display.h"
#include "error.h"
#include "input.h"
#include "input-controls.h"
#include "messagebus.h"
#include "ui.h"
#include "ui-debug.h"
#include "ui-menus.h"
#include "ui-settings.h"

static void menu_onfocus(struct ui_element *uie, bool focus)
{
    ui_element_set_visibility(uie, 1);
    ui_element_set_alpha(uie, 1.0);

    if (focus)  uia_lin_move(uie, UIE_MV_X_OFF, 1, 20, false, 1.0 / 6.0);
    else        uia_lin_move(uie, UIE_MV_X_OFF, 20, 1, false, 1.0 / 6.0);
}

static void menu_element_cb(struct ui_element *uie, unsigned int i)
{
    ui_element_set_visibility(uie, 0);

    uia_skip_duration(uie, 0.12 * i);
    uia_set_visible(uie, 1);
    uia_lin_float(uie, ui_element_set_alpha, 0, 1.0, true, 0.5);
    uia_cos_move(uie, UIE_MV_X_OFF, 200, 1, false, 0.5, 1.0, 0.0);
}

static void main_menu_on_create(struct ui *ui, struct ui_widget *uiw)
{
    ui->menu.widget = uiw;
}

static void menu_open(struct ui *ui, const ui_menu_item *root)
{
    if (!root || ui->menu.widget)   return;
    ui->menu.depth = 0;
    ui->menu.stack[0] = root;
    ui->menu.widget = ui_menu_new(ui, root);
}

static void menu_close(struct ui *ui)
{
    if (ui->menu.widget)
        ref_put(ui->menu.widget);
    ui->menu.widget = NULL;
    ui->menu.depth = 0;
}

#ifndef CONFIG_FINAL
static void __menu_devel(struct ui *ui, const ui_menu_item *item)
{
    /* In-game: close the menu + unpause so debug HUD can coexist with a
     * live game. Start menu: keep the menu up; the debug selector is an
     * imgui overlay on top. */
    if (ui->state == UI_ST_RUNNING) {
        menu_close(ui);
        ui_modality_send(ui);
    }
    ui_toggle_debug_selector();
}
#endif /* CONFIG_FINAL */

static void __menu_fullscreen(struct ui *ui, const ui_menu_item *item)
{
    menu_close(ui);
    ui_modality_send(ui);
    message_input_send(ui->clap_ctx, &(struct message_input){ .fullscreen = 1 }, NULL);
}

#ifndef CONFIG_BROWSER
static void __menu_exit(struct ui *ui, const ui_menu_item *item)
{
    if (ui->state == UI_ST_RUNNING)
        ui_modality_send(ui);
    menu_close(ui);
    display_request_exit();
}
#endif /* CONFIG_BROWSER */

static void __menu_start_game(struct ui *ui, const ui_menu_item *item)
{
    ui->state = UI_ST_LOADING;
    menu_close(ui);
    /*
     * Claim pointer lock from here rather than from the subsequent
     * UI_ST_RUNNING transition: this path is synchronous in the click
     * event handler, which Firefox and Safari require for PointerLock.
     * Chrome tolerates a later request; they don't.
     */
    if (input_controls_use_mouse())
        display_mouse_capture(true);
    /* Clap stays paused; the loading_cb (or the default below) sends the
     * modality toggle to unpause when loading is finished. */
    if (ui->menu.loading_cb)
        ui->menu.loading_cb(ui->clap_ctx, ui->menu.loading_cb_data);
    else
        ui_state_set_running(ui);
}

static const struct ui_widget_builder default_uwb = {
    .el_affinity    = UI_AF_TOP | UI_AF_RIGHT,
    .affinity       = UI_AF_TOP | UI_AF_RIGHT | UI_YOFF_FRAC,
    .el_margin      = 4,
    .x_off          = 10,
    .y_off          = 0.1,
    .w              = 500,
    .on_create      = main_menu_on_create,
    .el_cb          = menu_element_cb,
    .el_on_focus    = menu_onfocus,
    .el_color       = { 0.52f, 0.12f, 0.12f, 1.0f },
    .text_color     = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
};

/* Default in-game menu */
static const ui_menu_item default_in_game_root = UI_MENU_GROUP(
    NULL, &default_uwb,
#ifndef CONFIG_FINAL
    UI_MENU_ITEM("Devel",           __menu_devel),
#endif /* CONFIG_FINAL */
    UI_MENU_ITEM("Fullscreen",      __menu_fullscreen),
    UI_MENU_GROUP("Settings",       &default_uwb,
        UI_MENU_ITEM("Controls",    ui_settings_open_controls),
        UI_MENU_ITEM("Graphics",    NULL),
        UI_MENU_END
    ),
#ifndef CONFIG_BROWSER
    UI_MENU_ITEM("Exit",            __menu_exit),
#endif /* CONFIG_BROWSER */
    UI_MENU_END
);

/* Default start menu */
static const ui_menu_item default_start_root = UI_MENU_GROUP(
    NULL, &default_uwb,
    UI_MENU_ITEM("Start Game",      __menu_start_game),
#ifndef CONFIG_FINAL
    UI_MENU_ITEM("Devel",           __menu_devel),
#endif /* CONFIG_FINAL */
    UI_MENU_GROUP("Settings",       &default_uwb,
        UI_MENU_ITEM("Controls",    ui_settings_open_controls),
        UI_MENU_ITEM("Graphics",    NULL),
        UI_MENU_END
    ),
    UI_MENU_GROUP("Help",           &default_uwb,
        UI_MENU_ITEM("Credits",     NULL),
        UI_MENU_ITEM("License",     NULL),
        UI_MENU_ITEM("Help",        NULL),
        UI_MENU_END
    ),
#ifndef CONFIG_BROWSER
    UI_MENU_ITEM("Exit",            __menu_exit),
#endif /* CONFIG_BROWSER */
    UI_MENU_END
);

static int ui_menus_handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct ui *ui = data;
    uivec uivec = uivec_from_input(ui, m);

    switch (ui->state) {
    case UI_ST_START_MENU:
        /* Start menu is modal. A missed click acts as "back": pops one level
         * off the menu stack (see ui_menus_navigate_up). At the top of the
         * start menu it's a no-op, as the state machine keeps clap paused
         * until "Start Game". */
        if (m->input.mouse_click && ui->menu.widget &&
            !ui_widget_click(ui->menu.widget, uivec))
            ui_menus_navigate_up(ui);
        break;

    case UI_ST_LOADING:
        return MSG_HANDLED;

    case UI_ST_RUNNING:
        if (!ui->menu.in_game_root)
            return MSG_HANDLED;
        if (m->input.menu_toggle) {
            if (ui->menu.widget) {
                menu_close(ui);
                ui_modality_send(ui);
            } else {
                ui_modality_send(ui);
                menu_open(ui, ui->menu.in_game_root);
            }
        } else if (m->input.mouse_click) {
            if (!ui->menu.widget && !ui_element_click(ui, uivec)) {
                ui_modality_send(ui);
                menu_open(ui, ui->menu.in_game_root);
            } else if (ui->menu.widget && !ui_widget_click(ui->menu.widget, uivec)) {
                menu_close(ui);
                ui_modality_send(ui);
            }
        }
        break;
    }

    if (!clap_is_paused(ui->clap_ctx))
        return MSG_HANDLED;

    if (ui->menu.widget)
        ui->menu.widget->input_event(ui, ui->menu.widget, m);

    return MSG_STOP;
}

static const ui_menu_item *resolve_root(const ui_menu_config *cfg,
                                        const ui_menu_item *def,
                                        ui_menu_item *storage)
{
    if (!cfg || !cfg->enable)
        return NULL;

    const ui_menu_item *root = cfg->menu_root ? cfg->menu_root : def;
    if (cfg->uwb) {
        *storage = *root;
        storage->uwb = cfg->uwb;
        return storage;
    }
    return root;
}

cerr ui_menus_init(struct ui *ui,
                   const ui_menu_config *in_game_cfg,
                   const ui_menu_config *start_cfg)
{
    bool in_game_enable = in_game_cfg && in_game_cfg->enable;
    bool start_enable   = start_cfg && start_cfg->enable;

    if (!in_game_enable && !start_enable) {
        ui->state = UI_ST_RUNNING;
        return CERR_OK;
    }

    ui->menu.in_game_root = resolve_root(in_game_cfg, &default_in_game_root,
                                         &ui->menu.in_game_storage);
    ui->menu.start_root   = resolve_root(start_cfg,  &default_start_root,
                                         &ui->menu.start_storage);
    ui->menu.loading_cb      = start_cfg ? start_cfg->loading_cb      : NULL;
    ui->menu.loading_cb_data = start_cfg ? start_cfg->loading_cb_data : NULL;
    ui->menu.widget = NULL;
    ui->menu.depth  = 0;

    if (start_enable) {
        ui->state = UI_ST_START_MENU;
        ui_modality_send(ui);   /* pause */
        menu_open(ui, ui->menu.start_root);
    } else {
        ui->state = UI_ST_RUNNING;
    }

    return subscribe(ui->clap_ctx, MT_INPUT, ui_menus_handle_input, ui);
}

void ui_menus_done(struct ui *ui)
{
    menu_close(ui);
    ui->menu.in_game_root    = NULL;
    ui->menu.start_root      = NULL;
    ui->menu.loading_cb      = NULL;
    ui->menu.loading_cb_data = NULL;
}

void ui_menus_navigate_up(struct ui *ui)
{
    struct ui_widget *uiw = ui->menu.widget;
    if (!uiw)   return;

    on_create_fn on_create = uiw->on_create;
    void *priv = uiw->priv;

    if (ui->menu.depth > 0) {
        ui->menu.depth--;
        const ui_menu_item *root = ui->menu.stack[ui->menu.depth];
        ref_put(uiw);
        uiw = ui_menu_new(ui, root);
        if (uiw) {
            uiw->priv = priv;
            if (on_create)  on_create(ui, uiw);
        }
    } else if (ui->state == UI_ST_RUNNING) {
        ref_put(uiw);
        ui_modality_send(ui);
        if (on_create)  on_create(ui, NULL);
    }
    /* else: start menu at root — swallow */
}

ui_state ui_state_get(const struct ui *ui)
{
    return ui->state;
}

void ui_state_set_running(struct ui *ui)
{
    if (ui->state == UI_ST_RUNNING)
        return;
    ui->state = UI_ST_RUNNING;
    ui_modality_send(ui);   /* unpause */
}
