// SPDX-License-Identifier: Apache-2.0
#include "clap.h"
#include "display.h"
#include "error.h"
#include "input.h"
#include "memory.h"
#include "messagebus.h"
#include "util.h"
#include "ui.h"

#include "onehandclap.h"

typedef struct game_ui {
    struct ui           *ui;
    struct ui_widget    *menu;
} game_ui;

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
    game_ui *gui = ui->priv;
    gui->menu = uiw;
}

static void main_menu_done(game_ui *gui);

static void __unused __menu_hud_fps(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
}

#ifndef CONFIG_FINAL
static void __unused __menu_devel(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
}
#endif /* CONFIG_FINAL */

static void __menu_fullscreen(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
    message_input_send(ui->clap_ctx, &(struct message_input){ .fullscreen = 1 }, NULL);
}

static void __unused __menu_help_license(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
}

static void __unused __menu_help_help(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
}

static void __unused __menu_help_credits(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
}

static __unused void __menu_exit(struct ui *ui, const ui_menu_item *item)
{
    main_menu_done(ui->priv);
    display_request_exit();
}

static const struct ui_widget_builder main_menu_uwb = {
    .el_affinity    = UI_AF_TOP | UI_AF_RIGHT,
    .affinity       = UI_AF_VCENTER | UI_AF_RIGHT | UI_SZ_HEIGHT_FRAC,
    .el_x_off       = 10,
    .el_y_off       = 10,
    .el_w           = 300,
    .el_h           = 100,
    .el_margin      = 4,
    .x_off          = 10,
    .y_off          = 10,
    .w              = 500,
    .h              = 0.8,
    .on_create      = main_menu_on_create,
    .el_cb          = menu_element_cb,
    .el_on_focus    = menu_onfocus,
    .el_color       = { 0.52f, 0.12f, 0.12f, 1.0f },
    .text_color     = { 0.9375f, 0.902344f, 0.859375f, 1.0f },
};

/* Main in-game menu */
static const ui_menu_item main_menu_root = UI_MENU_GROUP(
    NULL, &main_menu_uwb,
    // UI_MENU_GROUP("HUD",            &main_menu_uwb,
    //     UI_MENU_ITEM("FPS",         __menu_hud_fps),
    //     UI_MENU_END
    // ),
#ifndef CONFIG_FINAL
    UI_MENU_ITEM("Devel",           __menu_devel),
#endif /* CONFIG_FINAL */
    UI_MENU_ITEM("Fullscreen",      __menu_fullscreen),
    // UI_MENU_GROUP("Help",           &main_menu_uwb,
    //     UI_MENU_ITEM("..help",      __menu_help_help),
    //     UI_MENU_ITEM("..credits",   __menu_help_credits),
    //     UI_MENU_ITEM("..license",   __menu_help_license),
    //     UI_MENU_END
    // ),
#ifndef CONFIG_BROWSER
    UI_MENU_ITEM("Exit",            __menu_exit),
#endif /* CONFIG_BROWSER */
    UI_MENU_END
);

static void main_menu_init(game_ui *gui)
{
    ui_modality_send(gui->ui);
    gui->menu = ui_menu_new(gui->ui, &main_menu_root);
}

static void main_menu_done(game_ui *gui)
{
    ui_modality_send(gui->ui);
    ref_put(gui->menu);
    gui->menu = NULL;
}

static int game_ui_handle_input(struct clap_context *clap_ctx, struct message *m, void *data)
{
    game_ui *gui = data;
    auto ui = gui->ui;
    uivec uivec = uivec_from_input(ui, m);

    if (m->input.menu_toggle) {
        if (gui->menu)  main_menu_done(gui);
        else            main_menu_init(gui);
    } else if (m->input.mouse_click) {
        /* No menu + click miss: open main menu */
        if (!gui->menu && !ui_element_click(ui, uivec))             main_menu_init(gui);
        /* Menu + menu click miss: close main menu */
        else if (gui->menu && !ui_widget_click(gui->menu, uivec))   main_menu_done(gui);
    }

    if (!clap_is_paused(ui->clap_ctx))  return MSG_HANDLED;

    if (gui->menu)   gui->menu->input_event(ui, gui->menu, m);

    return MSG_STOP;
}

DEFINE_CLEANUP(game_ui, if (*p) mem_free(*p))

cresp(game_ui) game_ui_init(struct ui *ui)
{
    LOCAL_SET(game_ui, game_ui) = mem_alloc(sizeof(*game_ui), .zero = 1);
    if (!game_ui)   return cresp_error(game_ui, CERR_NOMEM);

    game_ui->ui = ui;
    CERR_RET_T(subscribe(ui->clap_ctx, MT_INPUT, game_ui_handle_input, game_ui), game_ui);

    ui->priv = game_ui;
    return cresp_val(game_ui, NOCU(game_ui));
}

void game_ui_done(game_ui *game_ui)
{
    unsubscribe(game_ui->ui->clap_ctx, MT_INPUT, game_ui);
    if (game_ui->menu)  ref_put(game_ui->menu);
    mem_free(game_ui);
}
