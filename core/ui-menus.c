// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "display.h"
#include "error.h"
#include "input.h"
#include "messagebus.h"
#include "ui.h"
#include "ui-debug.h"
#include "ui-menus.h"

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

static void main_menu_init(struct ui *ui)
{
    ui_modality_send(ui);
    ui->menu.widget = ui_menu_new(ui, ui->menu.root);
}

static void main_menu_done(struct ui *ui)
{
    ui_modality_send(ui);
    if (ui->menu.widget)
        ref_put(ui->menu.widget);
    ui->menu.widget = NULL;
}

#ifndef CONFIG_FINAL
static void __menu_devel(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui);
    ui_toggle_debug_selector();
}
#endif /* CONFIG_FINAL */

static void __menu_fullscreen(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui);
    message_input_send(ui->clap_ctx, &(struct message_input){ .fullscreen = 1 }, NULL);
}

#ifndef CONFIG_BROWSER
static void __menu_exit(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui);
    display_request_exit();
}
#endif /* CONFIG_BROWSER */

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
static const ui_menu_item default_menu_root = UI_MENU_GROUP(
    NULL, &default_uwb,
#ifndef CONFIG_FINAL
    UI_MENU_ITEM("Devel",           __menu_devel),
#endif /* CONFIG_FINAL */
    UI_MENU_ITEM("Fullscreen",      __menu_fullscreen),
#ifndef CONFIG_BROWSER
    UI_MENU_ITEM("Exit",            __menu_exit),
#endif /* CONFIG_BROWSER */
    UI_MENU_END
);

static int ui_menus_handle_input(struct clap_context *ctx, struct message *m, void *data)
{
    struct ui *ui = data;
    uivec uivec = uivec_from_input(ui, m);

    if (m->input.menu_toggle) {
        if (ui->menu.widget)    main_menu_done(ui);
        else                    main_menu_init(ui);
    } else if (m->input.mouse_click) {
        /* No menu + click miss: open main menu */
        if (!ui->menu.widget && !ui_element_click(ui, uivec))
            main_menu_init(ui);
        /* Menu + menu click miss: close main menu */
        else if (ui->menu.widget && !ui_widget_click(ui->menu.widget, uivec))
            main_menu_done(ui);
    }

    if (!clap_is_paused(ui->clap_ctx))  return MSG_HANDLED;

    if (ui->menu.widget)
        ui->menu.widget->input_event(ui, ui->menu.widget, m);

    return MSG_STOP;
}

cerr ui_menus_init(struct ui *ui, const ui_menu_config *cfg)
{
    if (!cfg || !cfg->enable)
        return CERR_OK;

    const ui_menu_item *root = cfg->menu_root ? cfg->menu_root : &default_menu_root;

    if (cfg->uwb) {
        ui->menu.root_storage = *root;
        ui->menu.root_storage.uwb = cfg->uwb;
        ui->menu.root = &ui->menu.root_storage;
    } else {
        ui->menu.root = root;
    }
    ui->menu.widget = NULL;

    return subscribe(ui->clap_ctx, MT_INPUT, ui_menus_handle_input, ui);
}

void ui_menus_done(struct ui *ui)
{
    if (ui->menu.widget)
        ref_put(ui->menu.widget);
    ui->menu.widget = NULL;
    ui->menu.root = NULL;
}
