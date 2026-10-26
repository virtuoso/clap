/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_UI_MENUS_H__
#define __CLAP_UI_MENUS_H__

#include "error.h"

struct ui;
struct ui_menu_item;
struct ui_widget_builder;
struct clap_context;

/**
 * enum ui_state - high-level UI state machine
 * @UI_ST_PRESTART:     web-build gate before the start menu; a "click
 *                      anywhere" overlay waits for the user gesture that
 *                      unlocks the browser's AudioContext. Native builds
 *                      skip this state and start in %UI_ST_START_MENU.
 * @UI_ST_START_MENU:   start menu is active, clap is paused
 * @UI_ST_LOADING:      "Start Game" selected and its loading callback in
 *                      flight, or a load the game started with
 *                      ui_state_set_loading(); clap is paused
 * @UI_ST_RUNNING:      gameplay; in-game menu is toggleable
 */
typedef enum ui_state {
    UI_ST_PRESTART,
    UI_ST_START_MENU,
    UI_ST_LOADING,
    UI_ST_RUNNING,
} ui_state;

/**
 * struct ui_menu_config - menu configuration for clap_config
 * @enable:             create the menu at clap_init()
 * @menu_root:          root of the menu item tree, NULL for the built-in default
 * @uwb:                widget builder override, NULL for the default; when set,
 *                      takes precedence over @menu_root->uwb
 * @loading_cb:         start menu only: callback fired on "Start Game" click
 * @loading_cb_data:    data pointer passed to @loading_cb
 * @restart:            in-game menu only: callback fired on "Restart" click;
 *                      with nullptr, "Restart" does nothing
 * @restart_data:       data pointer passed to @restart
 * @credits:            start menu only: array of credit lines shown by
 *                      Help -> Credits, NULL for the built-in placeholder
 * @nr_credits:         number of entries in @credits
 */
typedef struct ui_menu_config {
    bool                                enable;
    const struct ui_menu_item           *menu_root;
    const struct ui_widget_builder      *uwb;
    void                                (*loading_cb)(struct clap_context *ctx, void *data);
    void                                *loading_cb_data;
    void                                (*restart)(struct clap_context *ctx, void *data);
    void                                *restart_data;
    const char                          *const *credits;
    unsigned int                        nr_credits;
} ui_menu_config;

/**
 * ui_menus_init() - initialize menu subsystem
 * @ui:             ui context
 * @in_game_cfg:    in-game menu configuration, may be NULL
 * @start_cfg:      start menu configuration, may be NULL
 *
 * Subscribe to input and arm the menu open/close flow. When @start_cfg is
 * enabled, the initial state is %UI_ST_START_MENU, the start menu opens, and
 * clap is paused via a modality toggle. Otherwise the initial state is
 * %UI_ST_RUNNING. No-op and returns %CERR_OK if neither config is enabled.
 * Return: %CERR_OK on success, or an error from subscribe().
 */
cerr ui_menus_init(struct ui *ui,
                   const ui_menu_config *in_game_cfg,
                   const ui_menu_config *start_cfg);

/**
 * ui_menus_done() - tear down menu subsystem
 * @ui:     ui context
 */
void ui_menus_done(struct ui *ui);

/**
 * ui_menus_ascend() - pop one level from the menu stack
 * @ui:     ui context
 *
 * Drop the current menu widget and rebuild it from the parent root stored
 * in &ui_menu.stack. If already at the top of the stack and the state is
 * %UI_ST_RUNNING, close the in-game menu and unpause. At %UI_ST_START_MENU
 * root the call is a no-op.
 */
void ui_menus_ascend(struct ui *ui);

/**
 * ui_state_get() - read current UI state
 * @ui:     ui context
 *
 * Return: the current &enum ui_state.
 */
ui_state ui_state_get(const struct ui *ui);

/**
 * ui_state_set_loading() - transition UI state to %UI_ST_LOADING
 * @ui:     ui context
 *
 * For a game without a start menu that loads while the frames go on: from
 * %UI_ST_RUNNING, closes the in-game menu and pauses clap the way "Start
 * Game" does, so that ui_state_set_running() unpauses it once loading is
 * finished. Does nothing in any other state: the start menu enters
 * %UI_ST_LOADING by itself, and a browser's %UI_ST_PRESTART waits for the
 * click that unlocks its audio.
 */
void ui_state_set_loading(struct ui *ui);

/**
 * ui_state_set_running() - transition UI state to %UI_ST_RUNNING
 * @ui:     ui context
 *
 * Intended to be called once loading is finished, by a user loading_cb or
 * by a game that entered %UI_ST_LOADING with ui_state_set_loading().
 * Unpauses clap (via modality toggle) and allows the in-game menu to
 * respond to input. Idempotent when already running.
 */
void ui_state_set_running(struct ui *ui);

#endif /* __CLAP_UI_MENUS_H__ */
